#pragma once
#include "sweep_backend.h"
#include <memory>
#include <mutex>
#include <thread>

namespace spectrum {
    // Owns the acquisition worker thread and the device for the duration of a
    // sweep. States: Idle -> Starting -> Sweeping <-> Paused -> Stopping -> Idle,
    // with Error / Device Disconnected when the backend fails. Start and stop
    // never block the UI thread on device I/O: opening and closing the device
    // happen on the worker thread, update() collects the finished worker.
    class SweepController {
    public:
        explicit SweepController(SweepEngine& engine);
        ~SweepController();

        // UI thread. Fails if a sweep is already active.
        bool start(std::unique_ptr<SweepBackend> backend, const BackendStartParams& params, std::string& error);
        void requestStop();
        void stopAndWait();
        void setPaused(bool paused);
        bool paused() const { return pausedFlag.load(); }
        // UI thread, every frame: joins a finished worker and lets the backend restore receiver state.
        void update();

        SweepState state() const { return (SweepState)st.load(); }
        bool active() const; // Starting / Sweeping / Paused / Stopping
        std::string message() const;
        SweepBackend* backend() { return be.get(); }
        uint64_t runCount() const { return runs; }

    private:
        void worker(BackendStartParams params);
        void setMessage(const std::string& m);

        SweepEngine& engine;
        std::unique_ptr<SweepBackend> be;
        std::thread th;
        std::atomic<bool> stopFlag{ false };
        std::atomic<bool> pausedFlag{ false };
        std::atomic<bool> workerDone{ true };
        std::atomic<int> st{ (int)SweepState::IDLE };
        mutable std::mutex msgMtx;
        std::string msg;
        uint64_t runs = 0;
    };
}
