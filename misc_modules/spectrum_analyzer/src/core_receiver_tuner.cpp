#include "core_receiver_tuner.h"
#include <core.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <utils/flog.h>
#include <algorithm>

namespace spectrum {
    CoreReceiverTuner::CoreReceiverTuner(std::string name, bool mute, bool restore)
        : sourceName(std::move(name)), muteAudio(mute), restoreReceiver(restore) {
        stream.origin = "spectrum_analyzer.generic";
    }

    CoreReceiverTuner::~CoreReceiverTuner() {
        stop();
    }

    bool CoreReceiverTuner::prepareOnUiThread(std::string& error) {
        auto names = sigpath::sourceManager.getSourceNames();
        if (std::find(names.begin(), names.end(), sourceName) == names.end()) {
            error = "Source '" + sourceName + "' is not available";
            return false;
        }
        prevSource = sigpath::sourceManager.getSelectedName();
        prevPlaying = gui::mainWindow.isPlaying();
        if (prevSource != sourceName) {
            if (prevPlaying) { gui::mainWindow.setPlayState(false); }
            sigpath::sourceManager.selectSource(sourceName);
            switchedSource = true;
        }
        if (!gui::mainWindow.isPlaying()) {
            gui::mainWindow.setPlayState(true);
            startedReceiver = true;
        }
        if (muteAudio) { sigpath::sinkManager.setAllMuted(true); }
        sampleRate = sigpath::iqFrontEnd.getEffectiveSamplerate();
        usable = gui::waterfall.getUsableSpectrumRatio();
        prepared = true;
        flog::info("Spectrum Analyzer: generic sweep on '{0}', {1} S/s", sourceName, sampleRate);
        return true;
    }

    void CoreReceiverTuner::finishOnUiThread() {
        if (!prepared) { return; }
        prepared = false;
        if (muteAudio) { sigpath::sinkManager.setAllMuted(false); }
        if (!restoreReceiver) {
            // Keep the receiver running on the swept source, back at its own frequency.
            sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
            return;
        }
        if (switchedSource) {
            if (gui::mainWindow.isPlaying()) { gui::mainWindow.setPlayState(false); }
            if (!prevSource.empty()) { sigpath::sourceManager.selectSource(prevSource); }
            if (prevPlaying) { gui::mainWindow.setPlayState(true); }
        }
        else if (startedReceiver && !prevPlaying) {
            gui::mainWindow.setPlayState(false);
        }
        else if (gui::mainWindow.isPlaying()) {
            // The receiver keeps running: return it to the frequency it was on.
            sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        }
    }

    bool CoreReceiverTuner::start(SampleRing& r, std::string& error) {
        ring = &r;
        stream.clearReadStop();
        stream.clearWriteStop();
        lastSampleNs = nowNs();
        sigpath::iqFrontEnd.bindIQStream(&stream);
        bound = true;
        readerThread = std::thread(&CoreReceiverTuner::reader, this);
        return true;
    }

    void CoreReceiverTuner::reader() {
        while (true) {
            int n = stream.read();
            if (n < 0) { break; }
            if (n > 0) {
                ring->write((const cf32*)stream.readBuf, n);
                lastSampleNs = nowNs();
            }
            stream.flush();
        }
    }

    void CoreReceiverTuner::stop() {
        if (!bound) { return; }
        bound = false;
        sigpath::iqFrontEnd.unbindIQStream(&stream);
        stream.stopReader();
        if (readerThread.joinable()) { readerThread.join(); }
        stream.clearReadStop();
    }

    bool CoreReceiverTuner::tune(double hz) {
        sigpath::sourceManager.tune(hz);
        return true;
    }

    bool CoreReceiverTuner::alive(std::string& detail) {
        if (!gui::mainWindow.isPlaying()) {
            detail = "The receiver was stopped while sweeping";
            return true; // not a device fault
        }
        double idle = (double)(nowNs() - lastSampleNs.load()) * 1e-9;
        if (idle > 2.0) {
            detail = "No IQ samples from " + sourceName + " for " + std::to_string((int)idle) + " s (device disconnected?)";
            return false;
        }
        return true;
    }
}
