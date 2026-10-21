#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include <implot/implot.h>
#include <gui/main_window.h>
#include <gui/gui.h>
#include "utils/usleep.h"
#include "utils/cty.h"
#include <stdio.h>
#include <thread>
#include <complex>
#include <gui/widgets/waterfall.h>
#include <gui/widgets/frequency_select.h>
#include <signal_path/iq_frontend.h>
#include <gui/icons.h>
#include <gui/widgets/bandplan.h>
#include <gui/style.h>
#include <config.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/menus/source.h>
#include <gui/menus/display.h>
#include <gui/menus/bandplan.h>
#include <gui/menus/sink.h>
#include <gui/menus/vfo_color.h>
#include <gui/menus/module_manager.h>
#include <gui/menus/theme.h>
#include <gui/dialogs/credits.h>
#include <cstring>
#include <map>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <signal_path/source.h>
#include <gui/dialogs/loading_screen.h>
#include <gui/colormaps.h>
#include <gui/widgets/snr_meter.h>
#include <gui/tuner.h>
#include <dsp/buffer/buffer.h>
#include <utils/wstr.h>
#include <utils/proto/http.h>
#include <utils/mpeg.h>
#include "brown/imgui-notify/imgui_notify.h"
#include "utils/proto/brown_ai.h"
#include "../../decoder_modules/radio/src/radio_interface.h"
#ifdef __ANDROID__
#include <imgui_internal.h>
#include <android_backend.h>
#endif

static BrownAIClient brownAIClient;

// Main views (tabs next to the receiver), see MainWindow::registerMainView()
namespace {
#ifdef __ANDROID__
    bool touchVSliderFloat(const char* label, const ImVec2& size, float* value, float min, float max) {
        bool changed = ImGui::VSliderFloat(label, size, value, min, max, "");
        static ImGuiID activeSlider = 0;
        static int activeFinger = -1;
        const ImGuiID slider = ImGui::GetItemID();
        ImGuiIO& io = ImGui::GetIO();
        // Where each finger went down: a slider only takes the touch if the touch began on it
        static bool fingerWasDown[IM_ARRAYSIZE(io.FingerDown)] = {};
        static ImVec2 fingerPressPos[IM_ARRAYSIZE(io.FingerDown)];
        static int pressFrame = -1;
        if (pressFrame != ImGui::GetFrameCount()) {
            pressFrame = ImGui::GetFrameCount();
            for (int finger = 0; finger < IM_ARRAYSIZE(io.FingerDown); finger++) {
                if (io.FingerDown[finger] && !fingerWasDown[finger]) { fingerPressPos[finger] = io.FingerPos[finger]; }
                fingerWasDown[finger] = io.FingerDown[finger];
            }
        }

        if (activeSlider != 0 && (activeFinger < 0 || !io.FingerDown[activeFinger])) {
            activeSlider = 0;
            activeFinger = -1;
        }
        if (activeSlider == 0) {
            const ImRect sliderRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            for (int finger = 0; finger < IM_ARRAYSIZE(io.FingerDown); finger++) {
                if (io.FingerDown[finger] && sliderRect.Contains(fingerPressPos[finger])) {
                    activeSlider = slider;
                    activeFinger = finger;
                    break;
                }
            }
        }
        if (activeSlider == slider && activeFinger >= 0 && io.FingerDown[activeFinger]) {
            const ImVec2 sliderMin = ImGui::GetItemRectMin();
            const ImVec2 sliderMax = ImGui::GetItemRectMax();
            const float position = ImClamp((io.FingerPos[activeFinger].y - sliderMin.y) / (sliderMax.y - sliderMin.y), 0.0f, 1.0f);
            const float touchValue = max + position * (min - max);
            if (touchValue != *value) {
                *value = touchValue;
                changed = true;
            }
        }
        return changed;
    }
#endif

    struct MainView {
        std::string name;
        std::function<void()> drawFunc;
    };

    std::vector<MainView> mainViews;
    std::string selectedMainView;

    bool selectedSourceReady() {
        const std::string& source = sigpath::sourceManager.getSelectedName();
        if (source.empty()) { return false; }
#ifdef __ANDROID__
        const std::vector<backend::DevVIDPID>* allowed = NULL;
        if (source == "RTL-SDR") { allowed = &backend::RTL_SDR_VIDPIDS; }
        else if (source == "HackRF") { allowed = &backend::HACKRF_VIDPIDS; }
        else if (source == "Airspy") { allowed = &backend::AIRSPY_VIDPIDS; }
        else if (source == "Airspy HF+") { allowed = &backend::AIRSPYHF_VIDPIDS; }
        else if (source == "HydraSDR") { allowed = &backend::HYDRASDR_VIDPIDS; }
        if (allowed != NULL) {
            static std::string checkedSource;
            static double nextCheck = 0.0;
            static bool ready = false;
            const double now = ImGui::GetTime();
            if (source != checkedSource || now >= nextCheck) {
                int vid, pid;
                ready = backend::getDeviceFD(vid, pid, *allowed) >= 0;
                checkedSource = source;
                nextCheck = now + 0.5;
            }
            return ready;
        }
#endif
        return true;
    }

    // Short centred hint shown after a keyboard shortcut (squelch level etc.)
    double keyHintUntil = 0;
    std::string keyHintText;

    void showKeyHint(const std::string& text) {
        keyHintText = text;
        keyHintUntil = ImGui::GetTime() + 1.5;
    }

    // Enter: type a frequency
    bool freqEntryOpenRequest = false;
    char freqEntryBuf[64] = "";

    // Accepts "145.5", "145.5M", "7074k", "1.2G", "145500000", "145.500.000" (grouped Hz).
    // A bare number below 100000 is MHz, larger ones are Hz. Returns <= 0 when invalid.
    double parseTypedFrequency(const char* text) {
        std::string t;
        for (const char* c = text; *c; c++) {
            if (*c == ' ' || *c == '_') { continue; }
            t += (*c == ',') ? '.' : *c;
        }
        if (t.empty()) { return -1; }
        double mult = 0;
        while (!t.empty() && std::isalpha((unsigned char)t.back())) {
            char u = (char)std::tolower((unsigned char)t.back());
            if (u == 'k') { mult = 1e3; }
            else if (u == 'm') { mult = 1e6; }
            else if (u == 'g') { mult = 1e9; }
            else if (u == 'h' && mult == 0) { mult = 1; }
            else if (u != 'z' && u != 'h') { return -1; }
            t.pop_back();
        }
        if (std::count(t.begin(), t.end(), '.') > 1) {
            t.erase(std::remove(t.begin(), t.end(), '.'), t.end());
            if (mult == 0) { mult = 1; }
        }
        char* end = nullptr;
        double v = strtod(t.c_str(), &end);
        if (t.empty() || end != t.c_str() + t.size() || !(v > 0)) { return -1; }
        if (mult == 0) { mult = (v < 100000) ? 1e6 : 1; }
        return v * mult;
    }

    void drawKeyHintAndFrequencyEntry() {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::GetTime() < keyHintUntil) {
            ImGui::PushFont(style::bigFont);
            ImVec2 ts = ImGui::CalcTextSize(keyHintText.c_str());
            ImVec2 pos((io.DisplaySize.x - ts.x) * 0.5f, io.DisplaySize.y * 0.3f);
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(ImVec2(pos.x - 16, pos.y - 8), ImVec2(pos.x + ts.x + 16, pos.y + ts.y + 8), IM_COL32(0, 0, 0, 190), 6.0f);
            dl->AddText(pos, IM_COL32(255, 255, 255, 255), keyHintText.c_str());
            ImGui::PopFont();
        }

