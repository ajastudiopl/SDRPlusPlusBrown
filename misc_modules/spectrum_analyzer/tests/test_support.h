#pragma once
// Test helpers: FFT engine for tests and a fake HackRF-style native sweep provider.
#include "../src/engine/fft_engine.h"
#include "../src/engine/synthetic_source.h"
#include <signal_path/native_sweep.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef SA_TEST_HAVE_FFTW
#include <fftw3.h>
#endif

namespace satest {
    using namespace spectrum;

#ifdef SA_TEST_HAVE_FFTW
    // Same library the SDR++ core uses on Linux/Android/Windows.
    class TestFFT : public FFTEngine {
    public:
        explicit TestFFT(int n) : n(n) {
            in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * n);
            out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * n);
            plan = fftwf_plan_dft_1d(n, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
        }
        ~TestFFT() {
            fftwf_destroy_plan(plan);
            fftwf_free(in);
            fftwf_free(out);
        }
        int size() const override { return n; }
        cf32* input() override { return (cf32*)in; }
        const cf32* output() override { return (const cf32*)out; }
        void execute() override { fftwf_execute(plan); }

    private:
        int n;
        fftwf_complex *in, *out;
        fftwf_plan plan;
    };
#else
    // Reference radix-2 FFT used only when FFTW is not installed on the test host.
    class TestFFT : public FFTEngine {
    public:
        explicit TestFFT(int n) : n(n), in(n), out(n), work(n), rev(n) {
            int bits = 0;
            while ((1 << bits) < n) { bits++; }
            for (int i = 0; i < n; i++) {
                int r = 0;
                for (int b = 0; b < bits; b++) {
                    if (i & (1 << b)) { r |= 1 << (bits - 1 - b); }
                }
                rev[i] = r;
            }
        }
        int size() const override { return n; }
        cf32* input() override { return in.data(); }
        const cf32* output() override { return out.data(); }
        void execute() override {
            if ((n & (n - 1)) != 0) {
                // Not a power of two: plain DFT.
                for (int k = 0; k < n; k++) {
                    std::complex<double> acc(0, 0);
                    for (int i = 0; i < n; i++) {
                        double a = -2.0 * M_PI * (double)k * (double)i / (double)n;
                        acc += std::complex<double>(in[i].real(), in[i].imag()) * std::complex<double>(cos(a), sin(a));
                    }
                    out[k] = cf32((float)acc.real(), (float)acc.imag());
                }
                return;
            }
            for (int i = 0; i < n; i++) { work[rev[i]] = std::complex<double>(in[i].real(), in[i].imag()); }
            for (int len = 2; len <= n; len <<= 1) {
                double ang = -2.0 * M_PI / (double)len;
                std::complex<double> wl(cos(ang), sin(ang));
                for (int i = 0; i < n; i += len) {
                    std::complex<double> w(1, 0);
                    for (int j = 0; j < len / 2; j++) {
                        std::complex<double> u = work[i + j];
                        std::complex<double> v = work[i + j + len / 2] * w;
                        work[i + j] = u + v;
                        work[i + j + len / 2] = u - v;
                        w *= wl;
                    }
                }
            }
            for (int i = 0; i < n; i++) { out[i] = cf32((float)work[i].real(), (float)work[i].imag()); }
        }

    private:
        int n;
        std::vector<cf32> in, out;
        std::vector<std::complex<double>> work;
        std::vector<int> rev;
    };
