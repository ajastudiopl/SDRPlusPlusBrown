#pragma once
#include "sweep_types.h"
#include <functional>
#include <memory>

namespace spectrum {
    // Forward complex FFT of a fixed size. In the module this wraps the SDR++
    // core FFT plan (dsp::arrays::allocateFFTWPlan: FFTW or Apple vDSP), so the
    // analyzer does not carry its own FFT implementation.
    class FFTEngine {
    public:
        virtual ~FFTEngine() {}
        virtual int size() const = 0;
        virtual cf32* input() = 0;
        virtual const cf32* output() = 0;
        virtual void execute() = 0;
    };

    typedef std::function<std::unique_ptr<FFTEngine>(int size)> FFTEngineFactory;

    std::vector<float> makeWindow(WindowType type, int n);
    // Equivalent noise bandwidth of a window in bins.
    double windowEnbwBins(const std::vector<float>& w);
    const char* windowName(WindowType type);
    // Half width of the window's main lobe in bins (leakage of a strong carrier / DC spike).
    int windowMainLobeBins(WindowType type);
    // Region around the LO treated as DC/LO leakage: main lobe + 1 bin, at least 0.2 % of fs.
    double defaultDcGuardHz(double sampleRateHz, int fftSize, WindowType type);
}