        bool focusInput = false;
        if (freqEntryOpenRequest) {
            freqEntryOpenRequest = false;
            snprintf(freqEntryBuf, sizeof(freqEntryBuf), "%.6f", (double)gui::freqSelect.frequency / 1e6);
            ImGui::OpenPopup("Go to frequency##sdrpp_freq_entry");
            focusInput = true;
        }
        if (!ImGui::IsPopupOpen("Go to frequency##sdrpp_freq_entry")) { return; }
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.25f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        if (!ImGui::BeginPopupModal("Go to frequency##sdrpp_freq_entry", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            return;
        }
        float scale = style::uiScale;
        ImGui::TextDisabled("MHz by default; k / M / G suffix, or Hz");
        if (focusInput) { ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(260.0f * scale);
        ImGui::PushFont(style::bigFont);
        bool enter = ImGui::InputText("##sdrpp_freq_entry_txt", freqEntryBuf, sizeof(freqEntryBuf),
                                      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsNoBlank);
        ImGui::PopFont();
        double hz = parseTypedFrequency(freqEntryBuf);
        if (hz > 0) {
            ImGui::Text("= %.6f MHz", hz / 1e6);
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "not a frequency");
        }
        bool go = ImGui::Button("Go##sdrpp_freq_entry_go", ImVec2(120.0f * scale, 0)) || enter;
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel##sdrpp_freq_entry_cancel", ImVec2(120.0f * scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (go && hz > 0) {
            gui::freqSelect.setFrequency((int64_t)std::llround(hz));
            gui::freqSelect.frequencyChanged = true;
            ImGui::CloseCurrentPopup();
        }
        else if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Portrait layout state
    bool portraitSlidersOpen = false;
    bool upperLineSkipFreq = false;
    ImVec2 volumeSliderMin(0, 0), volumeSliderMax(0, 0);
}

bool MainWindow::isPortraitLayout() {
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    return ds.y > ds.x * 1.05f;
}

void MainWindow::handleTouchScrollX(ImVec2 excludeMin, ImVec2 excludeMax) {
    // Same gesture logic as the Android menu scroll, on the X axis, per window.
    struct State {
        int mode = 0; // 0 idle, 1 tracking, 2 dragging, 3 ignored
        ImVec2 start, last;
        float scroll = 0;
    };
    static std::map<ImGuiID, State> states;
    auto& io = ImGui::GetIO();
    auto window = ImGui::GetCurrentWindow();
    State& st = states[window->ID];
    if (st.mode == 0) {
        bool inExclude = excludeMax.x > excludeMin.x && ImRect(excludeMin, excludeMax).Contains(io.MousePos);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && window->InnerRect.Contains(io.MousePos) && !inExclude) {
            st.mode = 1;
            st.start = st.last = io.MousePos;
            st.scroll = ImGui::GetScrollX();
        }
    }
    if (st.mode == 0) { return; }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        st.mode = 0;
        return;
    }
    ImVec2 total = io.MousePos - st.start;
    ImVec2 frame = io.MousePos - st.last;
    if (st.mode == 1) {
        float threshold = 8.0f * style::uiScale;
        if (fabsf(total.x) >= threshold && fabsf(total.x) > fabsf(total.y) * 1.2f) {
            st.mode = 2;
            st.scroll = ImGui::GetScrollX();
            ImGui::ClearActiveID();
        }
        else if (fabsf(total.y) >= threshold) {
            st.mode = 3;
        }
    }
    if (st.mode == 2) {
        st.scroll = ImClamp(st.scroll - frame.x, 0.0f, ImGui::GetScrollMaxX());
        ImGui::SetScrollX(st.scroll);
    }
    st.last = io.MousePos;
}

