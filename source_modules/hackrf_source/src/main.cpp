#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/widgets/stepped_slider.h>
#include <gui/smgui.h>
#include <signal_path/native_sweep.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

#ifndef __ANDROID__
#include <libhackrf/hackrf.h>
#else
#include <android_backend.h>
#include <hackrf.h>
#include <libusb.h>
#endif

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "hackrf_source",
    /* Description:     */ "HackRF source module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

const char* AGG_MODES_STR = "Off\0Low\0High\0";

const char* sampleRatesTxt = "20MHz\00016MHz\00010MHz\0008MHz\0005MHz\0004MHz\0002MHz\000";

const int sampleRates[] = {
    20000000,
    16000000,
    10000000,
    8000000,
    5000000,
    4000000,
    2000000,
};

#ifdef __ANDROID__
// Above this the phone's CPU and USB link cannot keep up: the receiver stalls and the audio breaks up
const int ANDROID_MAX_SAMPLE_RATE = 8000000;
#endif

const int bandwidths[] = {
    1750000,
    2500000,
    3500000,
    5000000,
    5500000,
    6000000,
    7000000,
    8000000,
    9000000,
    10000000,
    12000000,
    14000000,
    15000000,
    20000000,
    24000000,
    28000000,
};

const char* bandwidthsTxt = "1.75MHz\0"
                            "2.5MHz\0"
                            "3.5MHz\0"
                            "5MHz\0"
                            "5.5MHz\0"
                            "6MHz\0"
                            "7MHz\0"
                            "8MHz\0"
                            "9MHz\0"
                            "10MHz\0"
                            "12MHz\0"
                            "14MHz\0"
                            "15MHz\0"
                            "20MHz\0"
                            "24MHz\0"
                            "28MHz\0"
                            "Auto\0";

class HackRFSourceModule : public ModuleManager::Instance, public sweep::NativeSweepProvider {
public:
    HackRFSourceModule(std::string name) {
        this->name = name;

#ifdef __ANDROID__
        int usbErr = libusb_set_option(NULL, LIBUSB_OPTION_NO_DEVICE_DISCOVERY);
        if (usbErr != LIBUSB_SUCCESS) {
            hackrfError = HACKRF_ERROR_LIBUSB;
            flog::error("Could not disable HackRF USB discovery: {0}", libusb_error_name(usbErr));
        }
        else {
            hackrfError = (hackrf_error)hackrf_init();
        }
#else
        hackrfError = (hackrf_error)hackrf_init();
#endif
        if (hackrfError != HACKRF_SUCCESS) {
            flog::error("Could not initialize HackRF: {0}", hackrf_error_name(hackrfError));
        }

        // Select the last samplerate option
        sampleRate = 2000000;
        srId = 6;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        refresh();

        config.acquire();
        std::string confSerial = config.conf["device"];
        config.release();
        selectBySerial(confSerial);

        sigpath::sourceManager.registerSource("HackRF", &handler);
    }

