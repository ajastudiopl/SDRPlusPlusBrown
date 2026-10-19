#pragma once
#include "fft_engine.h"

namespace spectrum {
    // Contiguous run of FFT bins (linear power, tone calibrated: a full scale
    // complex tone on a bin centre reads 1.0 = 0 dBFS for every window).
    struct BinChunk {
        double firstFreqHz = 0; // centre frequency of the first bin
        double binWidthHz = 0;
        int count = 0;
        const float* power = nullptr;
        uint8_t priority = PRIORITY_CLEAN;
    };

    struct SegmentProcessorConfig {
        int fftSize = 2048;
        WindowType window = WindowType::BLACKMAN_HARRIS;
        int maxFrames = 1;          // FFT frames averaged per capture (Welch, no overlap)
        double sampleRateHz = 0;
    };

    // Turns one IQ capture taken at a known LO into calibrated power bins.
    // Buffers are allocated once in configure(), process() does not allocate.
    class SegmentProcessor {
    public:
        bool configure(const SegmentProcessorConfig& cfg, const FFTEngineFactory& factory, std::string& error);

        // Averages up to maxFrames FFT frames taken from the END of the capture
        // (the samples furthest away from the retune). Returns the number of
        // frames used, 0 if the capture is shorter than one FFT.
        int process(const cf32* samples, int count);

        // Splits the last processed spectrum into chunks covering `windows`.
        void chunks(double loHz, const std::vector<ValidWindow>& windows, std::vector<BinChunk>& out) const;

        const std::vector<float>& power() const { return powerCentered; }
        int fftSize() const { return cfg.fftSize; }
        double binWidthHz() const { return cfg.sampleRateHz / (double)cfg.fftSize; }
        double rbwHz() const { return binWidthHz() * enbwBins; }
        int samplesNeeded() const { return cfg.fftSize * cfg.maxFrames; }
        const SegmentProcessorConfig& config() const { return cfg; }

    private:
        SegmentProcessorConfig cfg;
        std::unique_ptr<FFTEngine> fft;
        std::vector<float> window;
        std::vector<float> accum;
        std::vector<float> powerCentered;
        double norm = 1.0;
        double enbwBins = 1.0;
    };
}
