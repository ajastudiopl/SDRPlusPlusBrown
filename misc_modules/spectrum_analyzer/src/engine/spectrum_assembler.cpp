#include "spectrum_assembler.h"
#include <algorithm>
#include <cmath>

namespace spectrum {
    void SpectrumAssembler::configure(double startHz, double stopHz, int bins, Detector det, int expectedSegments) {
        start = startHz;
        stop = stopHz;
        bins = std::max(bins, 1);
        binWidth = (stop - start) / (double)bins;
        detector = det;
        acc.assign(bins, 0.0f);
        cnt.assign(bins, 0);
        prio.assign(bins, 0);
        segReceived.assign(std::max(expectedSegments, 0), 0);
        received = 0;
        active = false;
    }

    void SpectrumAssembler::reset() {
        std::fill(acc.begin(), acc.end(), 0.0f);
        std::fill(cnt.begin(), cnt.end(), 0);
        std::fill(prio.begin(), prio.end(), 0);
        std::fill(segReceived.begin(), segReceived.end(), 0);
        received = 0;
    }

    void SpectrumAssembler::beginSweep(int64_t timeNs) {
        reset();
        active = true;
        sweepStartNs = timeNs;
    }

    inline void SpectrumAssembler::combine(int j, float p, uint8_t pr) {
        if (pr > prio[j]) {
            prio[j] = pr;
            acc[j] = p;
            cnt[j] = 1;
        }
        else if (pr == prio[j]) {
            if (detector == Detector::PEAK) {
                if (p > acc[j]) { acc[j] = p; }
            }
            else {
                acc[j] += p;
            }
            cnt[j]++;
        }
    }

    void SpectrumAssembler::addChunk(const BinChunk& c) {
        if (!active || c.count <= 0 || acc.empty()) { return; }
        const int n = (int)acc.size();
        const double bw = c.binWidthHz;
        if (bw <= binWidth) {
            // Several FFT bins per display bin: each FFT bin goes to the display bin holding its centre.
            for (int i = 0; i < c.count; i++) {
                double f = c.firstFreqHz + (double)i * bw;
                double pos = (f - start) / binWidth;
                if (pos < 0.0 || pos >= (double)n) { continue; }
                float p = c.power[i];
                if (!std::isfinite(p)) { continue; }
                combine((int)pos, p, c.priority);
            }
        }
        else {
            // Display finer than FFT resolution: an FFT bin fills every display bin it overlaps.
            for (int i = 0; i < c.count; i++) {
                double f = c.firstFreqHz + (double)i * bw;
                double lo = (f - bw / 2.0 - start) / binWidth;
                double hi = (f + bw / 2.0 - start) / binWidth;
                if (hi <= 0.0 || lo >= (double)n) { continue; }
                float p = c.power[i];
                if (!std::isfinite(p)) { continue; }
                int j0 = std::max(0, (int)std::floor(lo));
                int j1 = std::min(n - 1, (int)std::ceil(hi) - 1);
                for (int j = j0; j <= j1; j++) { combine(j, p, c.priority); }
            }
        }
    }

    void SpectrumAssembler::markSegmentReceived(int idx) {
        if (idx < 0 || idx >= (int)segReceived.size()) { return; }
        if (!segReceived[idx]) {
            segReceived[idx] = 1;
            received++;
        }
    }

    void SpectrumAssembler::snapshot(std::vector<float>& outDb) const {
        outDb.resize(acc.size());
        for (size_t j = 0; j < acc.size(); j++) {
            if (!active || cnt[j] == 0) {
                outDb[j] = nanValue();
                continue;
            }
            float v = (detector == Detector::PEAK) ? acc[j] : acc[j] / (float)cnt[j];
            outDb[j] = 10.0f * log10f(std::max(v, 1e-30f));
        }
    }

    void SpectrumAssembler::finishSweep(SweepFrame& out, bool interrupted, int64_t timeNs) {
        out.sequence = sequence++;
        out.startHz = start;
        out.stopHz = stop;
        out.segmentsExpected = (int)segReceived.size();
        out.segmentsReceived = received;
        out.interrupted = interrupted;
        out.complete = !interrupted && received == (int)segReceived.size() && !segReceived.empty();
        out.startTimeNs = sweepStartNs;
        out.endTimeNs = timeNs;
        out.db.resize(acc.size());
        for (size_t j = 0; j < acc.size(); j++) {
            if (cnt[j] == 0) {
                out.db[j] = nanValue();
                continue;
            }
            float v = (detector == Detector::PEAK) ? acc[j] : acc[j] / (float)cnt[j];
            out.db[j] = 10.0f * log10f(std::max(v, 1e-30f));
        }
        reset();
        active = false;
    }

    void SpectrumAssembler::abortSweep() {
        reset();
        active = false;
    }
}
