#pragma once
#include "analyzer_settings.h"
#include "sweep_engine.h"
#include <atomic>

namespace spectrum {
    struct BackendStartParams {
        FrequencyRange range;
        SweepSettings sweep;
        EngineSettings engine;
        std::map<std::string, double> gains;
    };

    enum class BackendHealth {
        OK,
        DEVICE_LOST,  // USB device unplugged / stream died
        FAILED        // other error, see detail
    };

    // A way of acquiring a wideband sweep. Threading contract:
    //  - prepareOnUiThread() / finishOnUiThread(): UI thread, before start / after the worker ended
    //  - open() / run() / close(): the controller's worker thread
    //  - setGain(), statusLine(): UI thread, any time
    class SweepBackend {
    public:
        virtual ~SweepBackend() {}
        virtual std::string name() const = 0;
        virtual bool synthetic() const { return false; }
        virtual bool prepareOnUiThread(const BackendStartParams& p, std::string& error) { return true; }
        virtual bool open(const BackendStartParams& p, SweepEngine& engine, std::string& error) = 0;
        // Acquires until `stop` is set or the device fails (then health() != OK).
        virtual void run(SweepEngine& engine, const std::atomic<bool>& stop, const std::atomic<bool>& paused) = 0;
        virtual void close(SweepEngine& engine) = 0;
        virtual void finishOnUiThread() {}
        virtual BackendHealth health(std::string& detail) {
            detail.clear();
            return BackendHealth::OK;
        }
        virtual bool setGain(const std::string& id, double value) { return false; }
        virtual std::string statusLine() { return ""; }
    };
}