void MainWindow::init() {
    LoadingScreen::show("Initializing UI");
    gui::waterfall.init();
    gui::waterfall.setRawFFTSize(fftSize);

    credits::init();

    core::configManager.acquire();
    json menuElements = core::configManager.conf["menuElements"];
    std::string modulesDir = core::configManager.conf["modulesDirectory"];
    std::string resourcesDir = core::configManager.conf["resourcesDirectory"];
    core::configManager.release();

    // Assert that directories are absolute
    modulesDir = std::filesystem::absolute(modulesDir).string();
    resourcesDir = std::filesystem::absolute(resourcesDir).string();

    dsp::buffer::runVerifier();

    // Load menu elements
    gui::menu.order.clear();
    for (auto& elem : menuElements) {
        if (!elem.contains("name")) {
            flog::error("Menu element is missing name key");
            continue;
        }
        if (!elem["name"].is_string()) {
            flog::error("Menu element name isn't a string");
            continue;
        }
        if (!elem.contains("open")) {
            flog::error("Menu element is missing open key");
            continue;
        }
        if (!elem["open"].is_boolean()) {
            flog::error("Menu element name isn't a string");
            continue;
        }
        Menu::MenuOption_t opt;
        opt.name = elem["name"];
        opt.open = elem["open"];
        gui::menu.order.push_back(opt);
    }

    gui::menu.registerEntry("Source", sourcemenu::draw, NULL);
    gui::menu.registerEntry("Sinks", sinkmenu::draw, NULL);
    gui::menu.registerEntry("Band Plan", bandplanmenu::draw, NULL);
    gui::menu.registerEntry("Display", displaymenu::draw, NULL);
    gui::menu.registerEntry("Theme", thememenu::draw, NULL);
    gui::menu.registerEntry("VFO Color", vfo_color_menu::draw, NULL);
    gui::menu.registerEntry("Module Manager", module_manager_menu::draw, NULL);

    gui::freqSelect.init();

    // Set default values for waterfall in case no source init's it
    gui::waterfall.setBandwidth(DEFAULT_SAMPLE_RATE);
    gui::waterfall.setViewBandwidth(DEFAULT_SAMPLE_RATE * gui::waterfall.getUsableSpectrumRatio());

    sigpath::iqFrontEnd.init(&dummyStream, DEFAULT_SAMPLE_RATE, true, 1, false, 1024, 20.0, IQFrontEnd::FFTWindow::NUTTALL, acquireFFTBuffer, releaseFFTBuffer, this);
    sigpath::iqFrontEnd.start();

    vfoCreatedHandler.handler = vfoAddedHandler;
    vfoCreatedHandler.ctx = this;
    sigpath::vfoManager.onVfoCreated.bindHandler(&vfoCreatedHandler);

    flog::info("Loading modules");

    // Load modules from /module directory
    if (std::filesystem::is_directory(modulesDir)) {
        for (const auto& file : std::filesystem::directory_iterator(modulesDir)) {
            std::string path = file.path().generic_string();
            if (file.path().extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            flog::info("Loading {0}", path);
            LoadingScreen::show("Loading " + file.path().filename().string());
            core::moduleManager.loadModule(path);
        }
    }
    else {
        flog::warn("Module directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    // Read module config
    core::configManager.acquire();
    std::vector<std::string> modules = core::configManager.conf["modules"];
    auto modList = core::configManager.conf["moduleInstances"].items();
    core::configManager.release();

    // Load additional modules specified through config
    for (auto const& path : modules) {
#ifndef __ANDROID__
        std::string apath = std::filesystem::absolute(path).string();
        flog::info("Loading {0}", apath);
        LoadingScreen::show("Loading " + std::filesystem::path(path).filename().string());
        core::moduleManager.loadModule(apath);
#else
        core::moduleManager.loadModule(path);
#endif
    }

    // Create module instances
    usleep(100000);
    for (auto const& [name, _module] : modList) {
        std::string mod = _module["module"];
        bool enabled = _module["enabled"];
        flog::info("Initializing {0} ({1})", name, mod);
        LoadingScreen::show("Initializing " + name + " (" + mod + ")");
        core::moduleManager.createInstance(name, mod);
        if (!enabled) { core::moduleManager.disableInstance(name); }
    }

    // Load color maps
    LoadingScreen::show("Loading color maps");
    flog::info("Loading color maps");
    if (std::filesystem::is_directory(resourcesDir + "/colormaps")) {
        for (const auto& file : std::filesystem::directory_iterator(resourcesDir + "/colormaps")) {
            std::string path = file.path().generic_string();
            LoadingScreen::show("Loading " + file.path().filename().string());
            flog::info("Loading {0}", path);
            if (file.path().extension().generic_string() != ".json") {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            colormaps::loadMap(path);
        }
    }
    else {
        flog::warn("Color map directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    gui::waterfall.updatePalletteFromArray(colormaps::maps["Turbo"].map, colormaps::maps["Turbo"].entryCount);

    utils::loadAllCty();


    sourcemenu::init();
    sinkmenu::init();
    bandplanmenu::init();
    displaymenu::init();
    vfo_color_menu::init();
    module_manager_menu::init();

    // TODO for 0.2.5
    // Fix gain not updated on startup, soapysdr

    // Update UI settings
    LoadingScreen::show("Loading configuration");
    core::configManager.acquire();
    fftMin = core::configManager.conf["min"];
    fftMax = core::configManager.conf["max"];
    bw = core::configManager.conf["zoomBw"];
    gui::waterfall.setFFTMin(fftMin);
    gui::waterfall.setWaterfallMin(fftMin);
    gui::waterfall.setFFTMax(fftMax);
    gui::waterfall.setWaterfallMax(fftMax);


    double frequency = core::configManager.conf["frequency"];

    showMenu = core::configManager.conf["showMenu"];
    startedWithMenuClosed = !showMenu;

    gui::freqSelect.setFrequency(frequency);
    gui::freqSelect.frequencyChanged = false;
    sigpath::sourceManager.tune(frequency);
    gui::waterfall.setCenterFrequency(frequency);
    gui::waterfall.vfoFreqChanged = false;
    gui::waterfall.centerFreqMoved = false;
    gui::waterfall.selectFirstVFO();

    menuWidth = core::configManager.conf["menuWidth"];
    newWidth = menuWidth;

    fftHeight = core::configManager.conf["fftHeight"];
    gui::waterfall.setFFTHeight(fftHeight);

    tuningMode = core::configManager.conf["centerTuning"] ? tuner::TUNER_MODE_CENTER : tuner::TUNER_MODE_NORMAL;
    gui::waterfall.VFOMoveSingleClick = (tuningMode == tuner::TUNER_MODE_CENTER);

    core::configManager.release();


    // Correct the offset of all VFOs so that they fit on the screen
    float finalBwHalf = gui::waterfall.getBandwidth() / 2.0;
    for (auto& [_name, _vfo] : gui::waterfall.vfos) {
        if (_vfo->lowerOffset < -finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, (_vfo->bandwidth / 2) - finalBwHalf);
            continue;
        }
        if (_vfo->upperOffset > finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, finalBwHalf - (_vfo->bandwidth / 2));
            continue;
        }
    }

    updateWaterfallZoomBandwidth(bw);

    autostart = core::args["autostart"].b();
    initComplete = true;

    core::moduleManager.doPostInitAll();
}

float* MainWindow::acquireFFTBuffer(void* ctx) {
    return gui::waterfall.getFFTBuffer();
}

void MainWindow::releaseFFTBuffer(void* ctx) {
    gui::waterfall.pushFFT();
}

void MainWindow::vfoAddedHandler(VFOManager::VFO* vfo, void* ctx) {
    MainWindow* _this = (MainWindow*)ctx;
    std::string name = vfo->getName();
    core::configManager.acquire();
    if (!core::configManager.conf["vfoOffsets"].contains(name)) {
        core::configManager.release();
        return;
    }
    double offset = core::configManager.conf["vfoOffsets"][name];
    core::configManager.release();

    double viewBW = gui::waterfall.getViewBandwidth();
    double viewOffset = gui::waterfall.getViewOffset();

    double viewLower = viewOffset - (viewBW / 2.0);
    double viewUpper = viewOffset + (viewBW / 2.0);

    double newOffset = std::clamp<double>(offset, viewLower, viewUpper);

    sigpath::vfoManager.setCenterOffset(name, _this->initComplete ? newOffset : offset);
}

void MainWindow::preDraw(ImGui::WaterfallVFO** vfo) {

    *vfo = NULL;
    if (gui::waterfall.selectedVFO != "") {
        *vfo = gui::waterfall.vfos[gui::waterfall.selectedVFO];
    }

    // Handle VFO movement
    if (*vfo != NULL) {
        if ((*vfo)->centerOffsetChanged) {
            if (tuningMode == tuner::TUNER_MODE_CENTER) {
                tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::waterfall.getCenterFrequency() + (*vfo)->generalOffset);
            }
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + (*vfo)->generalOffset);
            gui::freqSelect.frequencyChanged = false;

            core::configManager.acquire();
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = (*vfo)->generalOffset;
            core::configManager.release(true);
        }
    }

    sigpath::vfoManager.updateFromWaterfall(&gui::waterfall);

    lockWaterfallControls = false;
    // Handle selection of another VFO
    if (gui::waterfall.selectedVFOChanged) {
        gui::freqSelect.setFrequency((*vfo != NULL) ? ((*vfo)->generalOffset + gui::waterfall.getCenterFrequency()) : gui::waterfall.getCenterFrequency());
        gui::waterfall.selectedVFOChanged = false;
        gui::freqSelect.frequencyChanged = false;
    }

    // Handle change in selected frequency
    if (gui::freqSelect.frequencyChanged) {
        gui::freqSelect.frequencyChanged = false;
        tuner::tune(tuningMode, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
        if (*vfo != NULL) {
            (*vfo)->centerOffsetChanged = false;
            (*vfo)->lowerOffsetChanged = false;
            (*vfo)->upperOffsetChanged = false;
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        if (*vfo != NULL) {
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = (*vfo)->generalOffset;
        }
        core::configManager.release(true);
    }

    // Handle dragging the uency scale
    if (gui::waterfall.centerFreqMoved) {
        gui::waterfall.centerFreqMoved = false;
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        if (*vfo != NULL) {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + (*vfo)->generalOffset);
        }
        else {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency());
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        core::configManager.release(true);
    }

    int _fftHeight = gui::waterfall.getFFTHeight();
    if (fftHeight != _fftHeight) {
        fftHeight = _fftHeight;
        core::configManager.acquire();
        core::configManager.conf["fftHeight"] = fftHeight;
        core::configManager.release(true);
    }
}

// Receiver unplugged: stop and remember whether we were playing.
// Receiver back: reselect its source (rebuilds the device list and picks the receiver),
// then continue playing if it was running before.
void MainWindow::updateSourceAvailability(bool sourceReady) {
    // Starts false so a receiver that is already there at launch also gets its device list built
    static bool lastReady = false;
    static bool resumeOnReconnect = false;
    if (sourceReady == lastReady) { return; }
    lastReady = sourceReady;
    if (!sourceReady) {
        resumeOnReconnect = playing;
        if (playing) { setPlayState(false); }
        return;
    }
    sigpath::sourceManager.selectSource(sigpath::sourceManager.getSelectedName());
    if (resumeOnReconnect) {
        resumeOnReconnect = false;
        setPlayState(true);
    }
}

void MainWindow::drawUpperLine(ImGui::WaterfallVFO* vfo) {
    ImVec2 btnSize(30 * style::uiScale, 30 * style::uiScale);
    ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const bool sourceReady = selectedSourceReady();
    updateSourceAvailability(sourceReady);
    bool tmpPlaySate = playing;
    if ((playButtonLocked || !sourceReady) && !tmpPlaySate) { style::beginDisabled(); }
    if (playing) {
        ImGui::PushID(ImGui::GetID("sdrpp_stop_btn"));
        if (ImGui::ImageButton(icons::STOP, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            setPlayState(false);
        }
        ImGui::PopID();
    }
    else if (sourceReady) {
        ImGui::PushID(ImGui::GetID("sdrpp_play_btn"));
        if (ImGui::ImageButton(icons::PLAY, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            if (!playButtonLocked) {
                setPlayState(true);
            }
        }
        ImGui::PopID();
    }
    else {
        ImGui::Button("...##sdrpp_waiting_for_receiver", btnSize);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Waiting for receiver");
        }
    }
    if ((playButtonLocked || !sourceReady) && !tmpPlaySate) { style::endDisabled(); }

    // Handle auto-start
    if (autostart) {
        autostart = false;
        setPlayState(true);
    }

    ImGui::SameLine();
    float origY = ImGui::GetCursorPosY();

    sigpath::sinkManager.showVolumeSlider(gui::waterfall.selectedVFO, "##_sdrpp_main_volume_", 248 * style::uiScale, btnSize.x, 5, true);
    volumeSliderMin = ImGui::GetItemRectMin();
    volumeSliderMax = ImGui::GetItemRectMax();

    ImGui::SameLine();

    // In portrait the frequency is drawn on its own row (see MainWindow::draw)
    if (!upperLineSkipFreq) {
        ImGui::SetCursorPosY(origY);
        gui::freqSelect.draw();

        ImGui::SameLine();
    }

    ImGui::SetCursorPosY(origY);
    if (tuningMode == tuner::TUNER_MODE_CENTER) {
        ImGui::PushID(ImGui::GetID("sdrpp_ena_st_btn"));
        if (ImGui::ImageButton(icons::CENTER_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_NORMAL;
            gui::waterfall.VFOMoveSingleClick = false;
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = false;
            core::configManager.release(true);
        }
        ImGui::PopID();
    }
    else { // TODO: Might need to check if there even is a device
        ImGui::PushID(ImGui::GetID("sdrpp_dis_st_btn"));
        if (ImGui::ImageButton(icons::NORMAL_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_CENTER;
            gui::waterfall.VFOMoveSingleClick = true;
            tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = true;
            core::configManager.release(true);
        }
        ImGui::PopID();
    }

    ImGui::SameLine();

    int snrOffset = 87.0f * style::uiScale;
    int snrWidth = std::clamp<int>(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - snrOffset, 100.0f * style::uiScale, 300.0f * style::uiScale);
    int snrPos = std::max<int>(ImGui::GetWindowSize().x - (snrWidth + snrOffset), ImGui::GetCursorPosX());

    ImGui::SetCursorPosX(snrPos);
    ImGui::SetCursorPosY(origY + (5.0f * style::uiScale));
    ImGui::SetNextItemWidth(snrWidth);
    ImGui::SNRMeter((vfo != NULL) ? gui::waterfall.selectedVFOSNR : 0);
}

long long lastDrawTime = 0;
long long lastDrawTimeBackend = 0;
int glSleepTime = 0;

void MainWindow::ShowLogWindow() {
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize * 0.8, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Log Console", &gui::mainWindow.logWindow)) {
        ImGui::End();
        return;
    }

    // As a specific feature guaranteed by the library, after calling Begin() the last Item represent the title bar.
    // So e.g. IsItemHovered() will return true when hovering the title bar.
    // Here we create a context menu only available from the title bar.
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Close Console"))
            gui::mainWindow.logWindow = false;
        ImGui::EndPopup();
    }


    // Reserve enough left-over height for 1 separator + 1 input text
    const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::BeginPopupContextWindow()) {
        if (ImGui::Selectable("Clear")) {
            flog::outMtx.lock();
            flog::logRecords.clear();
            flog::outMtx.unlock();
        }
        ImGui::EndPopup();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
    flog::outMtx.lock();
    for (int i = 0; i < flog::logRecords.size(); i++) {
        auto item = flog::logRecords[i];
        // Normally you would store more information in your item than just a string.
        // (e.g. make Items[] an array of structure, store color/type etc.)
        ImVec4 color;
        bool has_color = false;
        if (item.typ == flog::TYPE_ERROR) {
            color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            has_color = true;
        }
        else if (item.typ == flog::TYPE_WARNING) {
            color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f);
            has_color = true;
        }
        if (has_color)
            ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(item.message.c_str());
        if (has_color)
            ImGui::PopStyleColor();
    }

    flog::outMtx.unlock();
    ImGui::SetScrollHereY(1.0f);

    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::Separator();

    ImGui::End();
}

void initBrownAIClient(std::function<void(const std::string&, const std::string&)> callback) {
    static bool initialized = false;
    if (initialized) return;

    brownAIClient.init("brownai.san.systems:8080"); // Change this to your Brown AI server address
    brownAIClient.onCommandReceived = [callback](const std::string& command) {
        // Process the command received from Brown AI server
        callback("", command);
    };
    brownAIClient.start();
    initialized = true;
}

void MainWindow::displayVariousWindows() {
    if (demoWindow) {
        lockWaterfallControls = true;
        ImGuiStyle styleBefore = ImGui::GetStyle();
        ImGui::ShowDemoWindow();
        ImGuiStyle styleAfter = ImGui::GetStyle();
        if (memcmp(&styleBefore, &styleAfter, sizeof(ImGuiStyle)) != 0) {
            thememenu::saveStyle();
        }
    }
    if (logWindow) {
        lockWaterfallControls = true;
        ShowLogWindow();
    }
    if (sigpath::iqFrontEnd.detectorPreprocessor.isEnabled()) {
        auto& toPlot = sigpath::iqFrontEnd.detectorPreprocessor.sigs_smoothed;
        if (!toPlot.empty()) {
            ImGui::SetNextWindowSize(ImVec2(1800, 300), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Signal Detector Output")) {
                if (ImPlot::BeginPlot("##DetectorPlot", ImVec2(-1, -1))) {
                    ImPlot::SetupAxes("Frequency Bin", "Smoothed Value");
                    ImPlot::PlotLine("Smoothed Signal", toPlot.data(), toPlot.size());
                    ImPlot::EndPlot();
                }
            }
            ImGui::End();
        }
    }
    if (showCredits) {
        lockWaterfallControls = true;
    }
}


void MainWindow::draw() {
    auto ctm = currentTimeNanos();
    ImGui::WaterfallVFO* vfo;
    this->preDraw(&vfo);
    handleDemodulatorShortcuts();
    ImGui::Begin("Main", NULL, WINDOW_FLAGS);
    ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    // To Bar
    // ImGui::BeginChild("TopBarChild", ImVec2(0, 49.0f * style::uiScale), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImVec2 btnSize(30 * style::uiScale, 30 * style::uiScale);
    // Portrait: the top bar is a horizontally scrollable strip, the frequency gets its own row
    bool portrait = isPortraitLayout();
    if (portrait) {
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4.0f * style::uiScale);
        ImGui::BeginChild("##sdrpp_portrait_strip", ImVec2(0, btnSize.y + 24.0f * style::uiScale), false, ImGuiWindowFlags_HorizontalScrollbar);
    }
    upperLineSkipFreq = portrait;
    ImGui::PushID(ImGui::GetID("sdrpp_menu_btn"));
    if (ImGui::ImageButton(icons::MENU, btnSize, ImVec2(0, 0), ImVec2(1, 1), 5, ImVec4(0, 0, 0, 0), textCol) || ImGui::IsKeyPressed(ImGuiKey_Menu, false)) {
        showMenu = !showMenu;
        core::configManager.acquire();
        core::configManager.conf["showMenu"] = showMenu;
        core::configManager.release(true);
    }
    ImGui::PopID();

    // Handle space key for microphone input
    static bool wasSpacePressed = false;
    static std::vector<dsp::stereo_t> micSamples;

    if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && sigpath::sinkManager.defaultInputAudio.hasInput()) {
        spacePressed = true;
        if (!wasSpacePressed) {
            sigpath::sinkManager.setAllMuted(true);
            // Start recording
            micSamples.clear();
            micStream.clearReadStop();
            micStream.clearWriteStop();
            sigpath::sinkManager.defaultInputAudio.bindStream(&micStream);
            wasSpacePressed = true;

            // Start mic stream thread if not already running
            if (!micThread) {
                micThreadRunning = true;
                micThread = std::make_shared<std::thread>([this]() {
                    SetThreadName("MicStreamReader");
                    while (micThreadRunning) {
                        int rd = micStream.read();
                        if (rd > 0) {
                            std::lock_guard<std::mutex> lck(micSamplesMutex);
                            micSamples.insert(micSamples.end(), micStream.readBuf, micStream.readBuf + rd);
                            micStream.flush();
                        }
                        else if (rd < 0) {
                            break;
                        }
                    }
                });
            }
            initBrownAIClient([this](const std::string& whisperResult, const std::string& result) {
                try {
                    // Parse JSON response
                    auto json = nlohmann::json::parse(result);

                    // Check for error
                    if (json.contains("error") && !json["error"].empty()) {
                        flog::error("Brown AI error: {}", json["error"].get<std::string>());
                        ImGui::InsertNotification({ ImGuiToastType_Error, 5000, "Brown AI error: %s", json["error"].get<std::string>().c_str() });
                        return;
                    }

                    // Extract command if present
                    std::string command = json.value("command", "");
                    if (command.empty()) {
                        flog::warn("Empty command received from Brown AI");
                        ImGui::InsertNotification({ ImGuiToastType_Warning, 5000, "Empty command received" });
                        return;
                    }

                    // Process the command
                    flog::info("Response from Brown AI: {}", command);
                    this->performDetectedLLMAction(whisperResult, command);
                }
                catch (const std::exception& e) {
                    flog::error("Failed to parse Brown AI response: {}", e.what());
                    ImGui::InsertNotification({ ImGuiToastType_Error, 5000, "Failed to parse Brown AI response" });
                }
            });
        }
    }

    mainThreadTasksMutex.lock();
    for (auto& task : mainThreadTasks) {
        task();
    }
    mainThreadTasks.clear();
    mainThreadTasksMutex.unlock();

    if (ImGui::IsKeyReleased(ImGuiKey_Space)) {
        spacePressed = false;
        if (wasSpacePressed) {
            flog::info("Space key released");
            sigpath::sinkManager.setAllMuted(false);
            // Stop recording
            sigpath::sinkManager.defaultInputAudio.unbindStream(&micStream);
            wasSpacePressed = false;

            // Stop mic stream thread
            if (micThread) {
                micThreadRunning = false;
                micStream.stopReader();
                if (micThread->joinable()) {
                    micThread->join();
                }
                micThread.reset();
            }

            // micSamples now contains all recorded audio
            flog::info("Recorded {} microphone samples", (int)micSamples.size());
            auto [speechFile, extension] = mpeg::produce_speech_file_for_whisper(micSamples, 48000);
            flog::info("Compressed to {} bytes as {}", (int)speechFile.size(), extension);
            brownAIClient.sendAudioData(speechFile);
        }
    }

    ImGui::SameLine();

    this->drawUpperLine(vfo);
    // Note: this is what makes the vertical size correct, needs to be fixed
    ImGui::SameLine();

    // ImGui::EndChild();

    // Logo button
    if (!portrait) {
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - (48 * style::uiScale));
        ImGui::SetCursorPosY(10.0f * style::uiScale);
    }
    if (ImGui::ImageButton(icons::LOGO, ImVec2(32 * style::uiScale, 32 * style::uiScale), ImVec2(0, 0), ImVec2(1, 1), 0)) {
        showCredits = true;
    }
    if (portrait) {
        handleTouchScrollX(volumeSliderMin, volumeSliderMax);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * style::uiScale);
        gui::freqSelect.draw();
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (showCredits) {
            showCredits = false;
            lockWaterfallControls = true;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showCredits = false;
    }

    // Reset waterfall lock
    lockWaterfallControls = showCredits;

    // Handle menu resize
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 mousePos = ImGui::GetMousePos();
    if (!lockWaterfallControls && showMenu && !portrait && ImGui::GetTopMostPopupModal() == NULL) {
        float curY = ImGui::GetCursorPosY();
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (grabbingMenu) {
            newWidth = mousePos.x;
            newWidth = std::clamp<float>(newWidth, 250, winSize.x - 250);
            ImGui::GetForegroundDrawList()->AddLine(ImVec2(newWidth, curY), ImVec2(newWidth, winSize.y - 10), ImGui::GetColorU32(ImGuiCol_SeparatorActive));
        }
        if (mousePos.x >= newWidth - (2.0f * style::uiScale) && mousePos.x <= newWidth + (2.0f * style::uiScale) && mousePos.y > curY) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (click) {
                grabbingMenu = true;
            }
        }
        else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        }
        if (!down && grabbingMenu) {
            grabbingMenu = false;
            menuWidth = newWidth;
            core::configManager.acquire();
            core::configManager.conf["menuWidth"] = menuWidth;
            core::configManager.release(true);
        }
    }

    // Process menu keybinds
    displaymenu::checkKeybinds();

    // A main view other than the receiver also takes the waterfall controls column
    bool viewShown = mainViewShown();
    float controlsWidth = (viewShown || portrait) ? 1.0f : (60.0f * style::uiScale);
    // Portrait: an open menu takes the whole width instead of sharing it with the waterfall
    bool portraitMenuOnly = portrait && showMenu;
    float menuColWidth = portraitMenuOnly ? (winSize.x - 2.0f) : (float)menuWidth;

    // Left Column
    if (showMenu) {
        ImGui::Columns(3, "WindowColumns", false);
        ImGui::SetColumnWidth(0, menuColWidth);
        ImGui::SetColumnWidth(1, portraitMenuOnly ? 1.0f : std::max<int>(winSize.x - menuColWidth - controlsWidth, 100.0f * style::uiScale));
        ImGui::SetColumnWidth(2, controlsWidth);

        ImGui::BeginChild("Left Column");
        if (gui::menu.draw(firstMenuRender)) {
            core::configManager.acquire();
            json arr = json::array();
            for (int i = 0; i < gui::menu.order.size(); i++) {
                arr[i]["name"] = gui::menu.order[i].name;
                arr[i]["open"] = gui::menu.order[i].open;
            }
            core::configManager.conf["menuElements"] = arr;

            // Update enabled and disabled modules
            for (auto [_name, inst] : core::moduleManager.instances) {
                if (!core::configManager.conf["moduleInstances"].contains(_name)) { continue; }
                core::configManager.conf["moduleInstances"][_name]["enabled"] = inst.instance->isEnabled();
            }

            core::configManager.release(true);
        }
        if (startedWithMenuClosed) {
            startedWithMenuClosed = false;
        }
        else {
            firstMenuRender = false;
        }


        this->drawDebugMenu();

        ImGui::EndChild();
    }
    else {
        // When hiding the menu bar
        ImGui::Columns(3, "WindowColumns", false);
        ImGui::SetColumnWidth(0, 8 * style::uiScale);
        ImGui::SetColumnWidth(1, winSize.x - (8 * style::uiScale) - controlsWidth);
        ImGui::SetColumnWidth(2, controlsWidth);
    }

    // Right Column
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::NextColumn();
    ImGui::PopStyleVar();

    this->displayVariousWindows();


    ImVec2 wfSize = ImVec2(0, 0);
    if (!bottomWindows.empty()) {
        wfSize.y -= bottomWindows[0].size.y;
    }
    if (portraitMenuOnly) {
        // Menu only: nothing else fits next to it in portrait
    }
    else {
        drawMainViewTabs();
    }
    if (portraitMenuOnly) {
        // the waterfall is not drawn while the menu covers the screen
    }
    else if (viewShown) {
        std::function<void()> viewDraw;
        for (auto& v : mainViews) {
            if (v.name == selectedMainView) { viewDraw = v.drawFunc; }
        }
        ImGui::BeginChild("MainView", wfSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (viewDraw) { viewDraw(); }
        ImGui::EndChild();
    }
    else {
        ImGui::BeginChild("Waterfall", wfSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetScrollX(0.0f);
        ImGui::SetScrollY(0.0f);

        gui::waterfall.draw();
        onWaterfallDrawn.emit(GImGui);

        ImGui::EndChild();

        this->handleWaterfallInput(vfo);
    }

    ImGui::NextColumn();
    ImGui::BeginChild("WaterfallControls");
#ifdef __ANDROID__
    if (displaymenu::showBattery) {
        displaymenu::currentBatteryLevel = backend::getBatteryLevel();
        ImGui::Text("B%s%%", displaymenu::currentBatteryLevel.c_str());
    }
#endif

    static int sliderDynamicAdjustmentY = 0;

    ImVec2 wfSliderSize((displaymenu::phoneLayout ? 40.0 : 20.0) * style::uiScale, (displaymenu::phoneLayout ? 100.0 : 140.0) * style::uiScale - sliderDynamicAdjustmentY);
    const int MIN_SLIDER_HEIGHT = 10;
    if (wfSliderSize.y < MIN_SLIDER_HEIGHT) {
        wfSliderSize.y = MIN_SLIDER_HEIGHT; // Prevents dynamic adjustment too large;
    }
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Zoom").x / 2.0));
    ImGui::TextUnformatted("Zoom");
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - wfSliderSize.x / 2);

#ifdef __ANDROID__
    if (touchVSliderFloat("##_7_", wfSliderSize, &bw, 1.0, 0.0)) {
#else
    if (ImGui::VSliderFloat("##_7_", wfSliderSize, &bw, 1.0, 0.0, "")) {
#endif
        core::configManager.acquire();
        core::configManager.conf["zoomBw"] = bw;
        core::configManager.release(true);
        updateWaterfallZoomBandwidth(bw);
    }


    auto addMaxSlider = [&]() {
        ImGui::NewLine();
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Max").x / 2.0));
        ImGui::TextUnformatted("Max");
        ImGui::SameLine();
        ImVec2 textSize = ImGui::CalcTextSize("Max");
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Max").x / 2.0));
        if (ImGui::InvisibleButton("##max_button_auto", textSize)) {
            const std::pair<int, int>& range = gui::waterfall.autoRange();
            if (range.first == 0 && range.second == 0) {
                // bad case
            }
            else {
                fftMin = range.first;
                fftMax = range.second;
            }
        }
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - wfSliderSize.x / 2);
#ifdef __ANDROID__
        if (touchVSliderFloat("##_8_", wfSliderSize, &fftMax, 0.0, -180.0f)) {
#else
        if (ImGui::VSliderFloat("##_8_", wfSliderSize, &fftMax, 0.0, -180.0f, "")) {
#endif
            fftMax = std::max<float>(fftMax, fftMin + 10);
            core::configManager.acquire();
            core::configManager.conf["max"] = fftMax;
            core::configManager.release(true);
            gui::waterfall.setFFTMax(fftMax);
            gui::waterfall.setWaterfallMax(fftMax);
        }
    };

    auto addMinSlider = [&]() {
        ImGui::NewLine();
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Min").x / 2.0));
        ImGui::TextUnformatted("Min");
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - wfSliderSize.x / 2);
        ImGui::SetItemUsingMouseWheel();
#ifdef __ANDROID__
        if (touchVSliderFloat("##_9_", wfSliderSize, &fftMin, 0.0, -200.0f)) {
#else
        if (ImGui::VSliderFloat("##_9_", wfSliderSize, &fftMin, 0.0, -200.0f, "")) {
#endif
            fftMin = std::min<float>(fftMax - 10, fftMin);
            core::configManager.acquire();
            core::configManager.conf["min"] = fftMin;
            core::configManager.release(true);
            gui::waterfall.setFFTMin(fftMin);
            gui::waterfall.setWaterfallMin(fftMin);
        }
    };

    if (displaymenu::phoneLayout) {
        // min slider is used much more often, if you ask me. So on small screen there should be no need to scroll down to operate it.
        addMinSlider();
        addMaxSlider();
    }
    else {
        addMaxSlider();
        addMinSlider();
    }
    const ImVec2 remainder = ImGui::GetContentRegionAvail();
    if (remainder.y <= 0 && wfSliderSize.y > MIN_SLIDER_HEIGHT) {
        // this fits the sliders to any height.
        sliderDynamicAdjustmentY++;
    }

    ImGui::EndChild();

    this->drawBottomWindows(0);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 100.f / 255.f));
    ImGui::PushFont(style::notificationFont);
    ImGui::RenderNotifications();
    ImGui::PopFont();
    ImGui::PopStyleVar(1); // Don't forget to Pop()
    ImGui::PopStyleColor(1);


    ImGui::End();

    if (showCredits) {
        credits::show();
    }

    // Draw listening popup if space is pressed
    if (spacePressed) {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.7f);
        if (ImGui::Begin("##ListeningPopup", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav)) {
            ImGui::Text("Listening... Please give your command...");
            ImGui::End();
        }
    }

    lastDrawTime = (currentTimeNanos() - ctm) / 1000;
}

