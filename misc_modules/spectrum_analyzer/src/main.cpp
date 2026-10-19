// SDR++ by AJA - Spectrum Analyzer ("Looking Glass" style wideband sweep)
//
// Adds a "Spectrum Analyzer" tab next to the receiver. Sweeps are acquired by:
//  - HackRF native sweep (hackrf_source implements sweep::NativeSweepProvider)
//  - Generic stepped sweep through the selected receiver source (any SDR)
//  - Demo mode with synthetic signals (always labelled as such)
#include <config.h>
#include <core.h>
#include <gui/colormaps.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/tuner.h>
#include <imgui.h>
#include <module.h>
#include <signal_path/native_sweep.h>
#include <signal_path/signal_path.h>
#include <utils/flog.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>

#include "core_fft_engine.h"
#include "core_receiver_tuner.h"
#include "engine/band_plan.h"
#include "engine/export.h"
#include "engine/generic_backend.h"
#include "engine/native_backend.h"
#include "engine/sweep_controller.h"
#include "engine/synthetic_tuner.h"
#include "spectrum_view.h"

SDRPP_MOD_INFO{
    /* Name:            */ "spectrum_analyzer",
    /* Description:     */ "Wideband sweep spectrum analyzer (Looking Glass) for SDR++ by AJA",
    /* Author:          */ "AJA Studio",
    /* Version:         */ 1, 0, 0,
    /* Max instances    */ 1
};

using namespace spectrum;

ConfigManager config;

namespace {
    const char* VIEW_NAME = "Spectrum Analyzer";
    const char* MODES_TXT = "Auto\0Native (hardware)\0Generic (stepped)\0";
    const char* UNITS_TXT = "kHz\0MHz\0GHz\0";
    const char* WINDOWS_TXT = "Rectangular\0Hann\0Blackman\0Nuttall\0Blackman-Harris\0";
    const char* DETECTORS_TXT = "Peak\0Average\0";
    const char* DIRECTIONS_TXT = "Up\0Down\0";
    const int FFT_SIZES[] = { 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
    const char* FFT_SIZES_TXT = "256\0" "512\0" "1024\0" "2048\0" "4096\0" "8192\0" "16384\0" "32768\0" "65536\0";
    const int BINS[] = { 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
    const char* BINS_TXT = "1024\0" "2048\0" "4096\0" "8192\0" "16384\0" "32768\0" "65536\0";

    // Initial settling times per source type. These are starting values, not
    // measurements; the user's value is remembered per source.
    double defaultSettleMs(const std::string& src) {
        struct E {
            const char* key;
            double ms;
        };
        static const E table[] = {
            { "RTL-TCP", 150 }, { "RTL-SDR", 60 }, { "AirspyHF", 40 }, { "Airspy", 25 }, { "SDRplay", 60 },
            { "LimeSDR", 30 }, { "USRP", 30 }, { "PlutoSDR", 60 }, { "HackRF", 30 }, { "HydraSDR", 30 },
            { "Soapy", 60 }, { "SpyServer", 250 }, { "SDR++ Server", 250 }, { "KiwiSDR", 400 }, { "Network", 250 },
            { "File", 0 }, { "Hermes", 50 }, { "HL2", 50 },
        };
        for (auto& e : table) {
            if (src.find(e.key) != std::string::npos) { return e.ms; }
        }
        return 60;
    }

    int indexOf(const int* arr, int n, int v, int def) {
        for (int i = 0; i < n; i++) {
            if (arr[i] == v) { return i; }
        }
        return def;
    }

    std::string timestampForFile() {
        time_t t = time(nullptr);
        struct tm tmv;
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[32];
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmv);
        return buf;
    }
}

class SpectrumAnalyzerModule : public ModuleManager::Instance {
public:
    enum class Kind {
        NONE,
        NATIVE,
        GENERIC,
        DEMO
    };

    struct DeviceEntry {
        Kind kind = Kind::NONE;      // NATIVE (native capable), GENERIC (receiver source), DEMO
        std::string key;             // persisted id
        std::string label;
        sweep::NativeSweepProvider* provider = nullptr;
        std::string deviceId;        // provider device id
        std::string sourceName;      // receiver source used for the generic sweep
    };

    SpectrumAnalyzerModule(std::string name) : name(name), engine(coreFftFactory()), controller(engine) {
        loadConfig();
        gui::menu.registerEntry(name, menuHandler, this, this);
        gui::mainWindow.registerMainView(VIEW_NAME, [this]() { drawView(); });
        wfDrawnHandler.handler = [](ImGuiContext* ctx, void* c) { ((SpectrumAnalyzerModule*)c)->tick(); };
        wfDrawnHandler.ctx = this;
        gui::mainWindow.onWaterfallDrawn.bindHandler(&wfDrawnHandler);
        sourceChangedHandler.handler = [](std::string n, void* c) { ((SpectrumAnalyzerModule*)c)->devicesStale = true; };
        sourceChangedHandler.ctx = this;
        sigpath::sourceManager.onSourceRegistered.bindHandler(&sourceChangedHandler);
        sigpath::sourceManager.onSourceUnregistered.bindHandler(&sourceChangedHandler);
    }

    ~SpectrumAnalyzerModule() {
        controller.stopAndWait();
        releasePlayLock();
        sigpath::sourceManager.onSourceRegistered.unbindHandler(&sourceChangedHandler);
        sigpath::sourceManager.onSourceUnregistered.unbindHandler(&sourceChangedHandler);
        gui::mainWindow.onWaterfallDrawn.unbindHandler(&wfDrawnHandler);
        gui::mainWindow.unregisterMainView(VIEW_NAME);
        gui::menu.removeEntry(name);
    }

    void postInit() override {
        refreshDevices();
    }

    void enable() override {
        if (enabled) { return; }
        enabled = true;
        gui::mainWindow.registerMainView(VIEW_NAME, [this]() { drawView(); });
    }

    void disable() override {
        if (!enabled) { return; }
        controller.stopAndWait();
        releasePlayLock();
        gui::mainWindow.unregisterMainView(VIEW_NAME);
        view.releaseGl();
        enabled = false;
    }

    bool isEnabled() override {
        return enabled;
    }

    // Automation (HTTP debug server): runs on the server thread, so actions
    // are queued and executed on the UI thread in tick().
    std::string handleDebugCommand(const std::string& cmd, const std::string& args) override {
        json r;
        if (cmd == "status") {
            std::lock_guard<std::mutex> lck(statusMtx);
            return cachedStatus.empty() ? std::string("{\"state\": \"starting\"}") : cachedStatus;
        }
        if (cmd == "devices") {
            std::lock_guard<std::mutex> lck(devMtx);
            r["devices"] = json::array();
            for (auto& d : devices) { r["devices"].push_back({ { "key", d.key }, { "label", d.label } }); }
            return r.dump();
        }
        static const char* actions[] = { "start", "stop", "pause", "resume", "show", "hide", "select_device", "set_range", "set", "export", "tune", "clear", "refresh" };
        bool known = false;
        for (auto a : actions) {
            if (cmd == a) { known = true; }
        }
        if (!known) {
            r["error"] = "unknown command";
            return r.dump();
        }
        std::lock_guard<std::mutex> lck(queueMtx);
        commandQueue.push_back({ cmd, args });
        r["queued"] = cmd;
        return r.dump();
    }

private:
    // ------------------------------------------------------------ config
    void loadConfig() {
        config.acquire();
        settingsFromJson(config.conf["settings"], settings);
        markers = markersFromJson(config.conf["markers"]);
        userPresets.clear();
        if (config.conf["presets"].is_object()) {
            for (auto& [pname, pj] : config.conf["presets"].items()) {
                Preset p;
                p.name = pname;
                p.settings = settings;
                settingsFromJson(pj, p.settings);
                userPresets.push_back(p);
            }
        }
        selectedDeviceKey = config.conf["device"];
        json& ui = config.conf["ui"];
        if (ui.contains("unit")) { unit = (FreqUnit)std::clamp<int>(ui["unit"].get<int>(), 0, 2); }
        if (ui.contains("muteAudio")) { muteAudio = ui["muteAudio"]; }
        if (ui.contains("restoreReceiver")) { restoreReceiver = ui["restoreReceiver"]; }
        if (ui.contains("resumeReceiver")) { resumeReceiver = ui["resumeReceiver"]; }
        if (ui.contains("exportDir")) { exportDir = ui["exportDir"]; }
        if (ui.contains("recentRanges") && ui["recentRanges"].is_array()) {
            for (auto& r : ui["recentRanges"]) {
                if (r.is_array() && r.size() == 2 && r[0].is_number() && r[1].is_number() && r[1].get<double>() > r[0].get<double>()) {
                    recentRanges.push_back({ r[0].get<double>(), r[1].get<double>() });
                }
            }
        }
        // Layout v2: bigger waterfall and auto waterfall levels by default
        int layoutVersion = ui.contains("layoutVersion") ? ui["layoutVersion"].get<int>() : 0;
        if (layoutVersion < 2) { settings.waterfall.autoLevels = true; }
        // Layout v3: spectrum 40 % / waterfall 60 %
        if (layoutVersion < 3) { settings.display.spectrumHeightFrac = 0.4f; }
        if (config.conf["settleBySource"].is_object()) {
            for (auto& [k, v] : config.conf["settleBySource"].items()) {
                if (v.is_number()) { settleBySource[k] = v.get<double>(); }
            }
        }
        config.release();
        editRange = settings.range;
        applyPalette();
    }

