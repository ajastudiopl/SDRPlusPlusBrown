#include "sweep_engine.h"
#include <algorithm>
#include <cmath>

namespace spectrum {
    SweepEngine::SweepEngine(FFTEngineFactory factory) : fftFactory(std::move(factory)) {}

    bool SweepEngine::configure(const SweepPlan& plan, const EngineSettings& settings, std::string& error) {
        if (!plan.valid()) {
            error = plan.error.empty() ? "Empty sweep plan" : plan.error;
            return false;
        }
        EngineSettings s = settings;
        s.framesPerCapture = std::clamp(s.framesPerCapture, 1, 256);
        s.sweepsPerLine = std::clamp(s.sweepsPerLine, 1, 100);
        s.waterfallRows = std::clamp(s.waterfallRows, 16, 4096);
        double span = plan.range.spanHz();
        int bins = std::clamp(s.displayBins, 64, 65536);
        if (span > 0 && (double)bins > span) { bins = std::max(64, (int)span); }
        s.displayBins = bins;

        SegmentProcessorConfig pc;
        pc.fftSize = s.fftSize;
        pc.window = s.window;
        pc.maxFrames = s.framesPerCapture;
        pc.sampleRateHz = plan.sampleRateHz;
        if (!proc.configure(pc, fftFactory, error)) { return false; }

        std::lock_guard<std::mutex> lck(mtx);
        currentPlan = plan;
        cfg = s;
        summary.valid = true;
        summary.range = plan.range;
        summary.segments = (int)plan.segments.size();
        summary.stepHz = plan.stepHz;
        summary.sampleRateHz = plan.sampleRateHz;
        summary.usableBandwidthHz = plan.usableBandwidthHz;
        summary.dcCoveredByOverlap = plan.dcCoveredByOverlap;
        summary.native = plan.native;
        summary.warnings = plan.warnings;
        summary.fftSize = s.fftSize;
        summary.window = s.window;
        summary.detector = s.detector;
        assembler.configure(plan.range.startHz, plan.range.stopHz, bins, s.detector, (int)plan.segments.size());
        display.reset(bins);
        display.setSettings(s.display);
        history.configure(bins, s.waterfallRows);
        wfRow.assign(bins, nanValue());
        wfRowSweeps = 0;
        wfRowComplete = true;
        peaks.clear();
        st = EngineStats();
        st.segmentsPerSweep = (int)plan.segments.size();
        st.fftBinHz = proc.binWidthHz();
        st.rbwHz = proc.rbwHz();
        st.displayBinHz = span / (double)bins;
        hasLast = false;
        last = SweepFrame();
        lastNativeSegment = -1;
        lastFinishNs = 0;
        holdUntilNs = 0;
        refreshLimitHz = std::max(0.0, s.refreshLimitHz);

        std::vector<std::pair<double, int>> los;
        for (auto& sg : plan.segments) { los.push_back({ sg.loHz, sg.index }); }
        std::sort(los.begin(), los.end());
        segLoSorted.clear();
        segLoIndex.clear();
        for (auto& p : los) {
            segLoSorted.push_back(p.first);
            segLoIndex.push_back(p.second);
        }
        ver++;
        partialVer++;
        return true;
    }

    void SweepEngine::updateLiveSettings(const EngineSettings& s) {
        std::lock_guard<std::mutex> lck(mtx);
        cfg.display = s.display;
        cfg.peaksEnabled = s.peaksEnabled;
        cfg.peaksOnAverage = s.peaksOnAverage;
        cfg.peaks = s.peaks;
        cfg.sweepsPerLine = std::clamp(s.sweepsPerLine, 1, 100);
        cfg.refreshLimitHz = std::max(0.0, s.refreshLimitHz);
        refreshLimitHz = cfg.refreshLimitHz;
        display.setSettings(cfg.display);
        if (hasLast && cfg.peaksEnabled) {
            peaks = detectPeaks(cfg.peaksOnAverage ? display.average() : display.current(), assembler.startHz(), assembler.binWidthHz(), display.noiseFloorDb(), cfg.peaks);
        }
        else if (!cfg.peaksEnabled) {
            peaks.clear();
        }
        ver++;
    }

    void SweepEngine::beginSweep(int64_t timeNs) {
        std::lock_guard<std::mutex> lck(mtx);
        assembler.beginSweep(timeNs);
        st.sweepActive = true;
        st.segmentsReceived = 0;
        partialVer++;
    }

