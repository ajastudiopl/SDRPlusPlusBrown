#pragma once
#include <dsp/types.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

// Demodulator used by the server in "server audio" mode. It takes the narrow IF produced by a VFO
// (ifRate(), already cut to vfoBandwidth() around vfoShift() from the station) and makes 48 kHz mono audio.
// Mode numbers are the radio module ones: 0 NFM, 1 WFM, 2 AM, 3 DSB, 4 USB, 5 CW, 6 LSB, 7 RAW.
class ServerDemod {
public:
    static const int AUDIO_RATE = 48000;

    void setMode(int newMode, double newBandwidth) {
        std::lock_guard<std::mutex> lck(mtx);
        mode = (newMode >= 0 && newMode <= 7) ? newMode : 0;
        double def = mode == 1 ? 150000 : mode == 0 ? 12500 : mode == 2 || mode == 7 ? 10000 : mode == 3 ? 6000 : mode == 5 ? 500 : 2700;
        bandwidth = newBandwidth > 100 ? newBandwidth : def;
        if (mode == 1) { bandwidth = std::min(bandwidth, 180000.0); }
        else { bandwidth = std::min(bandwidth, 20000.0); }
        design();
    }

    int getMode() { std::lock_guard<std::mutex> lck(mtx); return mode; }
    double ifRate() { std::lock_guard<std::mutex> lck(mtx); return mode == 1 ? 192000.0 : 48000.0; }
    double vfoBandwidth() { std::lock_guard<std::mutex> lck(mtx); return bandwidth; }

    // where the VFO has to be centred relative to the station
    double vfoShift() {
        std::lock_guard<std::mutex> lck(mtx);
        if (mode == 4) { return bandwidth / 2; }
        if (mode == 6) { return -bandwidth / 2; }
        return 0;
    }

    // Appends audio samples (AUDIO_RATE, roughly -1..1) made from IF samples.
    void process(const dsp::complex_t* in, int count, std::vector<float>& out) {
        std::lock_guard<std::mutex> lck(mtx);
        if (taps.empty()) { design(); }
        const int ntaps = (int)taps.size();
        for (int i = 0; i < count; i++) {
            float v = demodSample(in[i]);
            hist[histPos] = v;
            histPos = (histPos + 1) % ntaps;
            if (++phase < decim) { continue; }
            phase = 0;
            float acc = 0;
            int p = histPos;
            for (int k = 0; k < ntaps; k++) {
                p = p == 0 ? ntaps - 1 : p - 1;
                acc += taps[k] * hist[p];
            }
            out.push_back(postProcess(acc));
        }
    }

private:
    float demodSample(const dsp::complex_t& s) {
        switch (mode) {
        case 0:
        case 1: {
            // quadrature discriminator
            float re = s.re * prev.re + s.im * prev.im;
            float im = s.im * prev.re - s.re * prev.im;
            prev = s;
            float ph = atan2f(im, re);
            return ph * fmScale;
        }
        case 2:
        case 7: {
            float mag = sqrtf(s.re * s.re + s.im * s.im);
            dc += (mag - dc) * 0.0005f;
            return mag - dc;
        }
        default: {
            // SSB / CW / DSB: shift so that the wanted audio ends up at positive frequencies, then take the real part
            float c = cosf(mixPhase), sn = sinf(mixPhase);
            mixPhase += mixStep;
            if (mixPhase > 6.2831853f) { mixPhase -= 6.2831853f; }
            if (mixPhase < -6.2831853f) { mixPhase += 6.2831853f; }
            return s.re * c - s.im * sn;
        }
        }
    }

    float postProcess(float v) {
        if (mode == 1) {
            // 50 us de-emphasis at the audio rate
            deemph += deemphAlpha * (v - deemph);
            return deemph;
        }
        if (mode >= 2) {
            // slow AGC
            float a = fabsf(v);
            peak = a > peak ? a : peak * 0.99985f;
            float gain = 0.5f / std::max(peak, 1e-5f);
            gain = std::min(gain, 5000.0f);
            return v * gain;
        }
        return v;
    }

    void design() {
        double rate = mode == 1 ? 192000.0 : 48000.0;
        decim = mode == 1 ? 4 : 1;
        double cutoff = mode == 1 ? 15000 : mode == 0 ? std::min(4500.0, bandwidth / 2) : mode == 2 || mode == 7 ? std::min(5000.0, bandwidth / 2) : mode == 5 ? 400 : std::min(bandwidth, 10000.0);
        if (mode == 5) { cutoff = 1200; }
        int n = mode == 1 ? 193 : 129;
        taps.assign(n, 0.0f);
        double fc = cutoff / rate;
        double sum = 0;
        for (int i = 0; i < n; i++) {
            double x = i - (n - 1) / 2.0;
            double sinc = x == 0 ? 2 * fc : sin(2 * M_PI * fc * x) / (M_PI * x);
            double w = 0.54 - 0.46 * cos(2 * M_PI * i / (n - 1));
            taps[i] = (float)(sinc * w);
            sum += taps[i];
        }
        for (auto& t : taps) { t = (float)(t / sum); }
        hist.assign(n, 0.0f);
        histPos = 0;
        phase = 0;
        prev = dsp::complex_t{ 1, 0 };
        deemph = 0;
        dc = 0;
        peak = 0;
        mixPhase = 0;
        double dev = mode == 1 ? 75000.0 : bandwidth / 2;
        fmScale = (float)(rate / (2 * M_PI * dev));
        deemphAlpha = (float)(1.0 - exp(-1.0 / (50e-6 * AUDIO_RATE)));
        double shift = mode == 4 ? bandwidth / 2 : mode == 6 ? -bandwidth / 2 : mode == 5 ? 700 : 0;
        mixStep = (float)(2 * M_PI * shift / rate);
    }

    std::mutex mtx;
    int mode = 0;
    double bandwidth = 12500;
    int decim = 1;
    int phase = 0;
    std::vector<float> taps;
    std::vector<float> hist;
    int histPos = 0;
    dsp::complex_t prev{ 1, 0 };
    float fmScale = 1;
    float deemph = 0, deemphAlpha = 0.3f;
    float dc = 0;
    float peak = 0;
    float mixPhase = 0, mixStep = 0;
};
