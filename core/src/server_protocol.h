#pragma once
#include <stdint.h>
#include <gui/smgui.h>
#include <dsp/types.h>

#define SERVER_MAX_PACKET_SIZE  (STREAM_BUFFER_SIZE * sizeof(dsp::complex_t) * 2 + 1024) // metadata added

namespace server {

    static const int SDRPP_BROWN_MAGIC = 0x0b5a1000;
    static const int TX_WIRE_SAMPLERATE = 6000;
    static const std::string passwordSalt = "sdrpp-brown-ftw";

    enum PacketType {
        // Client to Server
        PACKET_TYPE_COMMAND,
        PACKET_TYPE_COMMAND_ACK,
        PACKET_TYPE_BASEBAND,
        PACKET_TYPE_BASEBAND_COMPRESSED,
        PACKET_TYPE_VFO,
        PACKET_TYPE_FFT,
        PACKET_TYPE_ERROR,

        // brown subset
        PACKET_TYPE_BASEBAND_WITH_METADATA = 0x37,
        PACKET_TYPE_TRANSMIT_PROGRESS,     // various indicators of transmitter. 0x38
        PACKET_TYPE_TRANSMIT_DATA,  // 0x39
        PACKET_TYPE_BASEBAND_EXPERIMENTAL_FFT,  // 0x3a
        PACKET_TYPE_WIDE_SPECTRUM,              // 0x3b, channel mode: spectrum of the whole device band (WideSpectrumHeader + one byte per bin)
        PACKET_TYPE_AUDIO,                      // 0x3c, server audio mode: AudioHeader + int16 mono samples
    };

    enum Command {
        // Client to Server
        COMMAND_GET_UI = 0x00,      // also unsolicited, server -> client, after async update.
        COMMAND_UI_ACTION,
        COMMAND_START,
        COMMAND_STOP,
        COMMAND_SET_FREQUENCY,      // also back, when file source changed, for example
        COMMAND_GET_SAMPLERATE,
        COMMAND_SET_SAMPLE_TYPE,
        COMMAND_SET_COMPRESSION,

        // brown subset
        COMMAND_TRANSMIT_ACTION = 0x37,                 // forth and back
        COMMAND_SET_FFTZSTD_COMPRESSION,
        COMMAND_SET_EFFT_LOSS_RATE,
        COMMAND_SET_EFFT_MASKED_FREQUENCIES,        // set the current vfo so efft does not blank it.
        COMMAND_SET_CHANNEL_MODE,                   // 1 byte: 0 = off, 1 = server cuts a narrow IQ channel around the tuned frequency out of the wide device band, 2 = server demodulates it and sends audio
        COMMAND_SET_DEMOD,                          // DemodSettings: modulation (radio module numbering) and bandwidth for server audio mode

        // Server to client, AND client to server. Client sets desired sample rate or 0. Server responds the actual.
        COMMAND_SET_SAMPLERATE = 0x80,

        COMMAND_SET_TRANSMITTER_SUPPORTED = 0xA1,
        COMMAND_SET_TRANSMITTER_NOT_SUPPORTED, // 0xA2,
        COMMAND_EFFT_NOISE_FIGURE,
        COMMAND_SECURE_CHALLENGE,
        COMMAND_DISCONNECT
    };

    enum Error {
        ERROR_NONE = 0x00,
        ERROR_INVALID_PACKET,
        ERROR_INVALID_COMMAND,
        ERROR_INVALID_ARGUMENT
    };
    
#pragma pack(push, 1)
    struct PacketHeader {
        uint32_t type;
        uint32_t size;
    };

    struct WideSpectrumHeader {
        double centerFrequency;
        double span;
        int32_t bins;
        int32_t reserved;
    };

    struct AudioHeader {
        int32_t sampleRate;
        int32_t frames;
    };

    struct DemodSettings {
        int32_t mode;
        double bandwidth;
        double offset;      // the station relative to the device centre frequency
    };

    struct StreamMetadata {
        int32_t version;
        int32_t size;

        double sampleRate;
        double frequency;

        int64_t fftCompressed;
    };

    // generic data packet, to ensure protocol evolution, for non-performance cases.
    struct JsonData {
        int32_t dataSize;
        char data[1024];
    };

    struct StartCommandArguments {
        int32_t magic;
        int32_t clientCapsRequested;
        int32_t txPrebufferMsec;
        char signedChallenge[256 / 8];

    };

    struct CommandHeader {
        uint32_t cmd;
    };
#pragma pack(pop)
}