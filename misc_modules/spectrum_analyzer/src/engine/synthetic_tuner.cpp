#include "synthetic_tuner.h"
#include <chrono>

namespace spectrum {
    SyntheticTuner::SyntheticTuner(SyntheticSource source, const SyntheticTunerConfig& c) : src(std::move(source)), cfg(c) {}

    SyntheticTuner::~SyntheticTuner() {
        stop();
    }

    bool SyntheticTuner::start(SampleRing& r, std::string& error) {
        if (running) { return true; }
        ring = &r;
        running = true;
        t0 = nowNs();
        th = std::thread(&SyntheticTuner::producer, this);
        return true;
    }

    void SyntheticTuner::stop() {
        running = false;
        if (ring) { ring->close(); }
        if (th.joinable()) { th.join(); }
        ring = nullptr;
    }

    bool SyntheticTuner::tune(double hz) {
        if (hz < cfg.minHz || hz > cfg.maxHz) { return false; }
        if (cfg.retuneDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds((int64_t)(cfg.retuneDelayMs * 1000.0)));
        }
        std::lock_guard<std::mutex> lck(loMtx);
        oldLo = curLo;
        curLo = hz;
        sinceTune = 0;
        tunes++;
        return true;
    }

    bool SyntheticTuner::alive(std::string& detail) {
        if (disconnected) {
            detail = "Synthetic device disconnected (test)";
            return false;
        }
        return true;
    }

    void SyntheticTuner::producer() {
        std::vector<cf32> block(cfg.blockSize);
        std::vector<cf32> tmp(cfg.blockSize);
        uint64_t produced = 0;
        auto start = std::chrono::steady_clock::now();
        while (running) {
            if (disconnected) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (cfg.onDemand && !ring->waitForDemand(20)) { continue; }
            if (cfg.realtimeFactor > 0) {
                double due = (double)produced / (cfg.sampleRateHz * cfg.realtimeFactor);
                double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                if (due > el) {
                    std::this_thread::sleep_for(std::chrono::microseconds((int64_t)((due - el) * 1e6)));
                }
            }
            double lo, old;
            int64_t since;
            {
                std::lock_guard<std::mutex> lck(loMtx);
                lo = curLo;
                old = oldLo;
                since = sinceTune;
                sinceTune += cfg.blockSize;
            }
            double t = (double)(nowNs() - t0) * 1e-9;
            int n = cfg.blockSize;
            if (since >= cfg.retuneLatencySamples) {
                src.generate(lo, cfg.sampleRateHz, n, t, block.data());
            }
            else {
                // First samples after a retune still carry the old LO.
                int oldCount = (int)std::min<int64_t>(n, cfg.retuneLatencySamples - since);
                src.generate(old, cfg.sampleRateHz, oldCount, t, block.data());
                if (oldCount < n) {
                    src.generate(lo, cfg.sampleRateHz, n - oldCount, t, tmp.data());
                    std::copy(tmp.begin(), tmp.begin() + (n - oldCount), block.begin() + oldCount);
                }
            }
            ring->write(block.data(), n);
            produced += n;
        }
    }
}