#endif

    inline FFTEngineFactory testFftFactory() {
        return [](int n) { return std::unique_ptr<FFTEngine>(new TestFFT(n)); };
    }

    // Emulates a HackRF running hackrf_init_sweep(INTERLEAVED): blocks of
    // SAMPLES_PER_BLOCK samples tagged with the block frequency, LO = f + 7.5 MHz.
    class FakeHackRFProvider : public sweep::NativeSweepProvider {
    public:
        explicit FakeHackRFProvider(SyntheticSource scene) : scene(std::move(scene)) {}
        ~FakeHackRFProvider() { stopSweep(); }

        int apiVersion() const override { return sweep::NATIVE_SWEEP_API_VERSION; }
        std::string providerName() override { return "FakeHackRF"; }
        std::vector<sweep::NativeSweepDevice> listDevices() override {
            if (!present) { return {}; }
            return { { "fake0", "Fake HackRF" } };
        }
        bool getCaps(const std::string& id, sweep::NativeSweepCaps& caps, std::string& error) override {
            namespace hf = sweep::hackrf_format;
            if (!present) {
                error = "No device";
                return false;
            }
            caps.nativeSweepSupported = firmwareOk;
            caps.firmwareKnown = true;
            caps.supportNote = firmwareOk ? "" : "Firmware too old for sweep mode";
            caps.minFreqHz = 0;
            caps.maxFreqHz = 7250e6;
            caps.sampleRateHz = hf::SAMPLE_RATE_HZ;
            caps.stepWidthHz = hf::STEP_WIDTH_HZ;
            caps.samplesPerBlock = hf::BLOCK_SAMPLES;
            caps.maxDwellBlocks = 4;
            caps.gains = { { "lna", "LNA", 0, 40, 8, 16, false, "" }, { "vga", "VGA", 0, 62, 2, 20, false, "" } };
            return true;
        }
        sweep::Ownership getOwnership(const std::string& id) override {
            if (!present) { return sweep::Ownership::UNAVAILABLE; }
            if (receiverRunning) { return sweep::Ownership::RECEIVER; }
            return running ? sweep::Ownership::SWEEP : sweep::Ownership::FREE;
        }
        std::string receiverSourceName() override { return "FakeHackRF"; }
        bool startSweep(const sweep::NativeSweepRequest& req, sweep::BlockCallback cb, void* ctx, sweep::NativeSweepPlanInfo& plan, std::string& error) override {
            namespace hf = sweep::hackrf_format;
            uint16_t s, e;
            int steps = hf::alignRange(req.startHz, req.stopHz, s, e);
            if (steps <= 0) {
                error = "bad range";
                return false;
            }
            plan.alignedStartHz = s * 1e6;
            plan.alignedStopHz = e * 1e6;
            plan.sampleRateHz = hf::SAMPLE_RATE_HZ;
            plan.stepWidthHz = hf::STEP_WIDTH_HZ;
            plan.loOffsetHz = hf::OFFSET_HZ;
            plan.blockFrequenciesHz = hf::interleavedBlockFrequencies(s, steps);
            plan.validOffsets = hf::validOffsets();
            plan.samplesPerBlock = hf::BLOCK_SAMPLES;
            freqs = plan.blockFrequenciesHz;
            dwell = req.dwellBlocks;
            callback = cb;
            cbCtx = ctx;
            lost = false;
            running = true;
            th = std::thread([this] { streamer(); });
            return true;
        }
        void stopSweep() override {
            running = false;
            if (th.joinable()) { th.join(); }
        }
        sweep::StreamStatus sweepStatus(std::string& detail) override {
            if (lost) {
                detail = "fake device unplugged";
                return sweep::StreamStatus::DEVICE_LOST;
            }
            return running ? sweep::StreamStatus::OK : sweep::StreamStatus::STOPPED;
        }
        bool setSweepGain(const std::string& id, double v) override {
            lastGainId = id;
            lastGainValue = v;
            return true;
        }

        void unplug() { lost = true; }

        std::atomic<bool> present{ true };
        std::atomic<bool> receiverRunning{ false };
        bool firmwareOk = true;
        std::atomic<uint64_t> sweepsSent{ 0 };
        std::string lastGainId;
        double lastGainValue = 0;
        int skipEveryNthBlock = 0; // simulate USB drops

    private:
        void streamer() {
            namespace hf = sweep::hackrf_format;
            std::vector<cf32> samples(hf::BLOCK_SAMPLES);
            std::vector<float> iq(2 * hf::BLOCK_SAMPLES);
            uint64_t blockNo = 0;
            auto t0 = std::chrono::steady_clock::now();
            while (running) {
                for (double f : freqs) {
                    for (int d = 0; d < dwell && running; d++) {
                        if (lost) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                            continue;
                        }
                        blockNo++;
                        if (skipEveryNthBlock > 0 && blockNo % skipEveryNthBlock == 0) { continue; }
                        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                        scene.generate(f + hf::OFFSET_HZ, hf::SAMPLE_RATE_HZ, hf::BLOCK_SAMPLES, t, samples.data());
                        for (int i = 0; i < hf::BLOCK_SAMPLES; i++) {
                            iq[2 * i] = samples[i].real();
                            iq[2 * i + 1] = samples[i].imag();
                        }
                        callback(cbCtx, (uint64_t)f, iq.data(), hf::BLOCK_SAMPLES, nowNs());
                    }
                    if (!running) { break; }
                }
                sweepsSent++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        SyntheticSource scene;
        std::vector<double> freqs;
        int dwell = 1;
        sweep::BlockCallback callback = nullptr;
        void* cbCtx = nullptr;
        std::atomic<bool> running{ false };
        std::atomic<bool> lost{ false };
        std::thread th;
    };

    template <typename Pred>
    bool waitUntil(Pred p, int timeoutMs) {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < end) {
            if (p()) { return true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return p();
    }
}