    void saveConfig() {
        config.acquire();
        config.conf["settings"] = settingsToJson(settings);
        config.conf["markers"] = markersToJson(markers);
        json presets = json::object();
        for (auto& p : userPresets) { presets[p.name] = settingsToJson(p.settings); }
        config.conf["presets"] = presets;
        config.conf["device"] = selectedDeviceKey;
        config.conf["ui"]["layoutVersion"] = 3;
        json rr = json::array();
        for (auto& r : recentRanges) { rr.push_back({ r.startHz, r.stopHz }); }
        config.conf["ui"]["recentRanges"] = rr;
        config.conf["ui"]["unit"] = (int)unit;
        config.conf["ui"]["muteAudio"] = muteAudio;
        config.conf["ui"]["restoreReceiver"] = restoreReceiver;
        config.conf["ui"]["resumeReceiver"] = resumeReceiver;
        config.conf["ui"]["exportDir"] = exportDir;
        json sbs = json::object();
        for (auto& [k, v] : settleBySource) { sbs[k] = v; }
        config.conf["settleBySource"] = sbs;
        config.release(true);
    }

    EngineSettings engineSettings() const {
        EngineSettings e;
        e.fftSize = settings.sweep.fftSize;
        e.window = settings.sweep.window;
        e.framesPerCapture = settings.sweep.framesPerCapture;
        e.detector = settings.sweep.detector;
        e.displayBins = settings.sweep.displayBins;
        e.display = settings.display.proc;
        e.waterfallRows = settings.waterfall.historyRows;
        e.sweepsPerLine = settings.waterfall.sweepsPerLine;
        e.peaksEnabled = settings.peaksEnabled;
        e.peaksOnAverage = settings.peaksOnAverage;
        e.peaks = settings.peaks;
        e.refreshLimitHz = settings.sweep.refreshLimitHz;
        return e;
    }

    void applyLiveSettings() {
        engine.updateLiveSettings(engineSettings());
        saveConfig();
    }

    void applyPalette() {
        auto it = colormaps::maps.find(settings.waterfall.palette);
        if (it == colormaps::maps.end()) { it = colormaps::maps.find("Turbo"); }
        if (it != colormaps::maps.end()) { view.setPalette(it->second.map, it->second.entryCount); }
    }

    double settleFor(const std::string& src) {
        auto it = settleBySource.find(src);
        return (it != settleBySource.end()) ? it->second : defaultSettleMs(src);
    }

    // ------------------------------------------------------------ devices
    void refreshDevices() {
        std::vector<DeviceEntry> list;
        std::vector<std::string> nativeSources;
        auto providers = core::moduleManager.getAllInterfaces<sweep::NativeSweepProvider>(sweep::NATIVE_SWEEP_INTERFACE);
        for (auto* p : providers) {
            if (p->apiVersion() != sweep::NATIVE_SWEEP_API_VERSION) { continue; }
            for (auto& d : p->listDevices()) {
                DeviceEntry e;
                e.kind = Kind::NATIVE;
                e.key = "native:" + p->providerName() + ":" + d.id;
                e.label = d.label + "  [native sweep]";
                e.provider = p;
                e.deviceId = d.id;
                e.sourceName = p->receiverSourceName();
                list.push_back(e);
                nativeSources.push_back(e.sourceName);
            }
        }
        for (auto& s : sigpath::sourceManager.getSourceNames()) {
            if (std::find(nativeSources.begin(), nativeSources.end(), s) != nativeSources.end()) { continue; }
            DeviceEntry e;
            e.kind = Kind::GENERIC;
            e.key = "source:" + s;
            e.label = s + "  [stepped sweep]";
            e.sourceName = s;
            list.push_back(e);
        }
        DeviceEntry demo;
        demo.kind = Kind::DEMO;
        demo.key = "demo";
        demo.label = "Demo - synthetic signals (no hardware)";
        list.push_back(demo);

        std::lock_guard<std::mutex> lck(devMtx);
        devices = list;
        devicesTxt.clear();
        for (auto& d : devices) {
            devicesTxt += d.label;
            devicesTxt += '\0';
        }
        deviceIdx = 0;
        for (int i = 0; i < (int)devices.size(); i++) {
            if (devices[i].key == selectedDeviceKey) { deviceIdx = i; }
        }
        devicesStale = false;
        updateCaps();
    }

    const DeviceEntry* currentDevice() {
        if (deviceIdx < 0 || deviceIdx >= (int)devices.size()) { return nullptr; }
        return &devices[deviceIdx];
    }

    void updateCaps() {
        capsValid = false;
        capsError.clear();
        const DeviceEntry* d = currentDevice();
        if (!d || d->kind != Kind::NATIVE || controller.active()) { return; }
        capsValid = d->provider->getCaps(d->deviceId, caps, capsError);
        if (capsValid) {
            for (auto& g : caps.gains) {
                if (!settings.gains.count(g.id)) { settings.gains[g.id] = g.defaultValue; }
            }
        }
    }

    Kind resolveKind(const DeviceEntry& d, std::string& note) {
        note.clear();
        if (d.kind == Kind::DEMO) { return Kind::DEMO; }
        if (d.kind == Kind::GENERIC) {
            if (settings.sweep.mode == SweepMode::NATIVE) { note = "This device has no native sweep; using the generic stepped sweep."; }
            return Kind::GENERIC;
        }
        if (settings.sweep.mode == SweepMode::GENERIC) { return Kind::GENERIC; }
        if (settings.sweep.mode == SweepMode::AUTO && capsValid && caps.firmwareKnown && !caps.nativeSweepSupported) {
            note = caps.supportNote + " Falling back to the generic stepped sweep.";
            return Kind::GENERIC;
        }
        return Kind::NATIVE;
    }

    RangeLimits currentLimits(Kind k) {
        RangeLimits lim;
        lim.minSpanHz = 1e3;
        if (k == Kind::NATIVE && capsValid) {
            lim.minHz = caps.minFreqHz;
            lim.maxHz = caps.maxFreqHz;
        }
        return lim;
    }

    // ------------------------------------------------------------ control
    void startSweep(bool confirmed) {
        notice.clear();
        rangeError.clear();
        controller.update();
        if (controller.active()) { return; }
        if (devicesStale) { refreshDevices(); }
        const DeviceEntry* dp = currentDevice();
        if (!dp) {
            notice = "No device available";
            return;
        }
        DeviceEntry d = *dp;
        std::string note;
        Kind kind = resolveKind(d, note);
        std::string err = validateRange(settings.range, currentLimits(kind));
        if (!err.empty()) {
            rangeError = err;
            return;
        }

        BackendStartParams p;
        p.range = settings.range;
        p.sweep = settings.sweep;
        p.engine = engineSettings();
        p.gains = settings.gains;
        std::unique_ptr<SweepBackend> backend;

        if (kind == Kind::NATIVE) {
            sweep::Ownership own = d.provider->getOwnership(d.deviceId);
            if (own == sweep::Ownership::RECEIVER) {
                if (!confirmed) {
                    openTakeoverPopup = true;
                    return;
                }
                gui::mainWindow.setPlayState(false);
                resumeAfterNative = resumeReceiver;
            }
            backend = std::make_unique<NativeSweepBackend>(d.provider, d.deviceId, d.label);
            runProvider = d.provider;
        }
        else if (kind == Kind::GENERIC) {
            if (gui::mainWindow.isPlaying() && !confirmed) {
                openGenericPopup = true;
                return;
            }
            p.sweep.settleMs = settleFor(d.sourceName);
            auto tuner = std::make_unique<CoreReceiverTuner>(d.sourceName, muteAudio, restoreReceiver);
            backend = std::make_unique<GenericSweepBackend>(std::move(tuner), d.sourceName + " (stepped sweep)");
            runProvider = nullptr;
        }
        else {
            SyntheticTunerConfig c;
            c.sampleRateHz = 10e6;
            c.retuneDelayMs = 2.0;
            c.onDemand = true;
            p.sweep.settleMs = 0.5;
            auto tuner = std::make_unique<SyntheticTuner>(SyntheticSource::demoScene(), c);
            backend = std::make_unique<GenericSweepBackend>(std::move(tuner), "Demo (synthetic)");
            runProvider = nullptr;
        }

        std::string error;
        if (!controller.start(std::move(backend), p, error)) {
            notice = error;
            return;
        }
        runKind = kind;
        runDevice = d;
        runParams = p;
        if (!note.empty()) { notice = note; }
        view.invalidateWaterfall();
        acqDirty = false;
        flog::info("Spectrum Analyzer: sweep started on '{0}' ({1} - {2} Hz)", d.label, settings.range.startHz, settings.range.stopHz);
    }

    void stopSweep() {
        controller.requestStop();
    }

    void restartIfRunning() {
        if (controller.active()) {
            pendingRestart = true;
            controller.requestStop();
        }
    }

    void releasePlayLock() {
        if (weLockedPlay) {
            gui::mainWindow.playButtonLocked = false;
            weLockedPlay = false;
        }
    }

    // Tune the standard receiver to a frequency seen on the analyzer. If the
    // analyzer is using the device, the sweep is stopped first and the device
    // is handed back to the receiver.
    void requestTune(double hz) {
        if (controller.active() && runKind != Kind::DEMO) {
            pendingTune = true;
            pendingTuneHz = hz;
            resumeAfterNative = false;
            controller.requestStop();
            notice = "Stopping the sweep and handing the device to the receiver...";
            return;
        }
        if (controller.active()) { controller.requestStop(); }
        performTune(hz);
    }

    void performTune(double hz) {
        std::string src;
        if (runKind == Kind::NATIVE && runProvider) { src = runProvider->receiverSourceName(); }
        else if (runKind == Kind::GENERIC) { src = runDevice.sourceName; }
        if (!src.empty() && sigpath::sourceManager.getSelectedName() != src) {
            if (gui::mainWindow.isPlaying()) { gui::mainWindow.setPlayState(false); }
            sigpath::sourceManager.selectSource(src);
            core::configManager.acquire();
            core::configManager.conf["source"] = src;
            core::configManager.release(true);
        }
        if (!gui::mainWindow.isPlaying() && runKind != Kind::NONE && runKind != Kind::DEMO) { gui::mainWindow.setPlayState(true); }
        tuner::tune(tuner::TUNER_MODE_NORMAL, gui::waterfall.selectedVFO, hz);
        gui::mainWindow.selectMainView("");
        notice = "Receiver tuned to " + formatFrequency(hz);
        flog::info("Spectrum Analyzer: receiver tuned to {0} Hz", hz);
    }