    bool SweepEngine::processCapture(int segIndex, const cf32* samples, int count, int64_t timeNs) {
        if (segIndex < 0 || segIndex >= (int)currentPlan.segments.size()) {
            std::lock_guard<std::mutex> lck(mtx);
            st.capturesOutsidePlan++;
            return false;
        }
        int frames = proc.process(samples, count);
        if (frames <= 0) {
            std::lock_guard<std::mutex> lck(mtx);
            st.capturesTooShort++;
            return false;
        }
        const Segment& seg = currentPlan.segments[segIndex];
        proc.chunks(seg.loHz, seg.windows, chunkScratch);

        std::lock_guard<std::mutex> lck(mtx);
        if (!assembler.sweepActive()) {
            assembler.beginSweep(timeNs);
            st.sweepActive = true;
        }
        for (auto& c : chunkScratch) { assembler.addChunk(c); }
        assembler.markSegmentReceived(segIndex);
        st.capturesProcessed++;
        st.currentLoHz = seg.loHz;
        st.segmentsReceived = assembler.segmentsReceived();
        partialVer++;
        return true;
    }

    void SweepEngine::finishSweep(bool interrupted, int64_t timeNs) {
        std::lock_guard<std::mutex> lck(mtx);
        if (!assembler.sweepActive()) { return; }
        SweepFrame f;
        assembler.finishSweep(f, interrupted, timeNs);
        st.sweepActive = false;
        st.segmentsReceived = 0;
        partialVer++;
        if (interrupted) {
            st.sweepsInterrupted++;
            return;
        }
        publishFrame(f);
    }

    void SweepEngine::abortSweep() {
        std::lock_guard<std::mutex> lck(mtx);
        if (assembler.sweepActive()) {
            assembler.abortSweep();
            st.sweepsInterrupted++;
        }
        st.sweepActive = false;
        st.segmentsReceived = 0;
        lastNativeSegment = -1;
        partialVer++;
    }

    void SweepEngine::publishFrame(const SweepFrame& f) {
        if (f.complete) { st.sweepsCompleted++; }
        else { st.sweepsIncomplete++; }
        st.lastMissingSegments = f.segmentsExpected - f.segmentsReceived;
        st.lastSweepSec = f.durationSec();
        if (lastFinishNs > 0 && f.endTimeNs > lastFinishNs) {
            double inst = 1e9 / (double)(f.endTimeNs - lastFinishNs);
            st.sweepsPerSec = (st.sweepsPerSec <= 0) ? inst : (0.8 * st.sweepsPerSec + 0.2 * inst);
        }
        lastFinishNs = f.endTimeNs;
        double limit = refreshLimitHz.load();
        if (limit > 0) {
            holdUntilNs = f.startTimeNs + (int64_t)(1e9 / limit);
        }

        display.push(f);

        // Waterfall: one row per `sweepsPerLine` finished sweeps (peak of the merged sweeps).
        for (size_t i = 0; i < wfRow.size() && i < f.db.size(); i++) {
            float x = f.db[i];
            if (std::isfinite(x) && (!std::isfinite(wfRow[i]) || x > wfRow[i])) { wfRow[i] = x; }
        }
        wfRowSweeps++;
        wfRowComplete = wfRowComplete && f.complete;
        if (wfRowSweeps >= cfg.sweepsPerLine) {
            WaterfallRowInfo info;
            info.sequence = f.sequence;
            info.timeNs = f.endTimeNs;
            info.complete = wfRowComplete;
            info.startHz = f.startHz;
            info.stopHz = f.stopHz;
            history.push(wfRow.data(), (int)wfRow.size(), info);
            std::fill(wfRow.begin(), wfRow.end(), nanValue());
            wfRowSweeps = 0;
            wfRowComplete = true;
        }

        if (cfg.peaksEnabled) {
            peaks = detectPeaks(cfg.peaksOnAverage ? display.average() : display.current(), f.startHz, f.binWidthHz(), display.noiseFloorDb(), cfg.peaks);
        }
        last = f;
        hasLast = true;
        st.lastFrameTimeNs = f.endTimeNs;
        ver++;
    }