void MainWindow::handleDemodulatorShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    drawKeyHintAndFrequencyEntry();
    if (mainViewShown()) {
        return;
    }
    if (io.WantTextInput || io.KeyCtrl || io.KeyAlt || io.KeySuper || gui::waterfall.selectedVFO.empty()) {
        return;
    }
    // Enter: type the frequency to tune to
    if (!io.KeyShift && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) && !ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId)) {
        freqEntryOpenRequest = true;
        return;
    }
    if (!core::modComManager.interfaceExists(gui::waterfall.selectedVFO) || core::modComManager.getModuleName(gui::waterfall.selectedVFO) != "radio") {
        return;
    }

    // Shift+0 / Shift+9: squelch up / down (1 dB, repeats while held)
    if (io.KeyShift && (ImGui::IsKeyPressed(ImGuiKey_0, true) || ImGui::IsKeyPressed(ImGuiKey_9, true))) {
        float level = -100.0f;
        core::modComManager.callInterface(gui::waterfall.selectedVFO, RADIO_IFACE_CMD_GET_SQUELCH_LEVEL, NULL, &level);
        level = std::clamp(level + (ImGui::IsKeyPressed(ImGuiKey_0, true) ? 1.0f : -1.0f), -100.0f, 0.0f);
        bool enabled = true;
        core::modComManager.callInterface(gui::waterfall.selectedVFO, RADIO_IFACE_CMD_SET_SQUELCH_ENABLED, &enabled, NULL);
        core::modComManager.callInterface(gui::waterfall.selectedVFO, RADIO_IFACE_CMD_SET_SQUELCH_LEVEL, &level, NULL);
        char hint[48];
        snprintf(hint, sizeof(hint), "Squelch %.0f dB", level);
        showKeyHint(hint);
        return;
    }

    // One key per mode, no Shift: A AM, F/W WFM, N NFM, D DSB, U USB, L LSB, C CW, R RAW,
    // S DSD, O oldDSD (the last two exist only with ch_extravhf_decoder loaded)
    int mode = -1;
    if (io.KeyShift) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        mode = RADIO_IFACE_MODE_AM;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_F, false) || ImGui::IsKeyPressed(ImGuiKey_W, false)) {
        mode = RADIO_IFACE_MODE_WFM;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        mode = RADIO_IFACE_MODE_NFM;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        mode = RADIO_IFACE_MODE_DSB;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_U, false)) {
        mode = RADIO_IFACE_MODE_USB;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_L, false)) {
        mode = RADIO_IFACE_MODE_LSB;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        mode = RADIO_IFACE_MODE_CW;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        mode = RADIO_IFACE_MODE_RAW;
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        mode = 0x1301; // RADIO_DEMOD_DSD (ch_extravhf_decoder)
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        mode = 0x1302; // RADIO_DEMOD_OLDDSD (ch_extravhf_decoder)
    }

    if (mode >= 0) {
        core::modComManager.callInterface(gui::waterfall.selectedVFO, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
        int now = -1;
        core::modComManager.callInterface(gui::waterfall.selectedVFO, RADIO_IFACE_CMD_GET_MODE, NULL, &now);
        static const std::map<int, const char*> modeNames = {
            { RADIO_IFACE_MODE_NFM, "NFM" }, { RADIO_IFACE_MODE_WFM, "WFM" }, { RADIO_IFACE_MODE_AM, "AM" }, { RADIO_IFACE_MODE_DSB, "DSB" },
            { RADIO_IFACE_MODE_USB, "USB" }, { RADIO_IFACE_MODE_CW, "CW" }, { RADIO_IFACE_MODE_LSB, "LSB" }, { RADIO_IFACE_MODE_RAW, "RAW" },
            { 0x1301, "DSD" }, { 0x1302, "oldDSD" }
        };
        auto it = modeNames.find(mode);
        std::string name = it != modeNames.end() ? it->second : "mode";
        showKeyHint(now == mode ? name : name + " not available");
    }
}

