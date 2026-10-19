#include "synthetic_source.h"
#include <cmath>

namespace spectrum {
    void SyntheticSource::generate(double loHz, double fs, int n, double timeSec, cf32* out) {
        std::normal_distribution<float> gauss(0.0f, 1.0f);
        // Complex noise with total power noiseDbfs per sample.
        float sigma = sqrtf(powf(10.0f, noiseDbfs / 10.0f) / 2.0f);
        for (int i = 0; i < n; i++) { out[i] = cf32(gauss(rng) * sigma, gauss(rng) * sigma); }

        if (std::isfinite(dcDbfs)) {
            float a = sqrtf(powf(10.0f, dcDbfs / 10.0f));
            for (int i = 0; i < n; i++) { out[i] += cf32(a, 0.0f); }
        }

        const double half = fs / 2.0;
        for (size_t e = 0; e < emitters.size(); e++) {
            const SyntheticEmitter& em = emitters[e];
            if (em.dutyCycle < 1.0 && em.burstPeriodSec > 0) {
                double ph = fmod(timeSec + (double)e * 0.37, em.burstPeriodSec) / em.burstPeriodSec;
                if (ph > em.dutyCycle) { continue; }
            }
            double fc = em.freqHz;
            if (em.hopPeriodSec > 0 && em.hopSpanHz > 0) {
                long hop = (long)std::floor(timeSec / em.hopPeriodSec);
                uint32_t h = (uint32_t)(hop * 2654435761u + e * 40503u);
                double frac = (double)(h % 1000) / 1000.0;
                fc = em.freqHz - em.hopSpanHz / 2.0 + frac * em.hopSpanHz;
            }
            int carriers = (em.bandwidthHz > 0) ? 9 : 1;
            float amp = sqrtf(powf(10.0f, em.levelDbfs / 10.0f) / (float)carriers);
            for (int c = 0; c < carriers; c++) {
                double f = fc;
                if (carriers > 1) { f = fc - em.bandwidthHz / 2.0 + em.bandwidthHz * (double)c / (double)(carriers - 1); }
                double off = f - loHz;
                if (off <= -half || off >= half) { continue; }
                double w = 2.0 * M_PI * off / fs;
                double phase0 = fmod(2.0 * M_PI * off * timeSec, 2.0 * M_PI) + (double)(e * 13 + c * 7);
                std::complex<double> z = std::polar((double)amp, phase0);
                const std::complex<double> rot = std::polar(1.0, w);
                for (int i = 0; i < n; i++) {
                    out[i] += cf32((float)z.real(), (float)z.imag());
                    z *= rot;
                }
            }
        }
    }

    SyntheticSource SyntheticSource::demoScene() {
        SyntheticSource s(4242);
        s.setNoiseDbfs(-85.0f);
        s.setDcSpikeDbfs(-45.0f);
        // Broadcast FM band: wide carriers of various strengths.
        const double fm[] = { 88.6e6, 89.8e6, 91.0e6, 92.4e6, 94.3e6, 96.0e6, 97.7e6, 99.1e6, 101.4e6, 102.9e6, 104.7e6, 106.2e6, 107.5e6 };
        for (int i = 0; i < (int)(sizeof(fm) / sizeof(fm[0])); i++) {
            SyntheticEmitter e;
            e.freqHz = fm[i];
            e.levelDbfs = -30.0f - (float)((i * 7) % 25);
            e.bandwidthHz = 150e3;
            s.addEmitter(e);
        }
        // Airband AM voice, bursty.
        const double air[] = { 118.1e6, 121.5e6, 124.375e6, 127.85e6, 132.0e6 };
        for (int i = 0; i < 5; i++) {
            SyntheticEmitter e;
            e.freqHz = air[i];
            e.levelDbfs = -50.0f - (float)(i * 3);
            e.bandwidthHz = 8e3;
            e.dutyCycle = 0.4;
            e.burstPeriodSec = 3.0 + i;
            s.addEmitter(e);
        }
        // 2 m / 70 cm carriers and a frequency hopping emitter.
        SyntheticEmitter e1;
        e1.freqHz = 145.5e6;
        e1.levelDbfs = -45.0f;
        e1.bandwidthHz = 12e3;
        e1.dutyCycle = 0.5;
        e1.burstPeriodSec = 4.0;
        s.addEmitter(e1);
        SyntheticEmitter e2;
        e2.freqHz = 433.92e6;
        e2.levelDbfs = -40.0f;
        e2.hopSpanHz = 1.5e6;
        e2.hopPeriodSec = 0.5;
        s.addEmitter(e2);
        SyntheticEmitter e3;
        e3.freqHz = 446.05e6;
        e3.levelDbfs = -55.0f;
        e3.bandwidthHz = 10e3;
        e3.dutyCycle = 0.3;
        e3.burstPeriodSec = 5.0;
        s.addEmitter(e3);
        SyntheticEmitter e4;
        e4.freqHz = 466.0e6;
        e4.levelDbfs = -60.0f;
        s.addEmitter(e4);
        return s;
    }
}
