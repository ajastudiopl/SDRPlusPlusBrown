#pragma once
#include "engine/fft_engine.h"
#include <utils/arrays.h>
#include <mutex>

namespace spectrum {
    // FFT through the SDR++ core plan (FFTW, or Apple vDSP when enabled), the
    // same engine the receiver waterfall uses.
    class CoreFFTEngine : public FFTEngine {
    public:
        explicit CoreFFTEngine(int n) : n(n) {
            // FFTW's planner is not thread safe; plans are created rarely (sweep start).
            std::lock_guard<std::mutex> lck(planMutex());
            plan = dsp::arrays::allocateFFTWPlan(false, n);
            in = plan->getInput();
            out = plan->getOutput();
        }
        int size() const override { return n; }
        cf32* input() override { return (cf32*)in->data(); }
        const cf32* output() override { return (const cf32*)out->data(); }
        // Passing the plan's own input avoids any copy/allocation in the wrapper.
        void execute() override { plan->npfftfft(in); }

        static std::mutex& planMutex() {
            static std::mutex m;
            return m;
        }

    private:
        int n;
        dsp::arrays::Arg<dsp::arrays::FFTPlan> plan;
        dsp::arrays::ComplexArray in;
        dsp::arrays::ComplexArray out;
    };

    inline FFTEngineFactory coreFftFactory() {
        return [](int n) { return std::unique_ptr<FFTEngine>(new CoreFFTEngine(n)); };
    }
}
