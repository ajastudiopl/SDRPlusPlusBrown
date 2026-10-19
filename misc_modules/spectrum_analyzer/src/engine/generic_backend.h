#pragma once
#include "sample_ring.h"
#include "sweep_backend.h"
#include <memory>

namespace spectrum {
    // Abstraction of a receiver that can be retuned and delivers IQ samples.
    // In SDR++ it is implemented on top of the selected source + IQ front end;
    // tests and the demo mode use SyntheticTuner.
    class IqTuner {
    public:
        virtual ~IqTuner() {}
        virtual std::string name() = 0;
        virtual bool synthetic() const { return false; }
        // UI thread: e.g. switch/start the receiver source.
        virtual bool prepareOnUiThread(std::string& error) { return true; }
        // UI thread: restore the receiver after the sweep.
        virtual void finishOnUiThread() {}
        // Worker thread: start delivering samples into `ring`.
        virtual bool start(SampleRing& ring, std::string& error) = 0;
        virtual void stop() = 0;
        virtual double sampleRateHz() = 0;
        // Usable part of the sample rate reported by the source (0 = unknown).
        virtual double usableFraction() { return 0.0; }
        virtual bool tune(double hz) = 0;
        // False if the sample stream died; detail explains why.
        virtual bool alive(std::string& detail) { return true; }
    };

    // Applies "Auto" sweep parameters for a stepped sweep at sample rate fs.
    void applyGenericAutoParams(SweepSettings& s, EngineSettings& e, double fs, double span, double sourceUsableFraction);

    // Software stepped sweep: tune -> wait for settling -> capture -> FFT ->
    // merge -> next segment; one complete pass is one sweep frame.
    // Settling is counted in samples received after the retune returned, so it
    // does not depend on scheduling jitter.
    class GenericSweepBackend : public SweepBackend {
    public:
        GenericSweepBackend(std::unique_ptr<IqTuner> tuner, std::string label);
        ~GenericSweepBackend();

        std::string name() const override { return label; }
        bool synthetic() const override { return tuner->synthetic(); }
        bool prepareOnUiThread(const BackendStartParams& p, std::string& error) override;
        bool open(const BackendStartParams& p, SweepEngine& engine, std::string& error) override;
        void run(SweepEngine& engine, const std::atomic<bool>& stop, const std::atomic<bool>& paused) override;
        void close(SweepEngine& engine) override;
        void finishOnUiThread() override;
        BackendHealth health(std::string& detail) override;
        std::string statusLine() override;

        // Values chosen at open() (for the UI / exports).
        const SweepPlan& plan() const { return currentPlan; }
        double sampleRateHz() const { return fs; }
        int settleSamples() const { return settle; }
        int captureSamples() const { return need; }
        IqTuner* getTuner() { return tuner.get(); }

    private:
        enum class Wait { OK, STOPPED, TIMEOUT };
        Wait waitSamples(uint64_t target, double timeoutSec, const std::atomic<bool>& stop, const std::atomic<bool>& paused);
        void fail(BackendHealth h, const std::string& detail);

        SampleRing ring; // declared before the tuner: the tuner writes into it until destroyed
        std::unique_ptr<IqTuner> tuner;
        std::string label;
        SweepPlan currentPlan;
        std::vector<cf32> captureBuf;
        double fs = 0;
        int settle = 0;
        int need = 0;
        std::atomic<bool> started{ false };
        std::mutex healthMtx;
        BackendHealth healthState = BackendHealth::OK;
        std::string healthDetail;
        std::atomic<double> lastRetuneMs{ 0.0 };
    };
}
