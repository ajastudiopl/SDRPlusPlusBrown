#pragma once
#include "block_queue.h"
#include "sweep_backend.h"
#include <signal_path/native_sweep.h>

namespace spectrum {
    // Applies "Auto" parameters for a native sweep.
    void applyNativeAutoParams(SweepSettings& s, EngineSettings& e, const sweep::NativeSweepCaps& caps, double span);

    // Hardware sweep through a sweep::NativeSweepProvider (HackRF). The device
    // streams blocks tagged with their tuning frequency from its USB thread;
    // they are queued without blocking and processed on the worker thread.
    class NativeSweepBackend : public SweepBackend {
    public:
        NativeSweepBackend(sweep::NativeSweepProvider* provider, std::string deviceId, std::string label);

        std::string name() const override { return label; }
        bool open(const BackendStartParams& p, SweepEngine& engine, std::string& error) override;
        void run(SweepEngine& engine, const std::atomic<bool>& stop, const std::atomic<bool>& paused) override;
        void close(SweepEngine& engine) override;
        BackendHealth health(std::string& detail) override;
        bool setGain(const std::string& id, double value) override;
        std::string statusLine() override;

        const sweep::NativeSweepPlanInfo& info() const { return planInfo; }
        uint64_t blocksDropped() const { return queue.droppedCount(); }

        // Seconds without any block before the device is considered lost.
        double watchdogSec = 3.0;

    private:
        static void onBlock(void* ctx, uint64_t freqHz, const float* iq, int count, int64_t timeNs);
        void fail(BackendHealth h, const std::string& detail);

        sweep::NativeSweepProvider* provider;
        std::string deviceId;
        std::string label;
        BlockQueue queue;
        sweep::NativeSweepPlanInfo planInfo;
        std::atomic<bool> sweeping{ false };
        std::atomic<bool> dropAll{ true };
        std::atomic<uint64_t> blocksIn{ 0 };
        std::mutex healthMtx;
        BackendHealth healthState = BackendHealth::OK;
        std::string healthDetail;
        int fftSize = 0;
    };
}