    void tick() {
        if (lastTickFrame == ImGui::GetFrameCount()) { return; }
        lastTickFrame = ImGui::GetFrameCount();
        controller.update();
        runQueuedCommands();

        // Status for the HTTP debug server, built here on the UI thread.
        int64_t now = nowNs();
        if (now - lastStatusNs > 100000000) {
            lastStatusNs = now;
            std::string st = statusJson().dump();
            std::lock_guard<std::mutex> lck(statusMtx);
            cachedStatus = st;
        }

        bool nativeActive = controller.active() && runKind == Kind::NATIVE && runProvider;
        bool shouldLock = nativeActive && sigpath::sourceManager.getSelectedName() == runProvider->receiverSourceName();
        if (shouldLock && !gui::mainWindow.playButtonLocked) {
            gui::mainWindow.playButtonLocked = true;
            weLockedPlay = true;
        }
        if (!shouldLock) { releasePlayLock(); }

        if (!controller.active()) {
            if (resumeAfterNative && runKind == Kind::NATIVE) {
                resumeAfterNative = false;
                if (!pendingTune && !pendingRestart) { gui::mainWindow.setPlayState(true); }
            }
            if (pendingTune) {
                pendingTune = false;
                performTune(pendingTuneHz);
            }
            if (pendingRestart) {
                pendingRestart = false;
                startSweep(true);
            }
        }
    }

    void runQueuedCommands() {
        std::vector<std::pair<std::string, std::string>> cmds;
        {
            std::lock_guard<std::mutex> lck(queueMtx);
            cmds.swap(commandQueue);
        }
        for (auto& [cmd, args] : cmds) {
            if (cmd == "start") { startSweep(true); }
            else if (cmd == "stop") { stopSweep(); }
            else if (cmd == "pause") { controller.setPaused(true); }
            else if (cmd == "resume") { controller.setPaused(false); }
            else if (cmd == "show") { gui::mainWindow.selectMainView(VIEW_NAME); }
            else if (cmd == "hide") { gui::mainWindow.selectMainView(""); }
            else if (cmd == "refresh") { refreshDevices(); }
            else if (cmd == "clear") { engine.clearWaterfall(); }
            else if (cmd == "tune") {
                try { requestTune(std::stod(args)); } catch (...) {}
            }
            else if (cmd == "select_device") {
                refreshDevices();
                for (int i = 0; i < (int)devices.size(); i++) {
                    if (devices[i].key == args || devices[i].label.find(args) != std::string::npos) {
                        selectDevice(i);
                        break;
                    }
                }
            }
            else if (cmd == "set_range") {
                double a = 0, b = 0;
                if (sscanf(args.c_str(), "%lf,%lf", &a, &b) == 2) {
                    editRange = { a, b };
                    applyRange();
                }
            }
            else if (cmd == "set") {
                try {
                    settingsFromJson(json::parse(args), settings);
                    editRange = settings.range;
                    applyPalette();
                    applyLiveSettings();
                } catch (...) {}
            }
            else if (cmd == "export") { exportSnapshot(); }
        }
    }

    json statusJson() {
        json j;
        EngineStats st = engine.stats();
        j["state"] = sweepStateName(controller.state());
        j["message"] = controller.message();
        j["notice"] = notice;
        j["view_selected"] = gui::mainWindow.getSelectedMainView() == VIEW_NAME;
        j["range"] = { settings.range.startHz, settings.range.stopHz };
        j["run_kind"] = (int)runKind;
        j["device"] = runDevice.label;
        j["sweeps_completed"] = st.sweepsCompleted;
        j["sweeps_incomplete"] = st.sweepsIncomplete;
        j["sweeps_interrupted"] = st.sweepsInterrupted;
        j["sweeps_per_s"] = st.sweepsPerSec;
        j["last_sweep_s"] = st.lastSweepSec;
        j["segments_per_sweep"] = st.segmentsPerSweep;
        j["captures_dropped"] = st.capturesDropped;
        j["rbw_hz"] = st.rbwHz;
        j["display_bin_hz"] = st.displayBinHz;
        j["last_export"] = lastExport;
        EngineSnapshot s;
        engine.snapshot(s, false, false);
        j["noise_floor_db"] = std::isfinite(s.noiseFloorDb) ? json(s.noiseFloorDb) : json(nullptr);
        j["peaks"] = json::array();
        for (size_t i = 0; i < s.peaks.size() && i < 32; i++) {
            j["peaks"].push_back({ { "hz", s.peaks[i].freqHz }, { "db", s.peaks[i].levelDb } });
        }
        j["receiver_playing"] = gui::mainWindow.isPlaying();
        j["receiver_source"] = sigpath::sourceManager.getSelectedName();
        j["receiver_center_hz"] = gui::waterfall.getCenterFrequency();
        j["play_locked"] = gui::mainWindow.playButtonLocked;
        j["view"] = { freqView.viewStart(), freqView.viewStop() };
        j["markers"] = markers.size();
        return j;
    }

    void selectDevice(int i) {
        if (i < 0 || i >= (int)devices.size()) { return; }
        deviceIdx = i;
        selectedDeviceKey = devices[i].key;
        updateCaps();
        saveConfig();
    }

    void applyRange() {
        std::string note;
        const DeviceEntry* d = currentDevice();
        Kind k = d ? resolveKind(*d, note) : Kind::GENERIC;
        rangeError = validateRange(editRange, currentLimits(k));
        if (!rangeError.empty()) { return; }
        settings.range = editRange;
        rememberRange(editRange);
        saveConfig();
        restartIfRunning();
    }

    // ------------------------------------------------------------ export
    MeasurementMetadata metadata() {
        MeasurementMetadata m;
        m.device = runDevice.label;
        m.deviceId = runDevice.kind == Kind::NATIVE ? runDevice.deviceId : runDevice.sourceName;
        m.backend = runKind == Kind::NATIVE ? "HackRF native sweep" : (runKind == Kind::GENERIC ? "Generic stepped sweep" : "Demo (synthetic)");
        m.synthetic = runKind == Kind::DEMO;
        PlanSummary plan = engine.planSummary();
        m.sampleRateHz = plan.sampleRateHz;
        m.fftSize = plan.fftSize;
        m.window = windowName(plan.window);
        m.detector = plan.detector == Detector::PEAK ? "peak" : "average";
        EngineStats st = engine.stats();
        m.rbwHz = st.rbwHz;
        m.fftBinHz = st.fftBinHz;
        m.segments = plan.segments;
        m.stepHz = plan.stepHz;
        m.usableBandwidthHz = plan.usableBandwidthHz;
        std::string g;
        if (runKind == Kind::NATIVE) {
            for (auto& [k, v] : runParams.gains) { g += k + "=" + std::to_string((int)v) + " "; }
        }
        else if (runKind == Kind::GENERIC) {
            g = "receiver source settings";
        }
        m.gains = g;
        m.unixTimeMs = unixTimeMsNow();
        return m;
    }

    void exportSnapshot() {
        EngineSnapshot s;
        if (!engine.snapshot(s, false, false)) {
            lastExport = "Nothing to export: no complete sweep yet.";
            return;
        }
        std::string dir = exportDir.empty() ? std::string(core::getRoot()) + "/spectrum_snapshots" : exportDir;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string base = dir + "/spectrum_" + timestampForFile();
        MeasurementMetadata meta = metadata();
        std::string err;
        std::vector<std::string> written;
        if (writeSpectrumCsv(base + ".csv", meta, s, err)) { written.push_back(base + ".csv"); }
        int w = 0, h = 0;
        std::vector<uint8_t> wf = renderWaterfallImage(engine.waterfall(), view.colorTable(settings.waterfall), w, h);
        if (w > 0 && h > 0 && writePngRgba(base + "_waterfall.png", w, h, wf.data(), err)) { written.push_back(base + "_waterfall.png"); }
        std::vector<uint8_t> sp = renderSpectrumImage(s, settings.display.minDb, settings.display.maxDb, 1600, 500);
        if (writePngRgba(base + "_spectrum.png", 1600, 500, sp.data(), err)) { written.push_back(base + "_spectrum.png"); }
        auto reports = buildSignalReports(s.peaks, markers, meta, s.startHz, s.stopHz, std::max(s.stats.displayBinHz * 2, 1e3));
        json doc = snapshotDocument(meta, s, markers, reports, base + ".csv", base + "_waterfall.png");
        if (writeTextFile(base + ".json", doc.dump(2), err)) { written.push_back(base + ".json"); }
        if (writeTextFile(base + "_signals.json", signalsDocument(reports, meta).dump(2), err)) { written.push_back(base + "_signals.json"); }
        lastExport.clear();
        for (auto& f : written) { lastExport += f + "\n"; }
        if (!err.empty()) { lastExport += "Error: " + err; }
        flog::info("Spectrum Analyzer: exported {0} files to {1}", (int)written.size(), dir);
    }

