#pragma once
#include <stdint.h>
#include <sdrpp_export.h>

// Spectrum of the whole device band, shown on top of the waterfall when the server is in channel mode
// and the client itself only receives the narrow channel.
namespace wideoverview {
    SDRPP_EXPORT void push(double centerFrequency, double span, const uint8_t* bins, int count);
    SDRPP_EXPORT void reset();
    SDRPP_EXPORT void drawOverlay();

    // The modulation chosen in the radio (radio module numbering) and its bandwidth, for the server that demodulates.
    // The offset is the station relative to the centre of the waterfall.
    SDRPP_EXPORT void setDemod(int mode, double bandwidth, double offset);
    SDRPP_EXPORT void getDemod(int* mode, double* bandwidth, double* offset);
    // In server audio mode there is no IQ stream. The server's spectrum then feeds the normal waterfall, which
    // behaves as in any other session of the whole band, and tuning is always done by moving the VFO in it.
    SDRPP_EXPORT void setAudioSession(bool active);
    SDRPP_EXPORT bool isAudioSession();
}
