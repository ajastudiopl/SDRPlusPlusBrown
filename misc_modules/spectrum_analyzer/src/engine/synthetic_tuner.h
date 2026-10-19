#pragma once
#include "generic_backend.h"
#include "synthetic_source.h"
#include <thread>

namespace spectrum {
    // IqTuner producing synthetic samples. Used by the unit tests and by the
    // demo mode, which is always labelled as synthetic in the UI and exports.
    struct SyntheticTunerConfig {
        double sampleRateHz = 10e6;
        bool onDemand = true;            // produce samples only when the sweep waits for them
        int retuneLatencySamples = 0;    // samples still at the old LO after tune() (pipeline delay)
        double retuneDelayMs = 0.0;      // time spent inside tune() (PLL / USB control transfer)
        double realtimeFactor = 0.0;     // >0: pace production at fs * factor samples per second
        int blockSize = 4096;
        double minHz = 0;
        double maxHz = 7.25e9;
    };

    class SyntheticTuner : public IqTuner {
    public:
        SyntheticTuner(SyntheticSource source, const SyntheticTunerConfig& cfg);
        ~SyntheticTuner();

        std::string name() override { return "Demo (synthetic signals)"; }
        bool synthetic() const override { return true; }
        bool start(SampleRing& ring, std::string& error) override;
        void stop() override;
        double sampleRateHz() override { return cfg.sampleRateHz; }
        double usableFraction() override { return 0.8; }
        bool tune(double hz) override;
        bool alive(std::string& detail) override;

        // Test hooks
        void simulateDisconnect() { disconnected = true; }
        uint64_t tuneCount() const { return tunes.load(); }

    private:
        void producer();

        SyntheticSource src;
        SyntheticTunerConfig cfg;
        SampleRing* ring = nullptr;
        std::thread th;
        std::atomic<bool> running{ false };
        std::atomic<bool> disconnected{ false };
        std::atomic<uint64_t> tunes{ 0 };
        std::mutex loMtx;
        double curLo = 100e6;
        double oldLo = 100e6;
        int64_t sinceTune = 1 << 30;
        int64_t t0 = 0;
    };
}