    ~HackRFSourceModule() {
        stopSweep();
        stop(this);
        if (hackrfError == HACKRF_SUCCESS) { hackrf_exit(); }
        sigpath::sourceManager.unregisterSource("HackRF");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    void* getInterface(const char* name) {
        if (!strcmp(name, sweep::NATIVE_SWEEP_INTERFACE)) { return (sweep::NativeSweepProvider*)this; }
        return nullptr;
    }

    // ---- sweep::NativeSweepProvider (used by the Spectrum Analyzer) ----

    int apiVersion() const { return sweep::NATIVE_SWEEP_API_VERSION; }

    std::string providerName() { return "HackRF"; }

    std::string receiverSourceName() { return "HackRF"; }

    std::vector<sweep::NativeSweepDevice> listDevices() {
        std::vector<sweep::NativeSweepDevice> out;
        if (hackrfError != HACKRF_SUCCESS) { return out; }
#ifndef __ANDROID__
        if (running || sweepDev != NULL) {
            // Do not enumerate (which opens devices) while one of them is streaming.
            for (auto& s : devList) { out.push_back({ s, "HackRF " + s.substr(s.size() > 8 ? s.size() - 8 : 0) }); }
            if (sweepDev != NULL && std::find(devList.begin(), devList.end(), sweepSerial) == devList.end()) {
                out.push_back({ sweepSerial, "HackRF " + sweepSerial.substr(sweepSerial.size() > 8 ? sweepSerial.size() - 8 : 0) });
            }
            return out;
        }
        hackrf_device_list_t* list = hackrf_device_list();
        if (list == NULL) { return out; }
        for (int i = 0; i < list->devicecount; i++) {
            if (list->serial_numbers[i] == NULL) { continue; }
            std::string s = list->serial_numbers[i];
            out.push_back({ s, "HackRF " + s.substr(s.size() > 8 ? s.size() - 8 : 0) });
        }
        hackrf_device_list_free(list);
#else
        int vid, pid;
        if (running || sweepDev != NULL || backend::getDeviceFD(vid, pid, backend::HACKRF_VIDPIDS) >= 0) {
            out.push_back({ "android_usb", "HackRF (USB)" });
        }
#endif
        return out;
    }

    bool getCaps(const std::string& deviceId, sweep::NativeSweepCaps& caps, std::string& error) {
        namespace hf = sweep::hackrf_format;
        if (hackrfError != HACKRF_SUCCESS) {
            error = std::string("HackRF library initialization failed: ") + hackrf_error_name(hackrfError);
            return false;
        }
        caps = sweep::NativeSweepCaps();
        caps.minFreqHz = 0;
        caps.maxFreqHz = hf::MAX_FREQ_MHZ * 1e6;
        caps.specMinFreqHz = 1e6;
        caps.specMaxFreqHz = 6000e6;
        caps.sampleRateHz = hf::SAMPLE_RATE_HZ;
        caps.stepWidthHz = hf::STEP_WIDTH_HZ;
        caps.basebandFilterHz = hf::BASEBAND_FILTER_HZ;
        caps.samplesPerBlock = hf::BLOCK_SAMPLES;
        caps.maxDwellBlocks = 16;
        caps.libraryVersion = std::string(hackrf_library_version()) + " (" + hackrf_library_release() + ")";
        caps.gains = {
            { "lna", "LNA Gain", 0, 40, 8, 16, false, "" },
            { "vga", "VGA Gain", 0, 62, 2, 20, false, "" },
            { "amp", "RF Amp (+11 dB)", 0, 1, 1, 0, true, "Avoid with strong signals or antennas carrying DC." },
            { "bias", "Bias-T (antenna power)", 0, 1, 1, 0, true, "Supplies 3.3 V on the antenna port." },
        };

        std::lock_guard<std::mutex> lck(sweepMtx);
        auto it = fwCache.find(deviceId);
        if (it == fwCache.end() && !running && sweepDev == NULL) {
            // Probe the firmware once while the device is free.
            hackrf_device* dev = NULL;
            std::string err;
            if (openForSweep(deviceId, &dev, err)) {
                FirmwareInfo fi;
                readFirmwareInfo(dev, fi);
                hackrf_close(dev);
                fwCache[deviceId] = fi;
                it = fwCache.find(deviceId);
            }
        }
        if (it != fwCache.end()) {
            caps.firmwareKnown = true;
            caps.firmwareVersion = it->second.version;
            caps.boardName = it->second.board;
            caps.nativeSweepSupported = it->second.usbApi >= 0x0102;
            if (!caps.nativeSweepSupported) {
                char buf[256];
                snprintf(buf, sizeof(buf), "HackRF firmware %s (USB API %x.%02x) does not support sweep mode (needs USB API 1.02). Update the firmware or use the Generic sweep mode.",
                         it->second.version.c_str(), it->second.usbApi >> 8, it->second.usbApi & 0xFF);
                caps.supportNote = buf;
            }
        }
        else {
            caps.firmwareKnown = false;
            caps.nativeSweepSupported = true;
            caps.supportNote = "Firmware not checked yet (device busy or not connected); checked when the sweep starts.";
        }
        return true;
    }

    sweep::Ownership getOwnership(const std::string& deviceId) {
        if (hackrfError != HACKRF_SUCCESS) { return sweep::Ownership::UNAVAILABLE; }
#ifdef __ANDROID__
        if (sweepDev != NULL) { return sweep::Ownership::SWEEP; }
        if (running) { return sweep::Ownership::RECEIVER; }
        int vid, pid;
        if (backend::getDeviceFD(vid, pid, backend::HACKRF_VIDPIDS) < 0) { return sweep::Ownership::UNAVAILABLE; }
        return sweep::Ownership::FREE;
#else
        if (sweepDev != NULL && deviceId == sweepSerial) { return sweep::Ownership::SWEEP; }
        if (running && deviceId == selectedSerial) { return sweep::Ownership::RECEIVER; }
        return sweep::Ownership::FREE;
#endif
    }

    bool startSweep(const sweep::NativeSweepRequest& req, sweep::BlockCallback cb, void* ctx, sweep::NativeSweepPlanInfo& plan, std::string& error) {
        namespace hf = sweep::hackrf_format;
        std::lock_guard<std::mutex> lck(sweepMtx);
        if (sweepDev != NULL) {
            error = "A HackRF sweep is already running";
            return false;
        }
        if (hackrfError != HACKRF_SUCCESS) {
            error = std::string("HackRF library initialization failed: ") + hackrf_error_name(hackrfError);
            return false;
        }
        if (running && (req.deviceId == selectedSerial || req.deviceId == "android_usb")) {
            error = "The HackRF is in use by the receiver. Stop the receiver first.";
            return false;
        }
        uint16_t freqs[2];
        int steps = hf::alignRange(req.startHz, req.stopHz, freqs[0], freqs[1]);
        if (steps <= 0) {
            error = "Range outside of the HackRF sweep range (0 - 7250 MHz)";
            return false;
        }

        hackrf_device* dev = NULL;
        if (!openForSweep(req.deviceId, &dev, error)) { return false; }

        FirmwareInfo fi;
        readFirmwareInfo(dev, fi);
        fwCache[req.deviceId] = fi;
        if (fi.usbApi < 0x0102) {
            hackrf_close(dev);
            char buf[256];
            snprintf(buf, sizeof(buf), "HackRF firmware %s (USB API %x.%02x) does not support sweep mode (needs USB API 1.02). Update the firmware or use the Generic sweep mode.",
                     fi.version.c_str(), fi.usbApi >> 8, fi.usbApi & 0xFF);
            error = buf;
            return false;
        }

        int dwell = std::clamp(req.dwellBlocks, 1, 16);
        hackrf_error err = (hackrf_error)hackrf_set_sample_rate_manual(dev, (uint32_t)hf::SAMPLE_RATE_HZ, 1);
        if (err == HACKRF_SUCCESS) { err = (hackrf_error)hackrf_set_baseband_filter_bandwidth(dev, (uint32_t)hf::BASEBAND_FILTER_HZ); }
        if (err == HACKRF_SUCCESS) {
            for (auto& g : req.gains) {
                err = applySweepGain(dev, g.first, g.second);
                if (err != HACKRF_SUCCESS) { break; }
            }
        }
        if (err == HACKRF_SUCCESS) { err = (hackrf_error)hackrf_init_sweep(dev, freqs, 1, (uint32_t)(dwell * hf::BLOCK_BYTES), (uint32_t)hf::STEP_WIDTH_HZ, (uint32_t)hf::OFFSET_HZ, INTERLEAVED); }
        if (err != HACKRF_SUCCESS) {
            error = std::string("Could not configure the HackRF sweep: ") + hackrf_error_name(err);
            hackrf_close(dev);
            return false;
        }

        sweepConv.resize(2 * hf::BLOCK_SAMPLES);
        sweepCb = cb;
        sweepCtx = ctx;
#ifndef SDRPP_HACKRF_NO_START_RX_SWEEP
        err = (hackrf_error)hackrf_start_rx_sweep(dev, sweepCallback, this);
#else
        err = (hackrf_error)hackrf_start_rx(dev, sweepCallback, this);
#endif
        if (err != HACKRF_SUCCESS) {
            sweepCb = NULL;
            sweepCtx = NULL;
            error = std::string("Could not start the HackRF sweep: ") + hackrf_error_name(err);
            hackrf_close(dev);
            return false;
        }
        sweepDev = dev;
        sweepSerial = req.deviceId;

        plan.alignedStartHz = freqs[0] * 1e6;
        plan.alignedStopHz = freqs[1] * 1e6;
        plan.sampleRateHz = hf::SAMPLE_RATE_HZ;
        plan.stepWidthHz = hf::STEP_WIDTH_HZ;
        plan.loOffsetHz = hf::OFFSET_HZ;
        plan.blockFrequenciesHz = hf::interleavedBlockFrequencies(freqs[0], steps);
        plan.validOffsets = hf::validOffsets();
        plan.samplesPerBlock = hf::BLOCK_SAMPLES;
        flog::info("HackRFSourceModule '{0}': native sweep {1}-{2} MHz started", name, (int)freqs[0], (int)freqs[1]);
        return true;
    }

    void stopSweep() {
        std::lock_guard<std::mutex> lck(sweepMtx);
        if (sweepDev == NULL) { return; }
        hackrf_error err = (hackrf_error)hackrf_close(sweepDev);
        sweepDev = NULL;
        sweepCb = NULL;
        sweepCtx = NULL;
        sweepSerial = "";
        if (err != HACKRF_SUCCESS) {
            flog::error("Could not close HackRF after sweep: {0}", hackrf_error_name(err));
        }
        flog::info("HackRFSourceModule '{0}': native sweep stopped", name);
    }

    sweep::StreamStatus sweepStatus(std::string& detail) {
        std::lock_guard<std::mutex> lck(sweepMtx);
        detail.clear();
        if (sweepDev == NULL) { return sweep::StreamStatus::STOPPED; }
        int r = hackrf_is_streaming(sweepDev);
        if (r == HACKRF_TRUE) { return sweep::StreamStatus::OK; }
        detail = std::string("HackRF stopped streaming: ") + hackrf_error_name((hackrf_error)r) + " (disconnected?)";
        return sweep::StreamStatus::DEVICE_LOST;
    }

    bool setSweepGain(const std::string& id, double value) {
        std::lock_guard<std::mutex> lck(sweepMtx);
        if (sweepDev == NULL) { return false; }
        return applySweepGain(sweepDev, id, value) == HACKRF_SUCCESS;
    }

    void refresh() {
        devList.clear();
        devListTxt = "";

#ifndef __ANDROID__
        uint64_t serials[256];
        hackrf_device_list_t* _devList = hackrf_device_list();

        for (int i = 0; i < _devList->devicecount; i++) {
            // Skip devices that are in use
            if (_devList->serial_numbers[i] == NULL) { continue; }

            // Save the device serial number
            devList.push_back(_devList->serial_numbers[i]);
            devListTxt += (char*)(_devList->serial_numbers[i] + 16);
            devListTxt += '\0';
        }

        hackrf_device_list_free(_devList);
#else
        int vid, pid;
        devFd = backend::getDeviceFD(vid, pid, backend::HACKRF_VIDPIDS);
        if (devFd < 0) { return; }
        std::string fakeName = "HackRF USB";
        devList.push_back("fake_serial");
        devListTxt += fakeName;
        devListTxt += '\0';
#endif
    }

    void selectFirst() {
        if (devList.size() != 0) {
            selectBySerial(devList[0]);
            return;
        }
        selectedSerial = "";
    }

    void selectBySerial(std::string serial) {
        if (std::find(devList.begin(), devList.end(), serial) == devList.end()) {
            selectFirst();
            return;
        }

        bool created = false;
        config.acquire();
        if (!config.conf["devices"].contains(serial)) {
            config.conf["devices"][serial]["sampleRate"] = 2000000;
            config.conf["devices"][serial]["biasT"] = false;
            config.conf["devices"][serial]["amp"] = false;
            config.conf["devices"][serial]["lnaGain"] = 0;
            config.conf["devices"][serial]["vgaGain"] = 0;
            config.conf["devices"][serial]["bandwidth"] = 16;
        }
        config.release(created);

        // Set default values
        srId = 6;
        sampleRate = 2000000;
        biasT = false;
        amp = false;
        lna = 0;
        vga = 0;
        bwId = 16;

        // Load from config if available and validate
        if (config.conf["devices"][serial].contains("sampleRate")) {
            int psr = config.conf["devices"][serial]["sampleRate"];
#ifdef __ANDROID__
            psr = std::min(psr, ANDROID_MAX_SAMPLE_RATE);
#endif
            for (int i = 0; i < 7; i++) {
                if (sampleRates[i] == psr) {
                    sampleRate = psr;
                    srId = i;
                }
            }
        }
        if (config.conf["devices"][serial].contains("biasT")) {
            biasT = config.conf["devices"][serial]["biasT"];
        }
        if (config.conf["devices"][serial].contains("amp")) {
            amp = config.conf["devices"][serial]["amp"];
        }
        if (config.conf["devices"][serial].contains("lnaGain")) {
            lna = config.conf["devices"][serial]["lnaGain"];
        }
        if (config.conf["devices"][serial].contains("vgaGain")) {
            vga = config.conf["devices"][serial]["vgaGain"];
        }
        if (config.conf["devices"][serial].contains("bandwidth")) {
            bwId = config.conf["devices"][serial]["bandwidth"];
            bwId = std::clamp<int>(bwId, 0, 16);
        }

        selectedSerial = serial;
    }

private:
    static void menuSelected(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
#ifdef __ANDROID__
        // Pick up a receiver that was plugged in after the list was last built
        _this->refresh();
        _this->selectFirst();
#endif
        core::setInputSampleRate(_this->sampleRate);
        flog::info("HackRFSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
        flog::info("HackRFSourceModule '{0}': Menu Deselect!", _this->name);
    }

    int bandwidthIdToBw(int id) {
        if (id == 16) { return hackrf_compute_baseband_filter_bw(sampleRate); }
        return bandwidths[id];
    }

    static void start(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
        if (_this->running) { return; }
        if (_this->sweepDev != NULL) {
            flog::error("Could not start HackRF: the device is used by the Spectrum Analyzer (native sweep)");
            return;
        }
        if (_this->hackrfError != HACKRF_SUCCESS) {
            flog::error("Could not start HackRF: initialization failed: {0}", hackrf_error_name(_this->hackrfError));
            return;
        }
        if (_this->selectedSerial == "") {
            flog::error("Tried to start HackRF source with empty serial");
            return;
        }

#ifndef __ANDROID__
        hackrf_error err = (hackrf_error)hackrf_open_by_serial(_this->selectedSerial.c_str(), &_this->openDev);
#else
        int vid, pid;
        _this->devFd = backend::getDeviceFD(vid, pid, backend::HACKRF_VIDPIDS);
        if (_this->devFd < 0) {
            flog::error("Could not start HackRF: no authorized USB file descriptor");
            return;
        }
        hackrf_error err = (hackrf_error)hackrf_open_by_fd(_this->devFd, &_this->openDev);
#endif
        if (err != HACKRF_SUCCESS) {
            flog::error("Could not open HackRF {0}: {1}", _this->selectedSerial, hackrf_error_name(err));
            return;
        }

        hackrf_set_sample_rate(_this->openDev, _this->sampleRate);
        hackrf_set_baseband_filter_bandwidth(_this->openDev, _this->bandwidthIdToBw(_this->bwId));
        hackrf_set_freq(_this->openDev, _this->freq);

        hackrf_set_antenna_enable(_this->openDev, _this->biasT);
        hackrf_set_amp_enable(_this->openDev, _this->amp);
        hackrf_set_lna_gain(_this->openDev, _this->lna);
        hackrf_set_vga_gain(_this->openDev, _this->vga);

        err = (hackrf_error)hackrf_start_rx(_this->openDev, callback, _this);
        if (err != HACKRF_SUCCESS) {
            flog::error("Could not start HackRF RX: {0}", hackrf_error_name(err));
            hackrf_close(_this->openDev);
            _this->openDev = NULL;
            return;
        }

        _this->running = true;
        flog::info("HackRFSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        _this->stream.stopWriter();
        // TODO: Stream stop
        hackrf_error err = (hackrf_error)hackrf_close(_this->openDev);
        _this->openDev = NULL;
        if (err != HACKRF_SUCCESS) {
            flog::error("Could not close HackRF {0}: {1}", _this->selectedSerial, hackrf_error_name(err));
        }
        _this->stream.clearWriteStop();
        flog::info("HackRFSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
        if (_this->running) {
            hackrf_set_freq(_this->openDev, freq);
        }
        _this->freq = freq;
        flog::info("HackRFSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;

        if (_this->running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_hackrf_dev_sel_", _this->name), &_this->devId, _this->devListTxt.c_str())) {
            _this->selectBySerial(_this->devList[_this->devId]);
            core::setInputSampleRate(_this->sampleRate);
            config.acquire();
            config.conf["device"] = _this->selectedSerial;
            config.release(true);
        }

        if (SmGui::Combo(CONCAT("##_hackrf_sr_sel_", _this->name), &_this->srId, sampleRatesTxt)) {
#ifdef __ANDROID__
            // Rates above the phone limit (20, 16 and 10 MS/s) are not offered
            _this->srId = std::max(_this->srId, 3);
#endif
            _this->sampleRate = sampleRates[_this->srId];
            core::setInputSampleRate(_this->sampleRate);
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["sampleRate"] = _this->sampleRate;
            config.release(true);
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_hackrf_refr_", _this->name))) {
            _this->refresh();
            _this->selectBySerial(_this->selectedSerial);
            core::setInputSampleRate(_this->sampleRate);
        }

        if (_this->running) { SmGui::EndDisabled(); }

        SmGui::LeftLabel("Bandwidth");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_hackrf_bw_sel_", _this->name), &_this->bwId, bandwidthsTxt)) {
            if (_this->running) {
                hackrf_set_baseband_filter_bandwidth(_this->openDev, _this->bandwidthIdToBw(_this->bwId));
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["bandwidth"] = _this->bwId;
            config.release(true);
        }

        SmGui::LeftLabel("LNA Gain");
        SmGui::FillWidth();
        if (SmGui::SliderFloatWithSteps(CONCAT("##_hackrf_lna_", _this->name), &_this->lna, 0, 40, 8, SmGui::FMT_STR_FLOAT_DB_NO_DECIMAL)) {
            if (_this->running) {
                hackrf_set_lna_gain(_this->openDev, _this->lna);
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["lnaGain"] = (int)_this->lna;
            config.release(true);
        }

        SmGui::LeftLabel("VGA Gain");
        SmGui::FillWidth();
        if (SmGui::SliderFloatWithSteps(CONCAT("##_hackrf_vga_", _this->name), &_this->vga, 0, 62, 2, SmGui::FMT_STR_FLOAT_DB_NO_DECIMAL)) {
            if (_this->running) {
                hackrf_set_vga_gain(_this->openDev, _this->vga);
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["vgaGain"] = (int)_this->vga;
            config.release(true);
        }

        if (SmGui::Checkbox(CONCAT("Bias-T##_hackrf_bt_", _this->name), &_this->biasT)) {
            if (_this->running) {
                hackrf_set_antenna_enable(_this->openDev, _this->biasT);
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["biasT"] = _this->biasT;
            config.release(true);
        }

        if (SmGui::Checkbox(CONCAT("Amp Enabled##_hackrf_amp_", _this->name), &_this->amp)) {
            if (_this->running) {
                hackrf_set_amp_enable(_this->openDev, _this->amp);
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["amp"] = _this->amp;
            config.release(true);
        }
    }

    static int callback(hackrf_transfer* transfer) {
        HackRFSourceModule* _this = (HackRFSourceModule*)transfer->rx_ctx;
        volk_8i_s32f_convert_32f((float*)_this->stream.writeBuf, (int8_t*)transfer->buffer, 128.0f, transfer->valid_length);
        if (!_this->stream.swap(transfer->valid_length / 2)) { return -1; }
        return 0;
    }

    struct FirmwareInfo {
        std::string version;
        std::string board;
        uint16_t usbApi = 0;
    };

    bool openForSweep(const std::string& deviceId, hackrf_device** dev, std::string& error) {
#ifndef __ANDROID__
        hackrf_error err = (hackrf_error)hackrf_open_by_serial(deviceId.c_str(), dev);
#else
        int vid, pid;
        int fd = backend::getDeviceFD(vid, pid, backend::HACKRF_VIDPIDS);
        if (fd < 0) {
            error = "No authorized HackRF USB device";
            return false;
        }
        hackrf_error err = (hackrf_error)hackrf_open_by_fd(fd, dev);
#endif
        if (err != HACKRF_SUCCESS) {
            error = std::string("Could not open the HackRF: ") + hackrf_error_name(err);
            *dev = NULL;
            return false;
        }
        return true;
    }

    static void readFirmwareInfo(hackrf_device* dev, FirmwareInfo& fi) {
        char ver[255] = { 0 };
        if (hackrf_version_string_read(dev, ver, sizeof(ver) - 1) == HACKRF_SUCCESS) { fi.version = ver; }
        uint8_t board = 0;
        if (hackrf_board_id_read(dev, &board) == HACKRF_SUCCESS) { fi.board = hackrf_board_id_name((enum hackrf_board_id)board); }
        uint16_t api = 0;
        if (hackrf_usb_api_version_read(dev, &api) == HACKRF_SUCCESS) { fi.usbApi = api; }
    }

    static hackrf_error applySweepGain(hackrf_device* dev, const std::string& id, double value) {
        if (id == "lna") { return (hackrf_error)hackrf_set_lna_gain(dev, std::clamp((int)std::lround(value / 8.0) * 8, 0, 40)); }
        if (id == "vga") { return (hackrf_error)hackrf_set_vga_gain(dev, std::clamp((int)std::lround(value / 2.0) * 2, 0, 62)); }
        if (id == "amp") { return (hackrf_error)hackrf_set_amp_enable(dev, value >= 0.5 ? 1 : 0); }
        if (id == "bias") { return (hackrf_error)hackrf_set_antenna_enable(dev, value >= 0.5 ? 1 : 0); }
        return HACKRF_SUCCESS;
    }

    // Sweep mode transfers carry BYTES_PER_BLOCK blocks, each starting with
    // 0x7F 0x7F and the 64-bit tuning frequency (see hackrf_sweep).
    static int sweepCallback(hackrf_transfer* transfer) {
        namespace hf = sweep::hackrf_format;
        HackRFSourceModule* _this = (HackRFSourceModule*)transfer->rx_ctx;
        sweep::BlockCallback cb = _this->sweepCb.load();
        if (cb == NULL) { return 0; }
        int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        int blocks = transfer->valid_length / hf::BLOCK_BYTES;
        for (int i = 0; i < blocks; i++) {
            uint8_t* block = transfer->buffer + (size_t)i * hf::BLOCK_BYTES;
            uint64_t freq;
            if (!hf::parseHeader(block, freq)) { continue; }
            volk_8i_s32f_convert_32f(_this->sweepConv.data(), (int8_t*)(block + hf::SKIP_BYTES), 128.0f, hf::BLOCK_BYTES - hf::SKIP_BYTES);
            cb(_this->sweepCtx, freq, _this->sweepConv.data(), hf::BLOCK_SAMPLES, now);
        }
        return 0;
    }

    std::string name;
    hackrf_device* openDev = NULL;
    hackrf_error hackrfError = HACKRF_ERROR_OTHER;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    int sampleRate;
    SourceManager::SourceHandler handler;
    bool running = false;
    double freq;
    std::string selectedSerial = "";
    int devId = 0;
    int srId = 0;
    int bwId = 16;
    bool biasT = false;
    bool amp = false;
    float lna = 0;
    float vga = 0;

#ifdef __ANDROID__
    int devFd = -1;
#endif

    std::vector<std::string> devList;
    std::string devListTxt;

    // Native sweep state (Spectrum Analyzer)
    std::mutex sweepMtx;
    hackrf_device* sweepDev = NULL;
    std::string sweepSerial;
    std::atomic<sweep::BlockCallback> sweepCb{ NULL };
    void* sweepCtx = NULL;
    std::vector<float> sweepConv;
    std::map<std::string, FirmwareInfo> fwCache;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(std::string(core::getRoot()) + "/hackrf_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new HackRFSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (HackRFSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
