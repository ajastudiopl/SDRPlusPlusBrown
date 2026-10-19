#include "native_backend.h"
#include <chrono>
#include <cstdio>
#include <thread>

namespace spectrum {
    void applyNativeAutoParams(SweepSettings& s, EngineSettings& e, const sweep::NativeSweepCaps& caps, double span) {
        int maxFft = 256;
        while (maxFft * 2 <= caps.samplesPerBlock) { maxFft *= 2; }
        if (s.autoParams) {
            e.fftSize = autoFftSize(caps.sampleRateHz, span, e.displayBins, 256, maxFft);
            e.framesPerCapture = 1;
            s.nativeDwellBlocks = 1;
        }
        e.fftSize = std::clamp(e.fftSize, 64, maxFft);
        // Frames are taken from one block; more blocks per tuning give more captures instead.
        int framesMax = std::max(1, caps.samplesPerBlock / e.fftSize);
        e.framesPerCapture = std::clamp(e.framesPerCapture, 1, framesMax);
        s.nativeDwellBlocks = std::clamp(s.nativeDwellBlocks, 1, std::max(1, caps.maxDwellBlocks));
    }

    NativeSweepBackend::NativeSweepBackend(sweep::NativeSweepProvider* p, std::string id, std::string l)
        : provider(p), deviceId(std::move(id)), label(std::move(l)) {}

    void NativeSweepBackend::fail(BackendHealth h, const std::string& detail) {
        std::lock_guard<std::mutex> lck(healthMtx);
        healthState = h;
        healthDetail = detail;
    }

    BackendHealth NativeSweepBackend::health(std::string& detail) {
        std::lock_guard<std::mutex> lck(healthMtx);
        detail = healthDetail;
        return healthState;
    }

    void NativeSweepBackend::onBlock(void* ctx, uint64_t freqHz, const float* iq, int count, int64_t timeNs) {
        NativeSweepBackend* _this = (NativeSweepBackend*)ctx;
        _this->blocksIn.fetch_add(1, std::memory_order_relaxed);
        if (_this->dropAll.load(std::memory_order_relaxed)) { return; }
        _this->queue.push(freqHz, timeNs, iq, count);
    }

    bool NativeSweepBackend::open(const BackendStartParams& p, SweepEngine& engine, std::string& error) {
        if (!provider) {
            error = "No native sweep provider";
            return false;
        }
        sweep::NativeSweepCaps caps;
        if (!provider->getCaps(deviceId, caps, error)) { return false; }
        if (caps.firmwareKnown && !caps.nativeSweepSupported) {
            error = caps.supportNote.empty() ? "The device does not support native sweep" : caps.supportNote;
            return false;
        }
        sweep::Ownership own = provider->getOwnership(deviceId);
        if (own == sweep::Ownership::RECEIVER) {
            error = "The device is in use by the receiver. Stop the receiver before starting the sweep.";
            return false;
        }
        if (own == sweep::Ownership::SWEEP) {
            error = "A sweep is already running on this device.";
            return false;
        }
        if (own == sweep::Ownership::UNAVAILABLE) {
            error = "The device is not available (not connected or the driver failed to initialize).";
            fail(BackendHealth::DEVICE_LOST, error);
            return false;
        }

        SweepSettings s = p.sweep;
        EngineSettings e = p.engine;
        applyNativeAutoParams(s, e, caps, p.range.spanHz());
        fftSize = e.fftSize;

        queue.configure(64, e.fftSize * e.framesPerCapture);
        dropAll = true; // until the engine is configured

        sweep::NativeSweepRequest req;
        req.deviceId = deviceId;
        req.startHz = p.range.startHz;
        req.stopHz = p.range.stopHz;
        req.dwellBlocks = s.nativeDwellBlocks;
        for (auto& g : caps.gains) {
            double v = g.defaultValue;
            auto it = p.gains.find(g.id);
            if (it != p.gains.end()) { v = it->second; }
            req.gains.push_back({ g.id, v });
        }
        if (!provider->startSweep(req, &NativeSweepBackend::onBlock, this, planInfo, error)) {
            std::string detail;
            if (provider->sweepStatus(detail) == sweep::StreamStatus::DEVICE_LOST) { fail(BackendHealth::DEVICE_LOST, error); }
            return false;
        }
        sweeping = true;

        std::vector<std::pair<double, double>> offs;
        for (auto& v : planInfo.validOffsets) { offs.push_back({ v.loHz, v.hiHz }); }
        SweepPlan plan = planFromNativeInfo(p.range, planInfo.sampleRateHz, planInfo.loOffsetHz, planInfo.blockFrequenciesHz, offs);
        if (!plan.valid()) {
            error = plan.error;
            return false;
        }
        if (planInfo.samplesPerBlock < e.fftSize) {
            error = "FFT size is larger than one sweep block";
            return false;
        }
        if (!engine.configure(plan, e, error)) { return false; }
        queue.drain();
        dropAll = false;
        return true;
    }

    void NativeSweepBackend::run(SweepEngine& engine, const std::atomic<bool>& stop, const std::atomic<bool>& paused) {
        auto lastBlock = std::chrono::steady_clock::now();
        bool wasPaused = false;
        BlockQueue::Block b;
        while (!stop) {
            if (paused) {
                if (!wasPaused) {
                    dropAll = true;
                    engine.abortSweep();
                    wasPaused = true;
                }
                queue.drain();
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                lastBlock = std::chrono::steady_clock::now();
                continue;
            }
            if (wasPaused) {
                queue.drain();
                dropAll = false;
                wasPaused = false;
            }
            if (queue.pop(b, 100)) {
                engine.processNativeBlock(b.freqHz, planInfo.loOffsetHz, b.samples, b.count, b.timeNs);
                queue.release();
                engine.setDroppedCaptures(queue.droppedCount());
                lastBlock = std::chrono::steady_clock::now();
                continue;
            }
            std::string detail;
            sweep::StreamStatus status = provider->sweepStatus(detail);
            if (status == sweep::StreamStatus::DEVICE_LOST) {
                fail(BackendHealth::DEVICE_LOST, detail.empty() ? "The device stopped streaming (disconnected?)" : detail);
                return;
            }
            if (status == sweep::StreamStatus::FAILED || status == sweep::StreamStatus::STOPPED) {
                fail(BackendHealth::FAILED, detail.empty() ? "The device stopped sweeping" : detail);
                return;
            }
            double idle = std::chrono::duration<double>(std::chrono::steady_clock::now() - lastBlock).count();
            if (idle > watchdogSec) {
                fail(BackendHealth::DEVICE_LOST, "No data from the device for " + std::to_string((int)idle) + " s (disconnected?)");
                return;
            }
        }
    }

    void NativeSweepBackend::close(SweepEngine& engine) {
        dropAll = true;
        if (sweeping) {
            provider->stopSweep();
            sweeping = false;
        }
        queue.wake();
    }

    bool NativeSweepBackend::setGain(const std::string& id, double value) {
        if (!sweeping) { return false; }
        return provider->setSweepGain(id, value);
    }

    std::string NativeSweepBackend::statusLine() {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s | fs %s | %d tunings/sweep | FFT %d | blocks %llu, dropped %llu",
                 label.c_str(), formatFrequency(planInfo.sampleRateHz, 1).c_str(), (int)planInfo.blockFrequenciesHz.size(), fftSize,
                 (unsigned long long)blocksIn.load(), (unsigned long long)queue.droppedCount());
        return buf;
    }
}
