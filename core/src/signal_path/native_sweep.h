#pragma once
// Native (hardware-assisted) frequency sweep interface.
//
// A source module whose hardware/firmware can sweep on its own (HackRF
// `hackrf_init_sweep` / `hackrf_start_rx_sweep`) exposes this interface through
// ModuleManager::Instance::getInterface(sweep::NATIVE_SWEEP_INTERFACE).
// Consumers (the Spectrum Analyzer module) find providers with
// core::moduleManager.getAllInterfaces<sweep::NativeSweepProvider>(...).
//
// The provider keeps exclusive ownership of the device handle: it refuses to
// start a sweep while its receiver stream is running on the same device and
// refuses to start the receiver while a sweep is running.
//
// Header-only and free of core dependencies on purpose, so it can be used by
// unit tests without linking sdrpp_core.

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace sweep {
    inline constexpr const char* NATIVE_SWEEP_INTERFACE = "NativeSweepProvider";
    inline constexpr int NATIVE_SWEEP_API_VERSION = 1;

    struct GainControl {
        std::string id;     // provider specific id, e.g. "lna", "vga", "amp", "bias"
        std::string label;  // human readable label
        double minValue = 0;
        double maxValue = 0;
        double step = 1;
        double defaultValue = 0;
        bool isToggle = false; // on/off control (value 0 or 1)
        std::string warning;   // shown next to the control (e.g. bias-T powers the antenna port)
    };

    struct NativeSweepDevice {
        std::string id;    // identifier passed back to startSweep() (serial number)
        std::string label; // human readable name
    };

    struct NativeSweepCaps {
        bool nativeSweepSupported = false; // false if library/firmware cannot sweep (when known)
        bool firmwareKnown = false;        // true if the firmware was actually queried
        std::string supportNote;           // explanation shown to the user
        double minFreqHz = 0;              // tunable range
        double maxFreqHz = 0;
        double specMinFreqHz = 0;          // range guaranteed by the manufacturer
        double specMaxFreqHz = 0;
        double sampleRateHz = 0;           // sample rate used while sweeping (fixed)
        double stepWidthHz = 0;            // tuning step of one sweep step
        double basebandFilterHz = 0;
        int samplesPerBlock = 0;           // complex samples delivered per block
        int maxDwellBlocks = 1;            // max blocks captured per tuning
        std::vector<GainControl> gains;
        std::string libraryVersion;
        std::string firmwareVersion;
        std::string boardName;
    };

    enum class Ownership {
        FREE,        // nobody uses the device
        RECEIVER,    // the receiver stream of the source module is running on it
        SWEEP,       // a native sweep is running on it
        UNAVAILABLE  // device missing or library failed to initialize
    };

    struct NativeSweepRequest {
        std::string deviceId;
        double startHz = 0;
        double stopHz = 0;
        int dwellBlocks = 1;
        std::vector<std::pair<std::string, double>> gains; // GainControl::id -> value
    };

    // Valid part of a capture, as offsets relative to the tuned LO frequency.
    struct ValidOffsetRange {
        double loHz;
        double hiHz;
    };

    // Filled by the provider when a sweep starts.
    struct NativeSweepPlanInfo {
        double alignedStartHz = 0;  // range actually swept (provider grid)
        double alignedStopHz = 0;
        double sampleRateHz = 0;
        double stepWidthHz = 0;
        double loOffsetHz = 0;      // LO frequency = block frequency + loOffsetHz
        std::vector<double> blockFrequenciesHz; // block frequencies of one sweep, ascending
        std::vector<ValidOffsetRange> validOffsets;
        int samplesPerBlock = 0;    // samples delivered per block callback
    };

    enum class StreamStatus {
        OK,
        STOPPED,      // not sweeping
        DEVICE_LOST,  // USB transfer thread died (device unplugged / reset)
        FAILED
    };

    // Called from the device's USB thread for every block. `iq` holds `count`
    // interleaved float I/Q pairs scaled to [-1, 1). Must return quickly and
    // must not call back into the provider.
    typedef void (*BlockCallback)(void* ctx, uint64_t blockFreqHz, const float* iq, int count, int64_t timestampNs);

    class NativeSweepProvider {
    public:
        virtual ~NativeSweepProvider() {}
        virtual int apiVersion() const = 0;
        virtual std::string providerName() = 0;
        virtual std::vector<NativeSweepDevice> listDevices() = 0;
        virtual bool getCaps(const std::string& deviceId, NativeSweepCaps& caps, std::string& error) = 0;
        virtual Ownership getOwnership(const std::string& deviceId) = 0;
        // Name under which the provider's receiver is registered in the SourceManager.
        virtual std::string receiverSourceName() = 0;
        virtual bool startSweep(const NativeSweepRequest& req, BlockCallback cb, void* ctx, NativeSweepPlanInfo& plan, std::string& error) = 0;
        virtual void stopSweep() = 0;
        virtual StreamStatus sweepStatus(std::string& detail) = 0;
        virtual bool setSweepGain(const std::string& id, double value) = 0;
    };

    // HackRF sweep transfer format (firmware `sweep_mode`, libhackrf >= 0.5).
    namespace hackrf_format {
        inline constexpr int BLOCK_BYTES = 16384;       // libhackrf BYTES_PER_BLOCK (a macro there)
        inline constexpr int HEADER_BYTES = 10;         // 0x7F 0x7F + uint64 LE frequency
        inline constexpr int SKIP_BYTES = 16;           // header rounded up to an even sample boundary
        inline constexpr int BLOCK_SAMPLES = (BLOCK_BYTES - SKIP_BYTES) / 2;
        inline constexpr double SAMPLE_RATE_HZ = 20e6;  // hackrf_sweep DEFAULT_SAMPLE_RATE_HZ
        inline constexpr double BASEBAND_FILTER_HZ = 15e6;
        inline constexpr double STEP_WIDTH_HZ = 20e6;   // TUNE_STEP
        inline constexpr double OFFSET_HZ = 7.5e6;      // OFFSET
        inline constexpr double MAX_FREQ_MHZ = 7250;    // FREQ_MAX_MHZ in hackrf_sweep
        inline constexpr int MAX_RANGES = 10;

        // Returns false if the block does not start with the sweep header.
        inline bool parseHeader(const uint8_t* block, uint64_t& freqHz) {
            if (block[0] != 0x7F || block[1] != 0x7F) { return false; }
            uint64_t f = 0;
            for (int i = 9; i >= 2; i--) { f = (f << 8) | block[i]; }
            freqHz = f;
            return true;
        }

        inline void writeHeader(uint8_t* block, uint64_t freqHz) {
            block[0] = 0x7F;
            block[1] = 0x7F;
            for (int i = 0; i < 8; i++) { block[2 + i] = (uint8_t)((freqHz >> (8 * i)) & 0xFF); }
        }

        // Aligns a requested range to the hackrf_sweep grid (1 MHz start, 20 MHz steps).
        // Returns the number of 20 MHz steps, 0 if the range is invalid.
        inline int alignRange(double startHz, double stopHz, uint16_t& startMHz, uint16_t& stopMHz) {
            if (!(stopHz > startHz) || startHz < 0) { return 0; }
            double s = startHz / 1e6;
            double e = stopHz / 1e6;
            if (e > MAX_FREQ_MHZ) { e = MAX_FREQ_MHZ; }
            long smhz = (long)s;
            long emhz = (long)e;
            if ((double)emhz < e) { emhz++; }
            if (emhz <= smhz) { emhz = smhz + 1; }
            long stepMHz = (long)(STEP_WIDTH_HZ / 1e6);
            long steps = 1 + (emhz - smhz - 1) / stepMHz;
            // Keep the last LO inside the tunable range.
            while (steps > 1 && smhz + steps * stepMHz > (long)MAX_FREQ_MHZ) { steps--; }
            if (smhz + steps * stepMHz > (long)MAX_FREQ_MHZ) { return 0; }
            startMHz = (uint16_t)smhz;
            stopMHz = (uint16_t)(smhz + steps * stepMHz);
            return (int)steps;
        }

        // Block frequencies the firmware emits for one interleaved sweep
        // (two tunings per 20 MHz step: f and f + step/4).
        inline std::vector<double> interleavedBlockFrequencies(uint16_t startMHz, int steps) {
            std::vector<double> out;
            out.reserve(steps * 2);
            for (int i = 0; i < steps; i++) {
                double f = ((double)startMHz * 1e6) + (double)i * STEP_WIDTH_HZ;
                out.push_back(f);
                out.push_back(f + STEP_WIDTH_HZ / 4.0);
            }
            return out;
        }

        // Usable part of each capture relative to the LO: hackrf_sweep keeps the
        // bins between 2.5 and 7.5 MHz on both sides of the LO, discarding the DC
        // region and the baseband filter roll-off.
        inline std::vector<ValidOffsetRange> validOffsets() {
            return { { -OFFSET_HZ, -SAMPLE_RATE_HZ / 8.0 }, { SAMPLE_RATE_HZ / 8.0, OFFSET_HZ } };
        }
    }
}