    // ------------------------------------------------------------ UI
    static void menuHandler(void* ctx) {
        SpectrumAnalyzerModule* _this = (SpectrumAnalyzerModule*)ctx;
        _this->tick();
        bool shown = gui::mainWindow.getSelectedMainView() == VIEW_NAME;
        if (ImGui::Button(shown ? "Back to receiver##sa_menu_open" : "Show Spectrum Analyzer##sa_menu_open", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            gui::mainWindow.selectMainView(shown ? "" : VIEW_NAME);
        }
        _this->drawControls();
        _this->drawSettings();
        _this->drawPopups();
    }

    ImVec4 stateColor(SweepState s) {
        switch (s) {
        case SweepState::SWEEPING: return ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
        case SweepState::PAUSED: return ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
        case SweepState::FAULT:
        case SweepState::DEVICE_DISCONNECTED: return ImVec4(1.0f, 0.35f, 0.3f, 1.0f);
        default: return ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
        }
    }

    bool freqInput(const char* label, double& hz, float width) {
        double v = toUnit(hz, unit);
        ImGui::SetNextItemWidth(width);
        const char* fmt = unit == FreqUnit::KHZ ? "%.3f" : (unit == FreqUnit::MHZ ? "%.6f" : "%.9f");
        if (ImGui::InputDouble(label, &v, 0, 0, fmt)) {
            hz = fromUnit(v, unit);
            return true;
        }
        return false;
    }

    void startStopButtons(float width) {
        bool active = controller.active();
        SweepState state = controller.state();
        float half = (width - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
        if (!active) {
            if (ImGui::Button("Start##sa_start", ImVec2(half, 0))) { startSweep(false); }
        }
        else {
            if (state == SweepState::STOPPING) { style::beginDisabled(); }
            if (ImGui::Button("Stop##sa_stop", ImVec2(half, 0))) { stopSweep(); }
            if (state == SweepState::STOPPING) { style::endDisabled(); }
        }
        ImGui::SameLine();
        if (!active) { style::beginDisabled(); }
        if (ImGui::Button(controller.paused() ? "Resume##sa_pause" : "Pause##sa_pause", ImVec2(half, 0))) { controller.setPaused(!controller.paused()); }
        if (!active) { style::endDisabled(); }
    }

    // Device, mode, start/stop and status, at the top of the menu entry.
    void drawControls() {
        float w = ImGui::GetContentRegionAvail().x;
        bool active = controller.active();
        SweepState state = controller.state();
        {
            std::lock_guard<std::mutex> lck(devMtx);
            if (active) { style::beginDisabled(); }
            ImGui::SetNextItemWidth(w);
            int idx = deviceIdx;
            if (ImGui::Combo("##sa_device", &idx, devicesTxt.c_str())) { selectDevice(idx); }
            ImGui::LeftLabel("Mode");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Refresh").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
            int mode = (int)settings.sweep.mode;
            if (ImGui::Combo("##sa_mode", &mode, MODES_TXT)) {
                settings.sweep.mode = (SweepMode)mode;
                saveConfig();
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh##sa_refresh")) { devicesStale = true; }
            if (active) { style::endDisabled(); }
        }
        if (devicesStale && !active) { refreshDevices(); }
        startStopButtons(w);
        drawPresetCombo(w);

        EngineStats st = engine.stats();
        ImGui::TextColored(stateColor(state), "%s", sweepStateName(state));
        if (state == SweepState::SWEEPING || state == SweepState::PAUSED) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f sweeps/s (measured)", st.sweepsPerSec);
            ImGui::TextDisabled("cycle %.0f ms, %d seg, incomplete %llu, lost %llu", st.lastSweepSec * 1000.0, st.segmentsPerSweep,
                                (unsigned long long)st.sweepsIncomplete, (unsigned long long)st.capturesDropped);
        }
        std::string m = controller.message();
        if (!m.empty() && !active) { ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "%s", m.c_str()); }
        if (!notice.empty()) { ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", notice.c_str()); }
        if (!rangeError.empty()) { ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", rangeError.c_str()); }
    }

    // Frequency value: tap = type it, hold and drag up/down = change it
    // (faster the further the finger moves from where it pressed).
    bool dragFreqField(const char* id, double& hz, float width) {
        float scale = style::uiScale;
        ImGuiIO& io = ImGui::GetIO();
        bool changed = false;
        ImGui::PushID(id);
        if (textEditId == id) {
            double v = toUnit(hz, unit);
            ImGui::SetNextItemWidth(width);
            if (textEditFocus) {
                ImGui::SetKeyboardFocusHere();
                textEditFocus = false;
            }
            const char* fmt = unit == FreqUnit::KHZ ? "%.3f" : (unit == FreqUnit::MHZ ? "%.6f" : "%.9f");
            if (ImGui::InputDouble("##txt", &v, 0, 0, fmt, ImGuiInputTextFlags_AutoSelectAll)) {
                hz = std::max(0.0, fromUnit(v, unit));
                changed = true;
            }
            if (ImGui::IsItemDeactivated()) { textEditId.clear(); }
        }
        else {
            std::string label = formatFrequency(hz, 6);
            ImGui::Button(label.c_str(), ImVec2(width, 0));
            if (ImGui::IsItemActivated()) {
                dragField = id;
                dragPressY = io.MousePos.y;
                dragPressTime = ImGui::GetTime();
                dragValue = hz;
                dragMoved = false;
            }
            // The gesture is tracked by us until the finger lifts, even when it
            // leaves the button (ImGui drops the item's active state then).
            if (dragField == id) {
                if (io.MouseDown[0]) {
                    // Position based: the change depends on how far the finger is from
                    // the press point (up = higher), not on per-frame deltas.
                    float dist = (dragPressY - io.MousePos.y) / scale;
                    if (std::fabs(dist) > 6.0f) { dragMoved = true; }
                    if (dragMoved) {
                        double speed = dragSpeedHzPerPx(dist);
                        double v = std::clamp(dragValue + dragOffsetHz(dist), freqView.navStart(), freqView.navStop());
                        double snapped = dragSnapHz(v, speed);
                        if (snapped != hz) {
                            hz = snapped;
                            changed = true;
                        }
                        double step = std::pow(10.0, std::floor(std::log10(speed)));
                        ImGui::SetTooltip("%s\nstep %s", formatFrequency(hz, 6).c_str(), formatFrequency(step, 0).c_str());
                    }
                }
                else {
                    if (!dragMoved && ImGui::GetTime() - dragPressTime < 0.35) {
                        textEditId = id;
                        textEditFocus = true;
                    }
                    dragField.clear();
                }
            }
        }
        ImGui::PopID();
        return changed;
    }

    void applyRangeAndScan() {
        applyRange();
        if (rangeError.empty() && !controller.active()) { startSweep(false); }
    }

    void rememberRange(const FrequencyRange& r) {
        recentRanges.erase(std::remove_if(recentRanges.begin(), recentRanges.end(), [&](const FrequencyRange& x) {
            return std::fabs(x.startHz - r.startHz) < 1 && std::fabs(x.stopHz - r.stopHz) < 1;
        }), recentRanges.end());
        recentRanges.insert(recentRanges.begin(), r);
        if (recentRanges.size() > 6) { recentRanges.resize(6); }
    }

    void drawRangePopup() {
        float scale = style::uiScale;
        rangePopupOpen = ImGui::BeginPopup("##sa_range_popup");
        if (!rangePopupOpen) { return; }
        float fw = 170.0f * scale;
        ImGui::TextDisabled("Hold and drag up/down to change, tap to type");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Start");
        ImGui::SameLine(60.0f * scale);
        dragFreqField("start", editRange.startHz, fw);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Stop");
        ImGui::SameLine(60.0f * scale);
        dragFreqField("stop", editRange.stopHz, fw);
        if (editRange.stopHz <= editRange.startHz) { ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "Stop must be above start"); }
        else { ImGui::TextDisabled("span %s", formatFrequency(editRange.spanHz(), 3).c_str()); }
        view.previewStartHz = editRange.startHz;
        view.previewStopHz = editRange.stopHz;

        if (ImGui::Button("Scan##sa_rp_scan")) {
            applyRangeAndScan();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Scan visible##sa_rp_vis")) {
            editRange = { std::max(0.0, freqView.viewStart()), freqView.viewStop() };
            applyRangeAndScan();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save preset##sa_rp_save")) {
            Preset p;
            p.settings = settings;
            p.settings.range = editRange;
            p.name = formatFrequency(editRange.startHz, 3) + " - " + formatFrequency(editRange.stopHz, 3);
            auto it = std::find_if(userPresets.begin(), userPresets.end(), [&](const Preset& x) { return x.name == p.name; });
            if (it != userPresets.end()) { *it = p; }
            else { userPresets.push_back(p); }
            saveConfig();
        }
        if (!rangeError.empty()) { ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "%s", rangeError.c_str()); }
        if (!recentRanges.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Recent");
            for (int i = 0; i < (int)recentRanges.size(); i++) {
                std::string l = formatFrequency(recentRanges[i].startHz, 3) + " - " + formatFrequency(recentRanges[i].stopHz, 3) + "##rr" + std::to_string(i);
                if (ImGui::Selectable(l.c_str())) {
                    editRange = recentRanges[i];
                    applyRangeAndScan();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }

    void drawBandsPopup() {
        if (!ImGui::BeginPopup("##sa_bands_popup")) { return; }
        float scale = style::uiScale;
        bool native = capsValid && currentDevice() && currentDevice()->kind == Kind::NATIVE;
        for (auto group : bandGroups()) {
            ImGui::TextDisabled("%s", group);
            float x0 = ImGui::GetCursorPosX();
            float maxX = x0 + 520.0f * scale;
            bool first = true;
            for (auto& b : bandPlan()) {
                if (std::string(b.group) != group) { continue; }
                float bw = ImGui::CalcTextSize(b.name).x + ImGui::GetStyle().FramePadding.x * 2;
                if (!first) {
                    ImGui::SameLine();
                    if (ImGui::GetCursorPosX() + bw > maxX) { ImGui::NewLine(); }
                }
                first = false;
                bool outOfSpec = native && (b.startHz < caps.specMinFreqHz || b.stopHz > caps.specMaxFreqHz);
                if (outOfSpec) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); }
                bool selected = std::fabs(settings.range.startHz - b.startHz) < 1 && std::fabs(settings.range.stopHz - b.stopHz) < 1;
                if (selected) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
                if (ImGui::Button(b.name)) {
                    editRange = { b.startHz, b.stopHz };
                    applyRangeAndScan();
                    freqView.setFullRange(b.startHz, b.stopHz, true);
                    ImGui::CloseCurrentPopup();
                }
                if (selected) { ImGui::PopStyleColor(); }
                if (outOfSpec) {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Outside the range specified for this device"); }
                }
            }
        }
        if (!userPresets.empty()) {
            ImGui::TextDisabled("My presets");
            for (auto& p : userPresets) {
                if (ImGui::Selectable((p.name + "##bp").c_str())) {
                    loadPreset(p, false);
                    if (!controller.active()) { startSweep(false); }
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }

    void drawPresetCombo(float width) {
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##sa_presets", "Presets...")) {
            for (auto& p : builtInPresets(settings)) {
                if (ImGui::Selectable(p.name.c_str())) { loadPreset(p, true); }
            }
            if (!userPresets.empty()) { ImGui::Separator(); }
            for (auto& p : userPresets) {
                if (ImGui::Selectable((p.name + "##user").c_str())) { loadPreset(p, false); }
            }
            ImGui::EndCombo();
        }
    }

    void loadPreset(const Preset& p, bool rangeOnly) {
        if (rangeOnly) {
            settings.range = p.settings.range;
            settings.sweep.displayBins = p.settings.sweep.displayBins;
        }
        else {
            settings = p.settings;
            applyPalette();
        }
        editRange = settings.range;
        engine.updateLiveSettings(engineSettings());
        saveConfig();
        restartIfRunning();
    }

    void drawPlots() {
        float scale = style::uiScale;
        // One slim line: start/stop, status, zoom, tap actions
        SweepState state = controller.state();
        if (!controller.active()) {
            if (ImGui::Button("Start##sa_vstart")) { startSweep(false); }
        }
        else if (ImGui::Button("Stop##sa_vstop")) { stopSweep(); }
        ImGui::SameLine();
        ImGui::TextColored(stateColor(state), "%s", sweepStateName(state));
        ImGui::SameLine();
        {
            EngineStats st = engine.stats();
            // Range chip: tap opens the quick range panel
            std::string chip = formatFrequency(settings.range.startHz, 3) + " - " + formatFrequency(settings.range.stopHz, 3) + "##sa_rchip";
            if (ImGui::Button(chip.c_str())) {
                editRange = settings.range;
                ImGui::OpenPopup("##sa_range_popup");
            }
            if (state == SweepState::SWEEPING) {
                float frac = st.segmentsPerSweep > 0 ? (float)st.segmentsReceived / (float)st.segmentsPerSweep : 0.0f;
                ImGui::SameLine();
                ImGui::TextDisabled("%.1f sw/s", st.sweepsPerSec);
                ImGui::SameLine();
                ImGui::ProgressBar(frac, ImVec2(50.0f * scale, ImGui::GetTextLineHeight() * 0.5f), "");
            }
            drawRangePopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Bands##sa_bands")) { ImGui::OpenPopup("##sa_bands_popup"); }
        drawBandsPopup();
        ImGui::SameLine();
        if (view.selectMode) {
            ImVec4 on = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
            ImGui::PushStyleColor(ImGuiCol_Button, on);
            if (ImGui::Button("Select##sa_selmode")) {
                view.selectMode = false;
                view.clearSelection();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (view.selPhase == 2) {
                // Round to a step matching the zoom (about one pixel)
                double pxHz = freqView.viewSpan() / std::max(ImGui::GetContentRegionAvail().x, 100.0f);
                double a = dragSnapHz(view.selA, pxHz), b = dragSnapHz(view.selB, pxHz);
                char lblBuf[96];
                snprintf(lblBuf, sizeof(lblBuf), "Scan %.6g-%.6g MHz##sa_selscan", a / 1e6, b / 1e6);
                if (ImGui::Button(lblBuf)) {
                    editRange = { a, b };
                    applyRangeAndScan();
                    view.selectMode = false;
                    view.clearSelection();
                }
            }
            else {
                ImGui::TextDisabled(view.selPhase == 1 ? "tap the end" : "drag or tap start");
            }
        }
        else if (ImGui::Button("Select##sa_selmode")) {
            view.selectMode = true;
            view.clearSelection();
        }
        ImGui::SameLine();
        if (ImGui::Button(" - ##sa_zout")) { freqView.zoom(0.5, (freqView.viewStart() + freqView.viewStop()) / 2); }
        ImGui::SameLine();
        if (ImGui::Button(" + ##sa_zin")) { freqView.zoom(2.0, (freqView.viewStart() + freqView.viewStop()) / 2); }
        ImGui::SameLine();
        if (ImGui::Button("Fit##sa_fit")) { freqView.reset(); }
        ImGui::SameLine();
        // Touch friendly actions on the last tapped frequency (no right click on touch screens)
        if (!view.hasTap) { style::beginDisabled(); }
        if (ImGui::Button("Tune##sa_tap_tune")) { requestTune(view.tapHz); }
        ImGui::SameLine();
        if (ImGui::Button("Mark##sa_tap_mark")) {
            markers.push_back({ "", view.tapHz });
            saveConfig();
        }
        if (!view.hasTap) { style::endDisabled(); }
        if (std::isfinite(view.hoverHz)) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", formatFrequency(view.hoverHz).c_str());
        }
        else if (view.hasTap) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", formatFrequency(view.tapHz).c_str());
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float splitterH = 6.0f * scale;
        if (!rangePopupOpen) {
            view.previewStartHz = view.previewStopHz = std::numeric_limits<double>::quiet_NaN();
        }
        float specH = std::floor((avail.y - splitterH) * settings.display.spectrumHeightFrac);
        float wfH = avail.y - splitterH - specH;

        OverlayInfo ov;
        ov.synthetic = runKind == Kind::DEMO && (controller.active() || snap.hasFrame);
        ov.sweeping = controller.state() == SweepState::SWEEPING;
        ov.paused = controller.state() == SweepState::PAUSED;
        ov.displayPaused = settings.waterfall.paused;
        if (controller.state() == SweepState::DEVICE_DISCONNECTED) {
            ov.headline = "DEVICE DISCONNECTED - data below is old";
            ov.headlineColor = IM_COL32(255, 90, 80, 255);
        }

        ViewEvents ev;
        float oldMin = settings.display.minDb, oldMax = settings.display.maxDb;
        view.drawSpectrum(ImVec2(avail.x, specH), snap, settings.display, freqView, markers, ov, ev);
        if (oldMin != settings.display.minDb || oldMax != settings.display.maxDb) { saveConfig(); }

        // Splitter between spectrum and waterfall
        ImVec2 sp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##sa_split", ImVec2(avail.x, splitterH));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            settings.display.spectrumHeightFrac = std::clamp(settings.display.spectrumHeightFrac + ImGui::GetIO().MouseDelta.y / std::max(avail.y, 1.0f), 0.15f, 0.85f);
        }
        if (ImGui::IsItemDeactivated()) { saveConfig(); }
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(sp.x, sp.y + splitterH / 2 - 1), ImVec2(sp.x + avail.x, sp.y + splitterH / 2 + 1), IM_COL32(90, 90, 100, 255));

        if (settings.waterfall.autoLevels && std::isfinite(snap.noiseFloorDb)) {
            // Follow the noise floor in 5 dB steps: noise dark, signals bright.
            float lo = std::floor((snap.noiseFloorDb - 3.0f) / 5.0f) * 5.0f;
            float hi = lo + settings.waterfall.autoRangeDb;
            if (lo != settings.waterfall.minDb || hi != settings.waterfall.maxDb) {
                settings.waterfall.minDb = lo;
                settings.waterfall.maxDb = hi;
            }
        }
        view.drawWaterfall(ImVec2(avail.x, wfH), engine.waterfall(), settings.waterfall, freqView, ov, ev);
        handleEvents(ev);
    }

    void handleEvents(const ViewEvents& ev) {
        if (ev.tune) { requestTune(ev.tuneHz); }
        if (ev.addMarker) {
            markers.push_back({ "", ev.markerHz });
            saveConfig();
        }
        if (ev.removeMarker && ev.markerIndex >= 0 && ev.markerIndex < (int)markers.size()) {
            markers.erase(markers.begin() + ev.markerIndex);
            saveConfig();
        }
        if (ev.setStart) {
            editRange.startHz = ev.rangeHz;
            if (editRange.stopHz <= editRange.startHz) { editRange.stopHz = editRange.startHz + 1e6; }
        }
        if (ev.setStop) {
            editRange.stopHz = ev.rangeHz;
            if (editRange.startHz >= editRange.stopHz) { editRange.startHz = std::max(0.0, editRange.stopHz - 1e6); }
        }
    }

    void drawView() {
        tick();
        bool sweeping = controller.state() == SweepState::SWEEPING;
        bool wantPartial = settings.display.showPartial && sweeping;
        if (engine.version() != snapVersion || wantPartial || snapHadPartial) {
            engine.snapshot(snap, wantPartial, settings.display.showPersistence);
            snapVersion = snap.version;
            snapHadPartial = wantPartial;
        }
        PlanSummary plan = engine.planSummary();
        if (plan.valid && (snap.hasFrame || controller.active())) { freqView.setFullRange(plan.range.startHz, plan.range.stopHz, false); }
        else { freqView.setFullRange(settings.range.startHz, settings.range.stopHz, false); }
        // The view can be moved over the whole device range to pick a new range.
        const DeviceEntry* navDev = currentDevice();
        if (navDev && navDev->kind == Kind::NATIVE && capsValid) { freqView.setNavigationLimits(caps.minFreqHz, caps.maxFreqHz); }
        else { freqView.setNavigationLimits(0, 6e9); }

        // Settings are in the left menu ("Spectrum Analyzer" entry), like every other module.
        ImGui::BeginChild("##sa_plots", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawPlots();
        ImGui::EndChild();
        drawPopups();
    }

    void drawPopups() {
        if (openTakeoverPopup) {
            ImGui::OpenPopup("Device in use##sa_takeover");
            openTakeoverPopup = false;
        }
        if (openGenericPopup) {
            ImGui::OpenPopup("Use the receiver?##sa_generic");
            openGenericPopup = false;
        }
        if (ImGui::BeginPopupModal("Device in use##sa_takeover", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            const DeviceEntry* d = currentDevice();
            ImGui::Text("%s is currently used by the receiver.", d ? d->label.c_str() : "The device");
            ImGui::TextWrapped("A HackRF cannot run a full sweep and continuous reception at the same time. The receiver will be stopped and the device handed to the Spectrum Analyzer.");
            ImGui::Checkbox("Resume the receiver when the sweep stops", &resumeReceiver);
            if (ImGui::Button("Stop receiver and start sweep")) {
                saveConfig();
                ImGui::CloseCurrentPopup();
                startSweep(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Use the receiver?##sa_generic", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            const DeviceEntry* d = currentDevice();
            ImGui::Text("The stepped sweep retunes the receiver source (%s).", d ? d->sourceName.c_str() : "?");
            ImGui::TextWrapped("Reception is interrupted while sweeping. The receiver configuration is restored when the sweep stops.");
            ImGui::Checkbox("Mute audio while sweeping", &muteAudio);
            ImGui::Checkbox("Restore the receiver afterwards", &restoreReceiver);
            if (ImGui::Button("Start sweep")) {
                saveConfig();
                ImGui::CloseCurrentPopup();
                startSweep(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------ settings panel
    // Sub-sections of the menu entry: tree nodes, so they do not look like menu entries.
    bool section(const char* label, bool defaultOpen) {
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (defaultOpen) { f |= ImGuiTreeNodeFlags_DefaultOpen; }
        return ImGui::TreeNodeEx(label, f);
    }

    void drawSettings() {
        float scale = style::uiScale;
        bool active = controller.active();
        std::string note;
        const DeviceEntry* dev = currentDevice();
        Kind kind = dev ? resolveKind(*dev, note) : Kind::GENERIC;
        bool isNative = kind == Kind::NATIVE;
        bool isGenericLike = kind == Kind::GENERIC || kind == Kind::DEMO;

        if (acqDirty && active) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Changed parameters apply after a restart.");
            if (ImGui::Button("Restart sweep now##sa_restart", ImVec2(-1, 0))) { restartIfRunning(); }
        }

        // ---- Frequency range
        if (section("Frequency Range", true)) {
            double center = editRange.centerHz(), span = editRange.spanHz();
            float w = ImGui::GetContentRegionAvail().x - 70.0f * scale;
            ImGui::LeftLabel("Start");
            bool ch = freqInput("##sa_s_start", editRange.startHz, w);
            ImGui::LeftLabel("Stop");
            ch |= freqInput("##sa_s_stop", editRange.stopHz, w);
            ImGui::LeftLabel("Center");
            if (freqInput("##sa_s_center", center, w)) {
                editRange = FrequencyRange::fromCenterSpan(center, span);
                ch = true;
            }
            ImGui::LeftLabel("Span");
            if (freqInput("##sa_s_span", span, w)) {
                editRange = FrequencyRange::fromCenterSpan(center, std::max(span, 1e3));
                ch = true;
            }
            if (ch) { rangeError = validateRange(editRange, currentLimits(kind)); }
            if (isNative && capsValid) {
                ImGui::TextDisabled("Device range %s - %s (specified %s - %s)", formatFrequency(caps.minFreqHz, 0).c_str(), formatFrequency(caps.maxFreqHz, 0).c_str(),
                                    formatFrequency(caps.specMinFreqHz, 0).c_str(), formatFrequency(caps.specMaxFreqHz, 0).c_str());
                if (editRange.startHz < caps.specMinFreqHz || editRange.stopHz > caps.specMaxFreqHz) {
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Outside the specified range: reduced performance.");
                }
                ImGui::TextDisabled("Native sweeps run on a 1 MHz / 20 MHz grid; the display shows the requested range.");
            }
            if (ImGui::Button("Apply range##sa_s_apply", ImVec2(-1, 0))) { applyRange(); }
        }

        // ---- Sweep configuration
        if (section("Sweep Settings", false)) {
            if (active) { ImGui::TextDisabled("Changes apply after restart."); }
            bool ch = false;
            ch |= ImGui::Checkbox("Auto parameters##sa_auto", &settings.sweep.autoParams);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Choose FFT size, overlap and DC guard from the device sample rate and the resolution."); }

            int binsId = indexOf(BINS, 7, settings.sweep.displayBins, 2);
            ImGui::LeftLabel("Resolution (points)");
            ImGui::FillWidth();
            if (ImGui::Combo("##sa_bins", &binsId, BINS_TXT)) {
                settings.sweep.displayBins = BINS[binsId];
                ch = true;
            }
            if (settings.sweep.autoParams) {
                SweepState fs = controller.state();
                if (fs == SweepState::SWEEPING || fs == SweepState::PAUSED) { ImGui::TextDisabled("FFT size: auto (active %d)", engine.planSummary().fftSize); }
                else { ImGui::TextDisabled("FFT size, overlap, step: chosen at start"); }
            }
            else {
                int fftId = indexOf(FFT_SIZES, 9, settings.sweep.fftSize, 3);
                ImGui::LeftLabel("FFT Size");
                ImGui::FillWidth();
                if (ImGui::Combo("##sa_fft", &fftId, FFT_SIZES_TXT)) {
                    settings.sweep.fftSize = FFT_SIZES[fftId];
                    ch = true;
                }
            }
            if (isNative && settings.sweep.fftSize > 4096 && !settings.sweep.autoParams) { ImGui::TextDisabled("HackRF native: FFT limited to 4096 (one 8184-sample block)."); }
            int win = (int)settings.sweep.window;
            ImGui::LeftLabel("Window");
            ImGui::FillWidth();
            if (ImGui::Combo("##sa_win", &win, WINDOWS_TXT)) {
                settings.sweep.window = (WindowType)win;
                ch = true;
            }
            int det = (int)settings.sweep.detector;
            ImGui::LeftLabel("Detector");
            ImGui::FillWidth();
            if (ImGui::Combo("##sa_det", &det, DETECTORS_TXT)) {
                settings.sweep.detector = (Detector)det;
                ch = true;
            }
            ImGui::LeftLabel("FFT averaging");
            ImGui::FillWidth();
            ch |= ImGui::SliderInt("##sa_frames", &settings.sweep.framesPerCapture, 1, 32);

            if (isNative) {
                ImGui::TextDisabled("Sample rate 20 MS/s, 20 MHz interleaved steps (fixed by the HackRF sweep mode)");
                ImGui::LeftLabel("Dwell (blocks/step)");
                ImGui::FillWidth();
                ch |= ImGui::SliderInt("##sa_dwell_blocks", &settings.sweep.nativeDwellBlocks, 1, capsValid ? caps.maxDwellBlocks : 16);
                ImGui::TextDisabled("Direction: up only. Settling is handled by the firmware.");
            }
            if (isGenericLike) {
                if (kind == Kind::GENERIC && dev && sigpath::sourceManager.getSelectedName() == dev->sourceName) {
                    ImGui::TextDisabled("Source sample rate: %s", formatFrequency(sigpath::iqFrontEnd.getEffectiveSamplerate(), 3).c_str());
                }
                if (!settings.sweep.autoParams) {
                    float usable = (float)settings.sweep.usableFraction * 100.0f;
                    ImGui::LeftLabel("Usable bandwidth %");
                    ImGui::FillWidth();
                    if (ImGui::SliderFloat("##sa_usable", &usable, 10.0f, 100.0f, "%.0f")) {
                        settings.sweep.usableFraction = usable / 100.0f;
                        ch = true;
                    }
                    float ov = (float)settings.sweep.overlapFraction * 100.0f;
                    ImGui::LeftLabel("Overlap %");
                    ImGui::FillWidth();
                    if (ImGui::SliderFloat("##sa_overlap", &ov, 0.0f, 90.0f, "%.0f")) {
                        settings.sweep.overlapFraction = ov / 100.0f;
                        ch = true;
                    }
                    double stepK = settings.sweep.stepHz / 1e3;
                    ImGui::LeftLabel("Frequency step (kHz, 0=auto)");
                    ImGui::FillWidth();
                    if (ImGui::InputDouble("##sa_step", &stepK, 0, 0, "%.1f")) {
                        settings.sweep.stepHz = std::max(0.0, stepK * 1e3);
                        ch = true;
                    }
                }
                float dwell = (float)settings.sweep.dwellMs;
                ImGui::LeftLabel("Dwell time (ms, 0=min)");
                ImGui::FillWidth();
                if (ImGui::SliderFloat("##sa_dwell", &dwell, 0.0f, 500.0f, "%.0f")) {
                    settings.sweep.dwellMs = dwell;
                    ch = true;
                }
                if (kind == Kind::GENERIC && dev) {
                    float settle = (float)settleFor(dev->sourceName);
                    ImGui::LeftLabel("Settling time (ms)");
                    ImGui::FillWidth();
                    if (ImGui::SliderFloat("##sa_settle", &settle, 0.0f, 1000.0f, "%.0f")) {
                        settleBySource[dev->sourceName] = settle;
                        ch = true;
                    }
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Samples received during this time after each retune are discarded.\nThe default depends on the source type and is a starting value, not a measurement."); }
                }
                int dir = (int)settings.sweep.direction;
                ImGui::LeftLabel("Direction");
                ImGui::FillWidth();
                if (ImGui::Combo("##sa_dir", &dir, DIRECTIONS_TXT)) {
                    settings.sweep.direction = (SweepDirection)dir;
                    ch = true;
                }
            }
            float rl = (float)settings.sweep.refreshLimitHz;
            ImGui::LeftLabel("Max sweeps/s (0=free)");
            ImGui::FillWidth();
            if (ImGui::SliderFloat("##sa_refresh", &rl, 0.0f, 50.0f, "%.1f")) {
                settings.sweep.refreshLimitHz = rl;
                engine.updateLiveSettings(engineSettings());
                saveConfig();
            }
            if (ch) {
                clampSettings(settings);
                acqDirty = true;
                saveConfig();
            }
            SweepState runState = controller.state();
            if (runState == SweepState::SWEEPING || runState == SweepState::PAUSED) {
                PlanSummary plan = engine.planSummary();
                EngineStats st = engine.stats();
                ImGui::Separator();
                ImGui::TextDisabled("Active plan: %d segments, step %s", plan.segments, formatFrequency(plan.stepHz, 3).c_str());
                ImGui::TextDisabled("FFT %d, bin %s, RBW %s", plan.fftSize, formatFrequency(st.fftBinHz, 1).c_str(), formatFrequency(st.rbwHz, 1).c_str());
                ImGui::TextDisabled("Display bin %s", formatFrequency(st.displayBinHz, 1).c_str());
                if (!plan.dcCoveredByOverlap) { ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "DC regions not covered by overlap"); }
                for (auto& wmsg : plan.warnings) { ImGui::TextWrapped("%s", wmsg.c_str()); }
                if (controller.backend()) { ImGui::TextWrapped("%s", controller.backend()->statusLine().c_str()); }
            }
        }

        // ---- Gain
        if (section("Gain", false)) {
            if (isNative && capsValid) {
                for (auto& g : caps.gains) {
                    double& v = settings.gains[g.id];
                    bool changed = false;
                    if (g.isToggle) {
                        bool b = v >= 0.5;
                        if (ImGui::Checkbox((g.label + "##sa_g_" + g.id).c_str(), &b)) {
                            v = b ? 1.0 : 0.0;
                            changed = true;
                        }
                    }
                    else {
                        int iv = (int)std::lround(v);
                        ImGui::LeftLabel(g.label.c_str());
                        ImGui::FillWidth();
                        if (ImGui::SliderInt(("##sa_g_" + g.id).c_str(), &iv, (int)g.minValue, (int)g.maxValue, "%d dB")) {
                            int step = std::max(1, (int)g.step);
                            iv = (int)std::lround((double)iv / step) * step;
                            v = iv;
                            changed = true;
                        }
                    }
                    if (!g.warning.empty() && ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", g.warning.c_str()); }
                    if (changed) {
                        if (controller.active() && runKind == Kind::NATIVE && controller.backend()) { controller.backend()->setGain(g.id, v); }
                        saveConfig();
                    }
                }
                ImGui::TextDisabled("No AGC in HackRF sweep mode.");
            }
            else if (kind == Kind::GENERIC && dev) {
                ImGui::TextWrapped("The stepped sweep uses the receiver source settings (gain, AGC, bias-T...) of %s:", dev->sourceName.c_str());
                if (sigpath::sourceManager.getSelectedName() == dev->sourceName) { sigpath::sourceManager.showSelectedMenu(); }
                else { ImGui::TextDisabled("(shown once %s is the selected receiver source)", dev->sourceName.c_str()); }
            }
            else if (kind == Kind::DEMO) {
                ImGui::TextDisabled("Synthetic source - no gain controls.");
            }
            else {
                ImGui::TextDisabled("Gain controls unavailable: %s", capsError.c_str());
            }
        }

        // ---- Display
        if (section("Spectrum Display", false)) {
            DisplayConfig& d = settings.display;
            bool ch = false, live = false;
            ImGui::LeftLabel("Max dB");
            ImGui::FillWidth();
            ch |= ImGui::SliderFloat("##sa_maxdb", &d.maxDb, -150.0f, 30.0f, "%.0f");
            ImGui::LeftLabel("Min dB");
            ImGui::FillWidth();
            ch |= ImGui::SliderFloat("##sa_mindb", &d.minDb, -200.0f, 0.0f, "%.0f");
            if (d.maxDb < d.minDb + 10) { d.maxDb = d.minDb + 10; }
            ch |= ImGui::Checkbox("Auto scale##sa_as", &d.autoScale);
            ImGui::SameLine();
            ch |= ImGui::Checkbox("Grid##sa_grid", &d.showGrid);
            ch |= ImGui::Checkbox("Current##sa_cur", &d.showCurrent);
            ImGui::SameLine();
            ch |= ImGui::Checkbox("Average##sa_avg", &d.showAverage);
            ImGui::SameLine();
            ch |= ImGui::Checkbox("Max hold##sa_mh", &d.showMaxHold);
            ch |= ImGui::Checkbox("Persistence##sa_pers", &d.showPersistence);
            ImGui::SameLine();
            ch |= ImGui::Checkbox("Noise floor##sa_nf", &d.showNoiseFloor);
            ch |= ImGui::Checkbox("Sweep in progress##sa_part", &d.showPartial);
            ImGui::SameLine();
            ch |= ImGui::Checkbox("Peaks##sa_pk", &d.showPeaks);
            ImGui::SameLine();
            ch |= ImGui::Checkbox("Markers##sa_mk", &d.showMarkers);
            ImGui::LeftLabel("Averaging (sweeps)");
            ImGui::FillWidth();
            live |= ImGui::SliderInt("##sa_avgn", &d.proc.averageCount, 1, 100);
            ImGui::LeftLabel("Peak decay (dB/s, 0=hold)");
            ImGui::FillWidth();
            live |= ImGui::SliderFloat("##sa_decay", &d.proc.peakDecayDbPerSec, 0.0f, 60.0f, "%.1f");
            ImGui::LeftLabel("Persistence (sweeps)");
            ImGui::FillWidth();
            live |= ImGui::SliderInt("##sa_persn", &d.proc.persistence, 0, 32);
            if (ImGui::Button("Reset average##sa_ravg")) { engine.resetAverage(); }
            ImGui::SameLine();
            if (ImGui::Button("Reset max##sa_rmax")) { engine.resetMaxHold(); }

            if (live) { applyLiveSettings(); }
            else if (ch) { saveConfig(); }
        }

        // ---- Waterfall
        if (section("Waterfall", false)) {
            WaterfallConfig& w = settings.waterfall;
            bool ch = false;
            ImGui::LeftLabel("Palette");
            ImGui::FillWidth();
            if (ImGui::BeginCombo("##sa_pal", w.palette.c_str())) {
                for (auto& [pname, pmap] : colormaps::maps) {
                    if (ImGui::Selectable(pname.c_str(), pname == w.palette)) {
                        w.palette = pname;
                        applyPalette();
                        ch = true;
                    }
                }
                ImGui::EndCombo();
            }
            ch |= ImGui::Checkbox("Auto levels (follow noise floor)##sa_wauto", &w.autoLevels);
            if (w.autoLevels) {
                ImGui::LeftLabel("Dynamic range (dB)");
                ImGui::FillWidth();
                ch |= ImGui::SliderFloat("##sa_wrange", &w.autoRangeDb, 20.0f, 100.0f, "%.0f");
            }
            else {
                ImGui::LeftLabel("Max dB");
                ImGui::FillWidth();
                ch |= ImGui::SliderFloat("##sa_wmax", &w.maxDb, -150.0f, 30.0f, "%.0f");
                ImGui::LeftLabel("Min dB");
                ImGui::FillWidth();
                ch |= ImGui::SliderFloat("##sa_wmin", &w.minDb, -200.0f, 0.0f, "%.0f");
            }
            if (w.maxDb < w.minDb + 5) { w.maxDb = w.minDb + 5; }
            ImGui::LeftLabel("Brightness");
            ImGui::FillWidth();
            ch |= ImGui::SliderFloat("##sa_bright", &w.brightness, -1.0f, 1.0f, "%.2f");
            ImGui::LeftLabel("Contrast");
            ImGui::FillWidth();
            ch |= ImGui::SliderFloat("##sa_contrast", &w.contrast, 0.2f, 5.0f, "%.2f");
            if (ImGui::Button("Match spectrum scale##sa_wmatch")) {
                w.minDb = settings.display.minDb;
                w.maxDb = settings.display.maxDb;
                ch = true;
            }
            ImGui::LeftLabel("Speed (sweeps/line)");
            ImGui::FillWidth();
            if (ImGui::SliderInt("##sa_spl", &w.sweepsPerLine, 1, 20)) { applyLiveSettings(); }
            ImGui::LeftLabel("History (lines)");
            ImGui::FillWidth();
            if (ImGui::SliderInt("##sa_hist", &w.historyRows, 16, 2048)) { ch = true; }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                engine.resizeWaterfall(w.historyRows);
                view.invalidateWaterfall();
            }
            ImGui::Checkbox("Freeze display##sa_wpause", &w.paused);
            ImGui::SameLine();
            if (ImGui::Button("Clear history##sa_wclear")) {
                engine.clearWaterfall();
                view.invalidateWaterfall();
            }
            {
                auto lck = engine.waterfall().lock();
                ImGui::TextDisabled("%d x %d lines, %.1f MB", engine.waterfall().width(), engine.waterfall().capacity(), engine.waterfall().memoryBytes() / 1048576.0);
            }
            ImGui::TextDisabled("Each line is one or more complete sweeps;\nred edge = incomplete sweep.");
            if (ch) { saveConfig(); }
        }

        // ---- Markers
        if (section("Markers", false)) {
            static double newMarkerHz = 100e6;
            static char newMarkerName[64] = "";
            freqInput("##sa_mk_f", newMarkerHz, 120.0f * scale);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f * scale);
            ImGui::InputText("##sa_mk_n", newMarkerName, sizeof(newMarkerName));
            ImGui::SameLine();
            if (ImGui::Button("Add##sa_mk_add")) {
                markers.push_back({ newMarkerName, newMarkerHz });
                newMarkerName[0] = 0;
                saveConfig();
            }
            if (ImGui::Button("Add markers at detected peaks##sa_mk_peaks")) {
                for (auto& p : snap.peaks) {
                    if (nearestMarker(markers, p.freqHz, std::max(snap.stats.displayBinHz * 2, 1e3)) < 0) { markers.push_back({ "", p.freqHz }); }
                }
                saveConfig();
            }
            int remove = -1;
            if (ImGui::BeginTable("##sa_mk_table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                for (int i = 0; i < (int)markers.size(); i++) {
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char nb[64];
                    snprintf(nb, sizeof(nb), "%s", markers[i].name.c_str());
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("##n", nb, sizeof(nb))) {
                        markers[i].name = nb;
                        saveConfig();
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(formatFrequency(markers[i].freqHz, 4).c_str());
                    ImGui::TableSetColumnIndex(2);
                    float lvl = traceValueAt(snap.current, snap.startHz, snap.stopHz, markers[i].freqHz);
                    if (std::isfinite(lvl)) { ImGui::Text("%.1f dB", lvl); }
                    else { ImGui::TextDisabled("-"); }
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton("Tune")) { requestTune(markers[i].freqHz); }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) { remove = i; }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (remove >= 0) {
                markers.erase(markers.begin() + remove);
                saveConfig();
            }
        }

        // ---- Peaks / signals
        if (section("Detected Signals", false)) {
            bool live = false;
            live |= ImGui::Checkbox("Detect peaks##sa_pd", &settings.peaksEnabled);
            ImGui::SameLine();
            live |= ImGui::Checkbox("On average trace##sa_pda", &settings.peaksOnAverage);
            ImGui::LeftLabel("Min level (dB)");
            ImGui::FillWidth();
            live |= ImGui::SliderFloat("##sa_pmin", &settings.peaks.minLevelDb, -160.0f, 0.0f, "%.0f");
            ImGui::LeftLabel("Above noise (dB)");
            ImGui::FillWidth();
            live |= ImGui::SliderFloat("##sa_pnf", &settings.peaks.aboveNoiseDb, 0.0f, 60.0f, "%.0f");
            float spacingK = (float)(settings.peaks.minSpacingHz / 1e3);
            ImGui::LeftLabel("Min spacing (kHz)");
            ImGui::FillWidth();
            if (ImGui::SliderFloat("##sa_psp", &spacingK, 0.0f, 2000.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
                settings.peaks.minSpacingHz = spacingK * 1e3;
                live = true;
            }
            float widthK = (float)(settings.peaks.minWidthHz / 1e3);
            ImGui::LeftLabel("Min width (kHz)");
            ImGui::FillWidth();
            if (ImGui::SliderFloat("##sa_pw", &widthK, 0.0f, 500.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) {
                settings.peaks.minWidthHz = widthK * 1e3;
                live = true;
            }
            if (live) { applyLiveSettings(); }
            if (ImGui::BeginTable("##sa_peaks", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0, 200.0f * scale))) {
                ImGui::TableSetupColumn("Frequency");
                ImGui::TableSetupColumn("Level");
                ImGui::TableSetupColumn("Width");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (int i = 0; i < (int)snap.peaks.size(); i++) {
                    const DetectedPeak& p = snap.peaks[i];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(formatFrequency(p.freqHz, 4).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.1f (+%.0f)", p.levelDb, p.aboveNoiseDb);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(formatFrequency(p.widthHz, 1).c_str());
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton("Tune")) { requestTune(p.freqHz); }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+M")) {
                        markers.push_back({ "", p.freqHz });
                        saveConfig();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        // ---- Presets
        if (section("Presets", false)) {
            static char presetName[64] = "Custom";
            drawPresetCombo(-1);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f * scale);
            ImGui::InputText("##sa_pname", presetName, sizeof(presetName));
            ImGui::SameLine();
            if (ImGui::Button("Save##sa_psave", ImVec2(-1, 0)) && presetName[0]) {
                Preset p;
                p.name = presetName;
                p.settings = settings;
                auto it = std::find_if(userPresets.begin(), userPresets.end(), [&](const Preset& x) { return x.name == p.name; });
                if (it != userPresets.end()) { *it = p; }
                else { userPresets.push_back(p); }
                saveConfig();
            }
            int del = -1;
            for (int i = 0; i < (int)userPresets.size(); i++) {
                ImGui::PushID(i);
                ImGui::TextUnformatted(userPresets[i].name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s - %s", formatFrequency(userPresets[i].settings.range.startHz, 3).c_str(), formatFrequency(userPresets[i].settings.range.stopHz, 3).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Load")) { loadPreset(userPresets[i], false); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) { del = i; }
                ImGui::PopID();
            }
            if (del >= 0) {
                userPresets.erase(userPresets.begin() + del);
                saveConfig();
            }
            ImGui::TextDisabled("Presets store range, sweep, display and gain settings.");
        }

        // ---- Export
        if (section("Export", false)) {
            char dirBuf[512];
            snprintf(dirBuf, sizeof(dirBuf), "%s", exportDir.c_str());
            ImGui::LeftLabel("Folder");
            ImGui::FillWidth();
            if (ImGui::InputTextWithHint("##sa_expdir", "<root>/spectrum_snapshots", dirBuf, sizeof(dirBuf))) {
                exportDir = dirBuf;
                saveConfig();
            }
            if (ImGui::Button("Save snapshot (CSV, JSON, PNG)##sa_exp", ImVec2(-1, 0))) { exportSnapshot(); }
            ImGui::TextDisabled("CSV holds the measured bins of the sweep grid.\n*_signals.json: detected signals (SDR Patrol format).");
            if (!lastExport.empty()) { ImGui::TextWrapped("%s", lastExport.c_str()); }
        }

        // ---- Device
        if (section("Device & Ownership", false)) {
            if (dev) {
                ImGui::TextWrapped("%s", dev->label.c_str());
                if (dev->kind == Kind::NATIVE) {
                    if (nowNs() - ownershipCheckNs > 1000000000) {
                        ownershipCheckNs = nowNs();
                        sweep::Ownership own = dev->provider->getOwnership(dev->deviceId);
                        ownershipText = own == sweep::Ownership::FREE ? "free" : (own == sweep::Ownership::RECEIVER ? "used by the receiver" : (own == sweep::Ownership::SWEEP ? "used by the analyzer" : "unavailable"));
                    }
                    ImGui::Text("Device: %s", ownershipText.c_str());
                    if (capsValid) {
                        ImGui::TextDisabled("libhackrf %s", caps.libraryVersion.c_str());
                        if (caps.firmwareKnown) { ImGui::TextDisabled("Firmware %s, board %s", caps.firmwareVersion.c_str(), caps.boardName.c_str()); }
                        ImGui::TextDisabled("Native sweep: %s", caps.firmwareKnown ? (caps.nativeSweepSupported ? "supported" : "NOT supported") : "not checked yet");
                        if (!caps.supportNote.empty()) { ImGui::TextWrapped("%s", caps.supportNote.c_str()); }
                    }
                    else if (!capsError.empty()) {
                        ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", capsError.c_str());
                    }
                    if (!active && ImGui::Button("Check device##sa_caps")) { updateCaps(); }
                }
                else if (dev->kind == Kind::GENERIC) {
                    ImGui::TextWrapped("Native sweep is not available for this source; the stepped sweep retunes the receiver segment by segment.");
                    ImGui::TextDisabled("Receiver: %s, %s", sigpath::sourceManager.getSelectedName().c_str(), gui::mainWindow.isPlaying() ? "running" : "stopped");
                }
                else {
                    ImGui::TextWrapped("Synthetic signals for demonstration and testing. Nothing shown in this mode is a measurement.");
                }
            }
            bool optCh = ImGui::Checkbox("Mute audio during stepped sweeps##sa_mute", &muteAudio);
            optCh |= ImGui::Checkbox("Restore receiver after stepped sweeps##sa_restore", &restoreReceiver);
            optCh |= ImGui::Checkbox("Resume receiver after HackRF sweeps##sa_resume", &resumeReceiver);
            if (optCh) { saveConfig(); }
        }
    }

    std::string name;
    bool enabled = true;
    SweepEngine engine;
    SweepController controller;
    SpectrumView view;
    FrequencyView freqView;
    AnalyzerSettings settings;
    FrequencyRange editRange;
    std::vector<Marker> markers;
    std::vector<Preset> userPresets;
    std::map<std::string, double> settleBySource;

    std::mutex devMtx;
    std::vector<DeviceEntry> devices;
    std::string devicesTxt;
    int deviceIdx = 0;
    std::string selectedDeviceKey;
    bool devicesStale = true;
    sweep::NativeSweepCaps caps;
    bool capsValid = false;
    std::string capsError;

    FreqUnit unit = FreqUnit::MHZ;
    bool muteAudio = true;
    bool restoreReceiver = true;
    bool resumeReceiver = true;
    std::string exportDir;
    std::string lastExport;
    std::string notice;
    std::string rangeError;
    bool acqDirty = false;
    std::vector<FrequencyRange> recentRanges;
    bool rangePopupOpen = false;
    std::string textEditId;
    bool textEditFocus = false;
    float dragPressY = 0;
    double dragPressTime = 0;
    double dragValue = 0;
    bool dragMoved = false;
    std::string dragField;

    Kind runKind = Kind::NONE;
    DeviceEntry runDevice;
    BackendStartParams runParams;
    sweep::NativeSweepProvider* runProvider = nullptr;
    bool weLockedPlay = false;
    bool resumeAfterNative = false;
    bool pendingTune = false;
    double pendingTuneHz = 0;
    bool pendingRestart = false;
    bool openTakeoverPopup = false;
    bool openGenericPopup = false;
    int lastTickFrame = -1;

    EngineSnapshot snap;
    uint64_t snapVersion = ~0ull;
    bool snapHadPartial = false;

    std::mutex queueMtx;
    std::vector<std::pair<std::string, std::string>> commandQueue;
    std::mutex statusMtx;
    std::string cachedStatus;
    int64_t lastStatusNs = 0;
    std::string ownershipText;
    int64_t ownershipCheckNs = 0;

    EventHandler<ImGuiContext*> wfDrawnHandler;
    EventHandler<std::string> sourceChangedHandler;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["settings"] = settingsToJson(AnalyzerSettings());
    def["markers"] = json::array();
    def["presets"] = json::object();
    def["device"] = "";
    def["ui"] = json::object();
    def["settleBySource"] = json::object();
    config.setPath(std::string(core::getRoot()) + "/spectrum_analyzer_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new SpectrumAnalyzerModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (SpectrumAnalyzerModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
