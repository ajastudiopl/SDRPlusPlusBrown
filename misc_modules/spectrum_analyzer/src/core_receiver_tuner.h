#pragma once
#include "engine/generic_backend.h"
#include <dsp/stream.h>
#include <dsp/types.h>
#include <thread>

namespace spectrum {
    // Generic stepped sweep through the regular SDR++ receiver path: the
    // selected source is retuned with SourceManager::tune() and IQ samples are
    // taken from the IQ front end (after decimation / DC blocking), exactly as
    // the receiver sees them. Works with every source module, including
    // RTL-SDR, Airspy, SDRplay, LimeSDR, USRP, SoapySDR, PlutoSDR, HackRF...
    //
    // UI-thread hooks switch the receiver to the requested source, start it
    // if needed, mute audio during the sweep and restore everything afterwards.
    class CoreReceiverTuner : public IqTuner {
    public:
        CoreReceiverTuner(std::string sourceName, bool muteAudio, bool restoreReceiver);
        ~CoreReceiverTuner();

        std::string name() override { return sourceName; }
        bool prepareOnUiThread(std::string& error) override;
        void finishOnUiThread() override;
        bool start(SampleRing& ring, std::string& error) override;
        void stop() override;
        double sampleRateHz() override { return sampleRate; }
        double usableFraction() override { return usable; }
        bool tune(double hz) override;
        bool alive(std::string& detail) override;

    private:
        void reader();

        std::string sourceName;
        bool muteAudio;
        bool restoreReceiver;
        dsp::stream<dsp::complex_t> stream;
        SampleRing* ring = nullptr;
        std::thread readerThread;
        std::atomic<bool> bound{ false };
        std::atomic<int64_t> lastSampleNs{ 0 };
        double sampleRate = 0;
        double usable = 0;

        // receiver state to restore
        bool prepared = false;
        std::string prevSource;
        bool prevPlaying = false;
        bool switchedSource = false;
        bool startedReceiver = false;
    };
}
