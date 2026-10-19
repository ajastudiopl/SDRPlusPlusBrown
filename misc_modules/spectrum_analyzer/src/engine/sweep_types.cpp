#include "sweep_types.h"
#include <chrono>
#include <cstdio>

namespace spectrum {
    const char* sweepStateName(SweepState s) {
        switch (s) {
        case SweepState::IDLE: return "Idle";
        case SweepState::STARTING: return "Starting";
        case SweepState::SWEEPING: return "Sweeping";
        case SweepState::PAUSED: return "Paused";
        case SweepState::STOPPING: return "Stopping";
        case SweepState::FAULT: return "Error";
        case SweepState::DEVICE_DISCONNECTED: return "Device Disconnected";
        }
        return "Unknown";
    }

    std::string validateRange(const FrequencyRange& range, const RangeLimits& limits) {
        if (!std::isfinite(range.startHz) || !std::isfinite(range.stopHz)) {
            return "Frequencies must be finite numbers";
        }
        if (range.startHz < 0) {
            return "Start frequency cannot be negative";
        }
        if (range.stopHz <= range.startHz) {
            return "Stop frequency must be above the start frequency";
        }
        if (range.spanHz() < limits.minSpanHz) {
            return "Span is smaller than " + formatFrequency(limits.minSpanHz);
        }
        if (limits.maxSpanHz > 0 && range.spanHz() > limits.maxSpanHz) {
            return "Span is larger than " + formatFrequency(limits.maxSpanHz);
        }
        if (limits.maxHz > limits.minHz) {
            if (range.startHz < limits.minHz) {
                return "Start frequency is below the device minimum (" + formatFrequency(limits.minHz) + ")";
            }
            if (range.stopHz > limits.maxHz) {
                return "Stop frequency is above the device maximum (" + formatFrequency(limits.maxHz) + ")";
            }
        }
        return "";
    }

    int64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::string formatFrequency(double hz, int decimals) {
        char buf[64];
        double a = std::fabs(hz);
        if (a >= 1e9) {
            snprintf(buf, sizeof(buf), "%.*f GHz", decimals < 0 ? 6 : decimals, hz / 1e9);
        }
        else if (a >= 1e6) {
            snprintf(buf, sizeof(buf), "%.*f MHz", decimals < 0 ? 4 : decimals, hz / 1e6);
        }
        else if (a >= 1e3) {
            snprintf(buf, sizeof(buf), "%.*f kHz", decimals < 0 ? 3 : decimals, hz / 1e3);
        }
        else {
            snprintf(buf, sizeof(buf), "%.*f Hz", decimals < 0 ? 0 : decimals, hz);
        }
        return buf;
    }
}