void MainWindow::setPlayState(bool _playing) {
    if (_playing == playing) { return; }
    if (_playing) {
        if (playButtonLocked || !selectedSourceReady()) { return; }
        sigpath::iqFrontEnd.flushInputBuffer();
        sigpath::sourceManager.start();
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        playing = true;
        onPlayStateChange.emit(true);
    }
    else {
        playing = false;
        onPlayStateChange.emit(false);
        sigpath::sourceManager.stop();
        sigpath::iqFrontEnd.flushInputBuffer();
    }
}

bool MainWindow::sdrIsRunning() {
    return playing;
}


int hzRange(double freq) {
    return round(log10(freq) / 3);
}

void MainWindow::performDetectedLLMAction(const std::string& whisperResult, std::string command) {
    std::string detectedCommand;
    flog::info("Whisper result: {}", whisperResult);
    flog::info("Detected command: {}", detectedCommand);

    if (command.empty()) {
        return;
    }
    if (command[0] == ':') { // just lazy
        command = "COMMAND" + command;
    }

    // Split command into lines
    std::istringstream stream(command);
    std::string line;
    std::string lastCaps;
    while (std::getline(stream, line)) {
        // Find last line containing "COMMAND"
        size_t pos = line.find("COMMAND");
        if (pos != std::string::npos) {
            // Extract everything after "COMMAND"
            detectedCommand = line.substr(pos + 8); // and colon
        }
        // check, if line is all capitals, assitn to lastCaps
        bool allCaps = true;
        for (char c : line) {
            if (!std::isupper(c) && std::isalpha(c)) {
                allCaps = false;
                break;
            }
        }
        if (allCaps) {
            lastCaps = line;
        }
    }

    if (detectedCommand.empty() && lastCaps.empty()) {
        flog::info("Invalid LLM reply, no command: {}", command);
        ImGui::InsertNotification({ ImGuiToastType_Info, 5000, "Invalid LLM reply, no command" });
    }

    if (!detectedCommand.empty()) {
        detectedCommand.erase(0, detectedCommand.find_first_not_of(' '));
        detectedCommand.erase(detectedCommand.find_last_not_of(' ') + 1);
    }
    else {
        detectedCommand = lastCaps;
    }
    // Trim leading/trailing whitespace


    // Split into parts by spaces
    std::vector<std::string> parts;
    splitStringV(detectedCommand, " ", parts);
    while (parts.size()) {
        if (parts[0] == "") {
            parts.erase(parts.begin(), parts.begin() + 1);
        }
        else {
            break;
        }
    }

    if (parts[0] == "VOLUME_UP") {
        if (sigpath::sinkManager.recentStreeam) {
            auto volume = sigpath::sinkManager.recentStreeam->getVolume();
            volume += 0.2;
            if (volume > 1.0) {
                volume = 1.0;
            }
            sigpath::sinkManager.recentStreeam->setVolume(volume);
        }
    }
    if (parts[0] == "VOLUME_DOWN") {
        if (sigpath::sinkManager.recentStreeam) {
            auto volume = sigpath::sinkManager.recentStreeam->getVolume();
            volume -= 0.2;
            if (volume <= 0) {
                volume = 0;
            }
            sigpath::sinkManager.recentStreeam->setVolume(volume);
        }
    }
    auto vfo = gui::waterfall.selectedVFO;
    if (parts[0] == "USB") {
        auto mode = RADIO_IFACE_MODE_USB;
        core::modComManager.callInterface(vfo, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
    }
    if (parts[0] == "LSB") {
        auto mode = RADIO_IFACE_MODE_LSB;
        core::modComManager.callInterface(vfo, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
    }
    if (parts[0] == "AM") {
        auto mode = RADIO_IFACE_MODE_AM;
        core::modComManager.callInterface(vfo, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
    }
    if (parts[0] == "FM") {
        auto mode = RADIO_IFACE_MODE_NFM;
        core::modComManager.callInterface(vfo, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
    }
    if (parts[0] == "STOP") {
        setPlayState(false);
    }
    if (parts[0] == "START") {
        setPlayState(true);
    }
    if (parts[0] == "MUTE") {
        if (sigpath::sinkManager.recentStreeam) {
            sigpath::sinkManager.recentStreeam->volumeAjust.setMuted(!sigpath::sinkManager.recentStreeam->volumeAjust.getMuted());
        }
    }
    if (parts[0] == "FREQ") {
        float freq = atof(parts[1].c_str());
        float mult = 1;
        if (parts.size() > 2) {
            if (parts[2][0] == 'K' || parts[2][0] == 'k') {
                mult = 1e3;
            }
            if (parts[2][0] == 'M' || parts[2][0] == 'm') {
                mult = 1e6;
            }
        }
        float currentFrequency = gui::waterfall.getCenterFrequency();
        double intPartF;
        float frac = std::modf((double)freq, &intPartF);
        long long intPart = (long long)intPartF;
        double newFreq = 0;
        if (frac != 0 && mult == 1) { // e.g 1.7
            mult = 1e6;               // mhz
            newFreq = mult * freq;    // make 1.7 mhz
        }
        else if (mult * freq < 100000 && mult == 1e3) { // 1200 khz spoken as 1.200 khz
            mult = 1e6;
            newFreq = mult * freq;
        }
        else if (mult == 1e6 || mult == 1e3) { // exact mhz freq
            newFreq = mult * freq;
        }
        else {
            while (hzRange(freq) < hzRange(currentFrequency)) {
                freq *= 1000;
            }
            while (hzRange(freq) > hzRange(currentFrequency)) {
                freq /= 1000;
            }
            newFreq = freq;
        }
        tuner::tune(tuningMode, gui::waterfall.selectedVFO, newFreq);
    }

    ImGui::InsertNotification({ ImGuiToastType_Info, 5000, "Voice command: %s", detectedCommand.c_str() });
}

bool MainWindow::isPlaying() {
    return playing;
}

void MainWindow::updateWaterfallZoomBandwidth(float bw) {
    ImGui::WaterfallVFO* vfo = NULL;
    if (gui::waterfall.selectedVFO != "") {
        vfo = gui::waterfall.vfos[gui::waterfall.selectedVFO];
    }
    double factor = (double)bw * (double)bw;

    // Map 0.0 -> 1.0 to 1000.0 -> bandwidth
    double wfBw = gui::waterfall.getBandwidth();
    double delta = wfBw - 1000.0;
    double finalBw = std::min<double>(1000.0 + (factor * delta), wfBw);

    gui::waterfall.setViewBandwidth(finalBw * gui::waterfall.getUsableSpectrumRatio());
    if (vfo != NULL) {
        gui::waterfall.setViewOffset(vfo->centerOffset); // center vfo on screen
    }
}

void MainWindow::handleWaterfallInput(ImGui::WaterfallVFO* vfo) {
    if (!lockWaterfallControls && ImGui::GetTopMostPopupModal() == NULL) {
        // Handle arrow keys
        if (vfo != NULL && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            bool freqChanged = false;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset - vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (freqChanged) {
                core::configManager.acquire();
                core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
                if (vfo != NULL) {
                    core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
                }
                core::configManager.release(true);
            }
        }

        // Handle scrollwheel
        int wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0 && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            double nfreq;
            if (vfo != NULL) {
                // Select factor depending on modifier keys
                double interval;
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
                    interval = vfo->snapInterval * 10.0;
                }
                else if (ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
                    interval = vfo->snapInterval * 0.1;
                }
                else {
                    interval = vfo->snapInterval;
                }

                nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + (interval * wheel);
                nfreq = roundl(nfreq / interval) * interval;
            }
            else {
                nfreq = gui::waterfall.getCenterFrequency() - (gui::waterfall.getViewBandwidth() * wheel / 20.0);
            }
            tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
            gui::freqSelect.setFrequency(nfreq);
            core::configManager.acquire();
            core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
            if (vfo != NULL) {
                core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            }
            core::configManager.release(true);
        }
    }
}

void MainWindow::drawBottomWindows(int dy) {
    if (!showMenu) {
        if (bottomWindows.size() > 0) {
            updateBottomWindowLayout();
            for (int i = 0; i < bottomWindows.size(); i++) {
                ImGui::Begin(("bottomwindow_" + bottomWindows[i].name).c_str(),
                             NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
                ImGui::SetWindowPos(ImVec2(bottomWindows[i].loc.x, gui::waterfall.wfMax.y + dy));
                ImGui::SetWindowSize(ImVec2(bottomWindows[i].size.x, bottomWindows[i].size.y));
                bottomWindows[i].drawFunc();
                ImGui::End();
            }
        }
    }
}

void MainWindow::registerMainView(std::string name, std::function<void()> drawFunc) {
    for (auto& v : mainViews) {
        if (v.name == name) {
            v.drawFunc = drawFunc;
            return;
        }
    }
    mainViews.push_back(MainView{ name, drawFunc });
}

void MainWindow::unregisterMainView(std::string name) {
    mainViews.erase(std::remove_if(mainViews.begin(), mainViews.end(), [&](const MainView& v) { return v.name == name; }), mainViews.end());
    if (selectedMainView == name) {
        selectMainView("");
    }
}

void MainWindow::selectMainView(std::string name) {
    if (name == selectedMainView) { return; }
    selectedMainView = name;
}

std::string MainWindow::getSelectedMainView() {
    return selectedMainView;
}

bool MainWindow::mainViewShown() {
    if (selectedMainView.empty()) { return false; }
    for (auto& v : mainViews) {
        if (v.name == selectedMainView) { return true; }
    }
    return false;
}

void MainWindow::drawMainViewTabs() {
    bool portrait = isPortraitLayout();
    if (mainViews.empty() && !portrait) { return; }
    // Plain buttons instead of an ImGui tab bar: they switch on the first
    // click/tap and need no state synchronisation with programmatic selection.
    ImVec4 activeCol = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    ImVec4 idleCol = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    std::string clicked = selectedMainView;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * style::uiScale, 4.0f * style::uiScale));
    auto viewButton = [&](const char* label, const std::string& viewName) {
        bool selected = (selectedMainView == viewName);
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? activeCol : idleCol);
        if (ImGui::Button(label) && !selected) { clicked = viewName; }
        ImGui::PopStyleColor();
    };
    if (!mainViews.empty()) {
        viewButton("Receiver##sdrpp_view_rx", "");
        for (auto& v : mainViews) {
            ImGui::SameLine();
            viewButton((v.name + "##sdrpp_view").c_str(), v.name);
        }
    }
    // Portrait: the zoom / max / min sliders are hidden behind a button
    bool receiverView = selectedMainView.empty() || !mainViewShown();
    if (portrait && receiverView) {
        if (!mainViews.empty()) { ImGui::SameLine(); }
        ImGui::PushStyleColor(ImGuiCol_Button, portraitSlidersOpen ? activeCol : idleCol);
        if (ImGui::Button("Zoom / Min / Max##sdrpp_psliders")) { portraitSlidersOpen = !portraitSlidersOpen; }
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    if (clicked != selectedMainView) { selectMainView(clicked); }

    if (portrait && receiverView && portraitSlidersOpen) {
        float w = (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##sdrpp_pzoom", &bw, 1.0f, 0.0f, "Zoom")) {
            core::configManager.acquire();
            core::configManager.conf["zoomBw"] = bw;
            core::configManager.release(true);
            updateWaterfallZoomBandwidth(bw);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##sdrpp_pmin", &fftMin, -200.0f, 0.0f, "Min %.0f dB")) {
            fftMin = std::min<float>(fftMax - 10, fftMin);
            core::configManager.acquire();
            core::configManager.conf["min"] = fftMin;
            core::configManager.release(true);
            gui::waterfall.setFFTMin(fftMin);
            gui::waterfall.setWaterfallMin(fftMin);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("##sdrpp_pmax", &fftMax, -180.0f, 0.0f, "Max %.0f dB")) {
            fftMax = std::max<float>(fftMax, fftMin + 10);
            core::configManager.acquire();
            core::configManager.conf["max"] = fftMax;
            core::configManager.release(true);
            gui::waterfall.setFFTMax(fftMax);
            gui::waterfall.setWaterfallMax(fftMax);
        }
    }
}

void MainWindow::addBottomWindow(std::string name, std::function<void()> drawFunc) {
    int foundIndex = -1;
    for (int i = 0; i < bottomWindows.size(); i++) {
        if (bottomWindows[i].name == name) {
            foundIndex = i;
        }
    }
    if (foundIndex != -1) {
        bottomWindows[foundIndex].drawFunc = drawFunc;
    }
    else {
        bottomWindows.emplace_back(ButtomWindow{ name, drawFunc });
    }
    updateBottomWindowLayout();
}

void MainWindow::updateBottomWindowLayout() {
    auto fullWidth = gui::waterfall.fftAreaMax.x - gui::waterfall.fftAreaMin.x;
    auto fullHeight = ImGui::GetIO().DisplaySize.y;
    int nWindows = bottomWindows.size();
    if (nWindows < 5) {
        nWindows = 5;
    }
    auto size = fullWidth / nWindows;
    auto scan = 0;
    for (int i = 0; i < bottomWindows.size(); i++) {
        bottomWindows[i].loc.x = scan;
        bottomWindows[i].loc.y = 0;
        bottomWindows[i].size.x = size;
        bottomWindows[i].size.y = fullHeight / 5;
        scan += size;
    }
}

bool MainWindow::hasBottomWindow(std::string name) {
    for (int i = 0; i < bottomWindows.size(); i++) {
        if (bottomWindows[i].name == name) {
            return true;
        }
    }
    return false;
}

void MainWindow::removeBottomWindow(std::string name) {
    for (int i = 0; i < bottomWindows.size(); i++) {
        if (bottomWindows[i].name == name) {
            bottomWindows.erase(bottomWindows.begin() + i);
            updateBottomWindowLayout();
            return;
        }
    }
}

void MainWindow::drawDebugMenu() {
    if (ImGui::CollapsingHeader("Debug")) {
        float deltaTimeSec = ImGui::GetIO().DeltaTime;
        ImGui::Text("Frame time: %.3f ms/frame", deltaTimeSec * 1000.0f);
        ImGui::Text("Draw time: imgui:%lld glwf:%lld us busy=%d%%", lastDrawTime, lastDrawTimeBackend, (int)((double)(lastDrawTime + lastDrawTimeBackend) * 100.0 / (deltaTimeSec * 1e6)));
        ImGui::Text("Framerate: %.1f FPS", ImGui::GetIO().Framerate);
        ImGui::Text("Center Frequency: %.0f Hz", gui::waterfall.getCenterFrequency());
        ImGui::Text("Source name: %s", sourceName.c_str());
        ImGui::Checkbox("Show demo window", &demoWindow);
        ImGui::Checkbox("Show log", &logWindow);
        ImGui::Text("ImGui version: %s", ImGui::GetVersion());

        // ImGui::Checkbox("Bypass buffering", &sigpath::iqFrontEnd.inputBuffer.bypass);

        // ImGui::Text("Buffering: %d", (sigpath::iqFrontEnd.inputBuffer.writeCur - sigpath::iqFrontEnd.inputBuffer.readCur + 32) % 32);

        if (ImGui::Button("Test Bug")) {
            flog::error("Will this make the software crash?");
        }

        if (ImGui::Button("Testing something")) {
            addBottomWindow("bw1", []() {
                ImGui::Text("Hello world1");
            });
            addBottomWindow("bw2", []() {
                ImGui::Text("Hello world2");
            });
            addBottomWindow("bw3", []() {
                ImGui::Text("Hello world3");
            });
        }
#ifdef __ANDROID__
        if (ImGui::Button("Perm.Request")) {
            backend::doPermissionsDialogs();
        }
#endif

        ImGui::Checkbox("WF Single Click", &gui::waterfall.VFOMoveSingleClick);
        onDebugDraw.emit(GImGui);


        ImGui::Spacing();
    }
}
