#include "generic_backend.h"
#include <chrono>
#include <cstdio>
#include <thread>

namespace spectrum {
    void applyGenericAutoParams(SweepSettings& s, EngineSettings& e, double fs, double span, double sourceUsableFraction) {
        if (s.autoParams) {
            s.usableFraction = (sourceUsableFraction > 0.0) ? std::min(sourceUsableFraction, 0.9) : 0.8;
            e.fftSize = autoFftSize(fs, span, e.displayBins, 256, 16384);
            s.dcGuardHz = defaultDcGuardHz(fs, e.fftSize, e.window);
            s.overlapFraction = std::max(0.25, minOverlapForDcCoverage(fs * s.usableFraction, s.dcGuardHz) + 0.02);
            s.stepHz = 0;
        }
        else if (s.dcGuardHz <= 0) {
            s.dcGuardHz = defaultDcGuardHz(fs, e.fftSize, e.window);
        }
    }

    GenericSweepBackend::GenericSweepBackend(std::unique_ptr<IqTuner> t, std::string l) : tuner(std::move(t)), label(std::move(l)) {}

    GenericSweepBackend::~GenericSweepBackend() {
        if (started) { tuner->stop(); }
        tuner.reset();
    }

    bool GenericSweepBackend::prepareOnUiThread(const BackendStartParams& p, std::string& error) {
        return tuner->prepareOnUiThread(error);
    }

    void GenericSweepBackend::finishOnUiThread() {
        tuner->finishOnUiThread();
    }

    void GenericSweepBackend::fail(BackendHealth h, const std::string& detail) {
        std::lock_guard<std::mutex> lck(healthMtx);
        healthState = h;
        healthDetail = detail;
    }

    BackendHealth GenericSweepBackend::health(std::string& detail) {
        std::lock_guard<std::mutex> lck(healthMtx);
        detail = healthDetail;
        return healthState;
    }

    bool GenericSweepBackend::open(const BackendStartParams& p, SweepEngine& engine, std::string& error) {
        fs = tuner->sampleRateHz();
        if (!(fs > 0)) {
            error = "The receiver source reports no sample rate. Select a device in the Source menu first.";
            return false;
        }
        SweepSettings s = p.sweep;
        EngineSettings e = p.engine;
        applyGenericAutoParams(s, e, fs, p.range.spanHz(), tuner->usableFraction());
        if (s.dwellMs > 0) {
            int frames = (int)(s.dwellMs * 1e-3 * fs / (double)e.fftSize);
            e.framesPerCapture = std::max(e.framesPerCapture, frames);
        }
        e.framesPerCapture = std::clamp(e.framesPerCapture, 1, 256);

        GenericPlanParams gp;
        gp.range = p.range;
        gp.sampleRateHz = fs;
        gp.usableFraction = s.usableFraction;
        gp.overlapFraction = s.overlapFraction;
        gp.dcGuardHz = s.dcGuardHz;
        gp.stepOverrideHz = s.stepHz;
        gp.direction = s.direction;
        currentPlan = planGenericSweep(gp);
        if (!currentPlan.valid()) {
            error = currentPlan.error;
            return false;
        }
        if (!engine.configure(currentPlan, e, error)) { return false; }

        need = engine.samplesNeeded();
        settle = (int)(s.settleMs * 1e-3 * fs);
        ring.configure(std::max<size_t>((size_t)need * 4, (size_t)1 << 18));
        captureBuf.assign(need, cf32(0, 0));
        if (!tuner->start(ring, error)) { return false; }
        started = true;
        return true;
    }

    GenericSweepBackend::Wait GenericSweepBackend::waitSamples(uint64_t target, double timeoutSec, const std::atomic<bool>& stop, const std::atomic<bool>& paused) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds((int64_t)(timeoutSec * 1000.0));
        while (true) {
            if (stop || paused) { return Wait::STOPPED; }
            if (ring.waitFor(target, 50)) { return Wait::OK; }
            if (ring.closed()) { return Wait::TIMEOUT; }
            if (std::chrono::steady_clock::now() > deadline) { return Wait::TIMEOUT; }
        }
    }

    void GenericSweepBackend::run(SweepEngine& engine, const std::atomic<bool>& stop, const std::atomic<bool>& paused) {
        const double timeoutSec = 2.0 + (double)(settle + need) / fs;
        while (!stop) {
            if (paused) {
                engine.abortSweep();
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (!engine.sweepAllowed(nowNs())) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            engine.beginSweep(nowNs());
            bool aborted = false;
            for (const Segment& seg : currentPlan.segments) {
                if (stop || paused) {
                    aborted = true;
                    break;
                }
                auto t0 = std::chrono::steady_clock::now();
                if (!tuner->tune(seg.loHz)) {
                    fail(BackendHealth::FAILED, "Retuning the receiver to " + formatFrequency(seg.loHz) + " failed");
                    engine.abortSweep();
                    return;
                }
                double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                double prev = lastRetuneMs.load();
                lastRetuneMs = (prev <= 0) ? ms : (0.9 * prev + 0.1 * ms);

                uint64_t from = ring.written() + (uint64_t)settle;
                Wait w = waitSamples(from + (uint64_t)need, timeoutSec, stop, paused);
                if (w == Wait::STOPPED) {
                    aborted = true;
                    break;
                }
                if (w == Wait::TIMEOUT) {
                    std::string detail;
                    bool alive = tuner->alive(detail);
                    if (detail.empty()) { detail = "No IQ samples from " + tuner->name() + " (receiver stopped or device disconnected)"; }
                    fail(alive ? BackendHealth::FAILED : BackendHealth::DEVICE_LOST, detail);
                    engine.abortSweep();
                    return;
                }
                if (!ring.copy(from, need, captureBuf.data())) { continue; }
                engine.processCapture(seg.index, captureBuf.data(), need, nowNs());
            }
            if (aborted) { engine.abortSweep(); }
            else { engine.finishSweep(false, nowNs()); }
        }
    }

    void GenericSweepBackend::close(SweepEngine& engine) {
        ring.close();
        if (started) {
            tuner->stop();
            started = false;
        }
    }

    std::string GenericSweepBackend::statusLine() {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s | fs %s | %d segments, step %s | settle %.0f ms | retune call %.1f ms",
                 tuner->name().c_str(), formatFrequency(fs, 3).c_str(), (int)currentPlan.segments.size(),
                 formatFrequency(currentPlan.stepHz, 3).c_str(), fs > 0 ? 1000.0 * settle / fs : 0.0, lastRetuneMs.load());
        return buf;
    }
}
