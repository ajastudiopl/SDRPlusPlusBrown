#include "sweep_controller.h"

namespace spectrum {
    SweepController::SweepController(SweepEngine& e) : engine(e) {}

    SweepController::~SweepController() {
        stopAndWait();
    }

    bool SweepController::active() const {
        SweepState s = state();
        return s == SweepState::STARTING || s == SweepState::SWEEPING || s == SweepState::PAUSED || s == SweepState::STOPPING;
    }

    std::string SweepController::message() const {
        std::lock_guard<std::mutex> lck(msgMtx);
        return msg;
    }

    void SweepController::setMessage(const std::string& m) {
        std::lock_guard<std::mutex> lck(msgMtx);
        msg = m;
    }

    bool SweepController::start(std::unique_ptr<SweepBackend> backend, const BackendStartParams& params, std::string& error) {
        update();
        if (active() || th.joinable()) {
            error = "A sweep is already running";
            return false;
        }
        if (!backend) {
            error = "No sweep backend";
            return false;
        }
        if (!backend->prepareOnUiThread(params, error)) {
            setMessage(error);
            st = (int)SweepState::FAULT;
            return false;
        }
        be = std::move(backend);
        stopFlag = false;
        workerDone = false;
        setMessage("");
        st = (int)SweepState::STARTING;
        runs++;
        th = std::thread(&SweepController::worker, this, params);
        return true;
    }

    void SweepController::worker(BackendStartParams params) {
        std::string err;
        if (!be->open(params, engine, err)) {
            std::string detail;
            BackendHealth h = be->health(detail);
            be->close(engine);
            setMessage(err);
            st = (int)((h == BackendHealth::DEVICE_LOST) ? SweepState::DEVICE_DISCONNECTED : SweepState::FAULT);
            workerDone = true;
            return;
        }
        if (!stopFlag) { st = (int)(pausedFlag ? SweepState::PAUSED : SweepState::SWEEPING); }

        be->run(engine, stopFlag, pausedFlag);

        std::string detail;
        BackendHealth h = be->health(detail);
        bool failed = (h != BackendHealth::OK) && !stopFlag;
        st = (int)SweepState::STOPPING;
        engine.abortSweep();
        be->close(engine);
        if (failed) {
            setMessage(detail);
            st = (int)((h == BackendHealth::DEVICE_LOST) ? SweepState::DEVICE_DISCONNECTED : SweepState::FAULT);
        }
        else {
            st = (int)SweepState::IDLE;
        }
        workerDone = true;
    }

    void SweepController::requestStop() {
        if (!th.joinable()) { return; }
        stopFlag = true;
        SweepState s = state();
        if (s == SweepState::STARTING || s == SweepState::SWEEPING || s == SweepState::PAUSED) {
            st = (int)SweepState::STOPPING;
        }
    }

    void SweepController::stopAndWait() {
        requestStop();
        if (th.joinable()) { th.join(); }
        update();
    }

    void SweepController::setPaused(bool p) {
        pausedFlag = p;
        SweepState s = state();
        if (s == SweepState::SWEEPING && p) { st = (int)SweepState::PAUSED; }
        else if (s == SweepState::PAUSED && !p) { st = (int)SweepState::SWEEPING; }
    }

    void SweepController::update() {
        if (!workerDone || !th.joinable()) { return; }
        th.join();
        if (be) {
            be->finishOnUiThread();
            be.reset();
        }
    }
}
