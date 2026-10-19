#include "segment_processor.h"
#include <algorithm>
#include <cmath>

namespace spectrum {
    std::vector<float> makeWindow(WindowType type, int n) {
        std::vector<float> w(n, 1.0f);
        if (n <= 1) { return w; }
        const double twoPi = 2.0 * M_PI;
        for (int i = 0; i < n; i++) {
            double x = (double)i / (double)(n - 1);
            double v = 1.0;
            switch (type) {
            case WindowType::RECTANGULAR:
                v = 1.0;
                break;
            case WindowType::HANN:
                v = 0.5 - 0.5 * cos(twoPi * x);
                break;
            case WindowType::BLACKMAN:
                v = 0.42 - 0.5 * cos(twoPi * x) + 0.08 * cos(2.0 * twoPi * x);
                break;
            case WindowType::NUTTALL:
                v = 0.355768 - 0.487396 * cos(twoPi * x) + 0.144232 * cos(2.0 * twoPi * x) - 0.012604 * cos(3.0 * twoPi * x);
                break;
            case WindowType::BLACKMAN_HARRIS:
                v = 0.35875 - 0.48829 * cos(twoPi * x) + 0.14128 * cos(2.0 * twoPi * x) - 0.01168 * cos(3.0 * twoPi * x);
                break;
            }
            w[i] = (float)v;
        }
        return w;
    }

    double windowEnbwBins(const std::vector<float>& w) {
        double s = 0, s2 = 0;
        for (float v : w) {
            s += v;
            s2 += (double)v * v;
        }
        if (s == 0) { return 1.0; }
        return (double)w.size() * s2 / (s * s);
    }

    const char* windowName(WindowType type) {
        switch (type) {
        case WindowType::RECTANGULAR: return "Rectangular";
        case WindowType::HANN: return "Hann";
        case WindowType::BLACKMAN: return "Blackman";
        case WindowType::NUTTALL: return "Nuttall";
        case WindowType::BLACKMAN_HARRIS: return "Blackman-Harris";
        }
        return "?";
    }

    int windowMainLobeBins(WindowType type) {
        switch (type) {
        case WindowType::RECTANGULAR: return 1;
        case WindowType::HANN: return 2;
        case WindowType::BLACKMAN: return 3;
        case WindowType::NUTTALL: return 4;
        case WindowType::BLACKMAN_HARRIS: return 4;
        }
        return 4;
    }

    double defaultDcGuardHz(double sampleRateHz, int fftSize, WindowType type) {
        if (fftSize <= 0) { return 0.0; }
        double bins = (double)(windowMainLobeBins(type) + 1) * sampleRateHz / (double)fftSize;
        return std::max(bins, sampleRateHz * 0.002);
    }

    bool SegmentProcessor::configure(const SegmentProcessorConfig& c, const FFTEngineFactory& factory, std::string& error) {
        if (c.fftSize < 16 || c.fftSize > 1 << 20) {
            error = "FFT size out of range";
            return false;
        }
        if (!(c.sampleRateHz > 0)) {
            error = "Sample rate must be positive";
            return false;
        }
        cfg = c;
        cfg.maxFrames = std::max(1, c.maxFrames);
        if (!fft || fft->size() != cfg.fftSize) {
            fft = factory ? factory(cfg.fftSize) : nullptr;
            if (!fft) {
                error = "Could not create FFT plan";
                return false;
            }
        }
        window = makeWindow(cfg.window, cfg.fftSize);
        double sum = 0;
        for (float v : window) { sum += v; }
        norm = 1.0 / (sum * sum);
        enbwBins = windowEnbwBins(window);
        accum.assign(cfg.fftSize, 0.0f);
        powerCentered.assign(cfg.fftSize, 0.0f);
        return true;
    }

    int SegmentProcessor::process(const cf32* samples, int count) {
        const int n = cfg.fftSize;
        int frames = std::min(cfg.maxFrames, count / n);
        if (frames <= 0) { return 0; }
        std::fill(accum.begin(), accum.end(), 0.0f);
        const cf32* base = samples + (count - frames * n);
        cf32* in = fft->input();
        for (int f = 0; f < frames; f++) {
            const cf32* s = base + f * n;
            for (int i = 0; i < n; i++) { in[i] = s[i] * window[i]; }
            fft->execute();
            const cf32* out = fft->output();
            for (int i = 0; i < n; i++) { accum[i] += std::norm(out[i]); }
        }
        const float scale = (float)(norm / (double)frames);
        const int half = n / 2;
        for (int k = 0; k < n; k++) {
            powerCentered[k] = accum[(k + half) % n] * scale;
        }
        return frames;
    }

    void SegmentProcessor::chunks(double loHz, const std::vector<ValidWindow>& windows, std::vector<BinChunk>& out) const {
        out.clear();
        const int n = cfg.fftSize;
        const double bw = binWidthHz();
        const int half = n / 2;
        for (auto& w : windows) {
            // Bin k has centre offset (k - n/2) * bw; take lo <= offset < hi.
            int kmin = (int)std::ceil(w.loOffsetHz / bw - 1e-9) + half;
            int kmax = (int)std::ceil(w.hiOffsetHz / bw - 1e-9) + half - 1;
            kmin = std::max(kmin, 0);
            kmax = std::min(kmax, n - 1);
            if (kmax < kmin) { continue; }
            BinChunk c;
            c.firstFreqHz = loHz + (double)(kmin - half) * bw;
            c.binWidthHz = bw;
            c.count = kmax - kmin + 1;
            c.power = powerCentered.data() + kmin;
            c.priority = w.priority;
            out.push_back(c);
        }
    }
}