    int SweepEngine::segmentForBlock(uint64_t blockFreqHz, double loOffsetHz) const {
        if (segLoSorted.empty()) { return -1; }
        double lo = (double)blockFreqHz + loOffsetHz;
        auto it = std::lower_bound(segLoSorted.begin(), segLoSorted.end(), lo);
        int best = -1;
        double bestDiff = 1e300;
        if (it != segLoSorted.end()) {
            best = (int)(it - segLoSorted.begin());
            bestDiff = std::fabs(*it - lo);
        }
        if (it != segLoSorted.begin()) {
            double d = std::fabs(*(it - 1) - lo);
            if (d < bestDiff) {
                best = (int)(it - segLoSorted.begin()) - 1;
                bestDiff = d;
            }
        }
        if (best < 0 || bestDiff > 1000.0) { return -1; }
        return segLoIndex[best];
    }

    void SweepEngine::processNativeBlock(uint64_t blockFreqHz, double loOffsetHz, const cf32* samples, int count, int64_t timeNs) {
        int idx = segmentForBlock(blockFreqHz, loOffsetHz);
        if (idx < 0) {
            std::lock_guard<std::mutex> lck(mtx);
            st.capturesOutsidePlan++;
            return;
        }
        bool active;
        {
            std::lock_guard<std::mutex> lck(mtx);
            active = assembler.sweepActive();
        }
        if (active && idx < lastNativeSegment) {
            // Segment index went backwards: the previous sweep is over.
            finishSweep(false, timeNs);
            active = false;
        }
        if (!active) {
            // Only start on the first segment so every frame covers a full cycle.
            if (idx != 0 || !sweepAllowed(timeNs)) {
                lastNativeSegment = -1;
                return;
            }
            beginSweep(timeNs);
        }
        lastNativeSegment = idx;
        processCapture(idx, samples, count, timeNs);
    }

    void SweepEngine::setDroppedCaptures(uint64_t count) {
        std::lock_guard<std::mutex> lck(mtx);
        st.capturesDropped = count;
    }

    bool SweepEngine::sweepAllowed(int64_t timeNs) const {
        return refreshLimitHz.load() <= 0 || timeNs >= holdUntilNs.load();
    }

    bool SweepEngine::snapshot(EngineSnapshot& out, bool includePartial, bool includePersistence) {
        std::lock_guard<std::mutex> lck(mtx);
        out.version = ver.load();
        out.startHz = assembler.startHz();
        out.stopHz = assembler.stopHz();
        out.current = display.current();
        out.average = display.average();
        out.maxHold = display.maxHold();
        if (includePersistence) { out.persistence = display.persistenceTraces(); }
        else { out.persistence.clear(); }
        if (includePartial && assembler.sweepActive()) { assembler.snapshot(out.partial); }
        else { out.partial.clear(); }
        out.noiseFloorDb = display.noiseFloorDb();
        out.peakDb = display.peakDb();
        out.peaks = peaks;
        out.hasFrame = hasLast;
        out.lastComplete = last.complete;
        out.lastSegmentsExpected = last.segmentsExpected;
        out.lastSegmentsReceived = last.segmentsReceived;
        out.lastSequence = last.sequence;
        out.stats = st;
        return hasLast;
    }

    bool SweepEngine::lastFrame(SweepFrame& out) {
        std::lock_guard<std::mutex> lck(mtx);
        if (!hasLast) { return false; }
        out = last;
        return true;
    }

    EngineStats SweepEngine::stats() {
        std::lock_guard<std::mutex> lck(mtx);
        return st;
    }

    void SweepEngine::resetAverage() {
        std::lock_guard<std::mutex> lck(mtx);
        display.resetAverage();
        ver++;
    }

    void SweepEngine::resetMaxHold() {
        std::lock_guard<std::mutex> lck(mtx);
        display.resetMaxHold();
        ver++;
    }

    void SweepEngine::clearWaterfall() {
        std::lock_guard<std::mutex> lck(mtx);
        history.clear();
        std::fill(wfRow.begin(), wfRow.end(), nanValue());
        wfRowSweeps = 0;
        wfRowComplete = true;
        ver++;
    }

    void SweepEngine::resizeWaterfall(int rows) {
        std::lock_guard<std::mutex> lck(mtx);
        cfg.waterfallRows = std::clamp(rows, 16, 4096);
        history.configure(std::max((int)wfRow.size(), 1), cfg.waterfallRows);
        std::fill(wfRow.begin(), wfRow.end(), nanValue());
        wfRowSweeps = 0;
        wfRowComplete = true;
        ver++;
    }

    PlanSummary SweepEngine::planSummary() {
        std::lock_guard<std::mutex> lck(mtx);
        return summary;
    }
}
