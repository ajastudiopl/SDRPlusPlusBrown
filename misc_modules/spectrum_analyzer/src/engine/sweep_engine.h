#pragma once
#include "display_processor.h"
#include "peak_detector.h"
#include "spectrum_assembler.h"
#include "sweep_planner.h"
#include "waterfall_history.h"
#include <atomic>
#include <mutex>

namespace spectrum {
    struct EngineSettings {
        int fftSize = 2048;
        WindowType window = WindowType::BLACKMAN_HARRIS;
        int framesPerCapture = 1;        // FFT frames averaged per capture
        Detector detector = Detector::PEAK;
        int displayBins = 4096;          // wideband grid size (clamped 64..65536)
        DisplaySettings display;
        int waterfallRows = 400;         // history length (bounded by memory limit)
        int sweepsPerLine = 1;           // complete sweeps merged into one waterfall row
        bool peaksEnabled = true;
        bool peaksOnAverage = false;
        PeakDetectorConfig peaks;
        double refreshLimitHz = 0;       // max sweeps per second, 0 = unlimited
    };

    struct EngineStats {
        uint64_t sweepsCompleted = 0;    // all segments present
        uint64_t sweepsIncomplete = 0;   // finished with missing segments
        uint64_t sweepsInterrupted = 0;  // aborted (stop/pause/restart)
        uint64_t capturesProcessed = 0;
        uint64_t capturesTooShort = 0;
        uint64_t capturesDropped = 0;    // lost before processing (queue overflow, USB)
        uint64_t capturesOutsidePlan = 0;
        double lastSweepSec = 0;         // measured duration of the last finished sweep
        double sweepsPerSec = 0;         // measured rate of finished sweeps (EMA)
        int segmentsPerSweep = 0;
        int lastMissingSegments = 0;
        int segmentsReceived = 0;        // in the sweep in progress
        double currentLoHz = 0;
        bool sweepActive = false;
        double fftBinHz = 0;
        double rbwHz = 0;
        double displayBinHz = 0;
        int64_t lastFrameTimeNs = 0;
    };

    // Thread-safe summary of the active plan for the UI / exports.
    struct PlanSummary {
        bool valid = false;
        FrequencyRange range;
        int segments = 0;
        double stepHz = 0;
        double sampleRateHz = 0;
        double usableBandwidthHz = 0;
        bool dcCoveredByOverlap = true;
        bool native = false;
        std::vector<std::string> warnings;
        int fftSize = 0;
        WindowType window = WindowType::BLACKMAN_HARRIS;
        Detector detector = Detector::PEAK;
    };

    struct EngineSnapshot {
        uint64_t version = 0;
        double startHz = 0;
        double stopHz = 0;
        std::vector<float> current;
        std::vector<float> average;
        std::vector<float> maxHold;
        std::vector<float> partial;      // sweep in progress (NaN where not measured yet)
        std::deque<std::vector<float>> persistence;
        float noiseFloorDb = nanValue();
        float peakDb = nanValue();
        std::vector<DetectedPeak> peaks;
        bool hasFrame = false;
        bool lastComplete = false;
        int lastSegmentsExpected = 0;
        int lastSegmentsReceived = 0;
        uint64_t lastSequence = 0;
        EngineStats stats;
    };

    // Connects a sweep plan, the per-capture FFT, the wideband assembler, the
    // display traces, the waterfall history and peak detection.
    // Acquisition calls (beginSweep/processCapture/finishSweep/processNativeBlock)
    // come from a single acquisition thread; snapshot()/waterfall()/reset*()
    // may be called from the UI thread at any time.
    class SweepEngine {
    public:
        explicit SweepEngine(FFTEngineFactory factory);

        bool configure(const SweepPlan& plan, const EngineSettings& settings, std::string& error);
        void updateLiveSettings(const EngineSettings& settings); // display / peaks / waterfall speed / refresh limit

        // ---- acquisition thread ----
        void beginSweep(int64_t timeNs);
        // Processes a capture of plan segment `segIndex`.
        bool processCapture(int segIndex, const cf32* samples, int count, int64_t timeNs);
        void finishSweep(bool interrupted, int64_t timeNs);
        void abortSweep();
        // Native sweeps: maps a block frequency to its plan segment, detects
        // sweep boundaries (segment index going backwards) and only starts a
        // new sweep on the first segment, so partial sweeps are not shown as complete.
        void processNativeBlock(uint64_t blockFreqHz, double loOffsetHz, const cf32* samples, int count, int64_t timeNs);
        void setDroppedCaptures(uint64_t count);
        // True when a new sweep may start (refresh rate limit).
        bool sweepAllowed(int64_t timeNs) const;
        int samplesNeeded() const { return proc.samplesNeeded(); }

        // ---- any thread ----
        uint64_t version() const { return ver.load(); }
        uint64_t partialVersion() const { return partialVer.load(); }
        bool snapshot(EngineSnapshot& out, bool includePartial, bool includePersistence);
        bool lastFrame(SweepFrame& out);
        EngineStats stats();
        // Acquisition thread only (the plan changes in configure()).
        const SweepPlan& plan() const { return currentPlan; }
        PlanSummary planSummary();
        WaterfallHistory& waterfall() { return history; }
        double rbwHz() const { return proc.rbwHz(); }
        void resetAverage();
        void resetMaxHold();
        void clearWaterfall();
        // Changes the history length (clears the history), safe while sweeping.
        void resizeWaterfall(int rows);

    private:
        void publishFrame(const SweepFrame& f);
        int segmentForBlock(uint64_t blockFreqHz, double loOffsetHz) const;

        FFTEngineFactory fftFactory;
        SweepPlan currentPlan;
        EngineSettings cfg;
        SegmentProcessor proc;
        std::vector<BinChunk> chunkScratch;
        std::vector<double> segLoSorted;
        std::vector<int> segLoIndex;

        std::mutex mtx; // protects everything below
        PlanSummary summary;
        SpectrumAssembler assembler;
        DisplayProcessor display;
        WaterfallHistory history;
        SweepFrame last;
        bool hasLast = false;
        std::vector<float> wfRow;
        int wfRowSweeps = 0;
        bool wfRowComplete = true;
        std::vector<DetectedPeak> peaks;
        EngineStats st;
        int lastNativeSegment = -1;
        int64_t lastFinishNs = 0;
        std::atomic<int64_t> holdUntilNs{ 0 };
        std::atomic<double> refreshLimitHz{ 0.0 };

        std::atomic<uint64_t> ver{ 0 };
        std::atomic<uint64_t> partialVer{ 0 };
    };
}
