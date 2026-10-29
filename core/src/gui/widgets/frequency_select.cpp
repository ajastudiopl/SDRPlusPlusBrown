#include <gui/widgets/frequency_select.h>
#include <config.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <backend.h>
#include <utils/hrfreq.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include "gui/menus/display.h"

namespace gui {
    Event<VFOFrequencyChange> vfoFrequencyChanged;
}

bool isInArea(ImVec2 val, ImVec2 min, ImVec2 max) {
    return val.x >= min.x && val.x < max.x && val.y >= min.y && val.y < max.y;
}

FrequencySelect::FrequencySelect() {
}

static int getNumberOfDigits() {
    return displaymenu::phoneLayout ? 10 : 12;
}

void FrequencySelect::init() {
    for (int i = 0; i < getNumberOfDigits(); i++) {
        digits[i] = 0;
    }
}

void FrequencySelect::onPosChange() {
    ImVec2 digitSz = ImGui::CalcTextSize("0");
    ImVec2 commaSz = ImGui::CalcTextSize(".");
    int digitHeight = digitSz.y;
    int digitWidth = digitSz.x;
    int commaOffset = 0;
    for (int i = 0; i < getNumberOfDigits(); i++) {
        digitTopMins[i] = ImVec2(widgetPos.x + (i * digitWidth) + commaOffset, widgetPos.y);
        digitBottomMins[i] = ImVec2(widgetPos.x + (i * digitWidth) + commaOffset, widgetPos.y + (digitHeight / 2));

        digitTopMaxs[i] = ImVec2(widgetPos.x + (i * digitWidth) + commaOffset + digitWidth, widgetPos.y + (digitHeight / 2));
        digitBottomMaxs[i] = ImVec2(widgetPos.x + (i * digitWidth) + commaOffset + digitWidth, widgetPos.y + digitHeight);

        if (((getNumberOfDigits() - 1 - i) % 3 == 0) && i < (getNumberOfDigits()-1)) {
            commaOffset += commaSz.x;
        }
    }
}

void FrequencySelect::incrementDigit(int i) {
    if (i < 0) {
        return;
    }
    if (digits[i] < 9) {
        digits[i]++;
    }
    else {
        digits[i] = 0;
        incrementDigit(i - 1);
    }
    frequencyChanged = true;
}

void FrequencySelect::decrementDigit(int i) {
    if (i < 0) {
        return;
    }
    if (digits[i] > 0) {
        digits[i]--;
    }
    else {
        if (i == 0) { return; }

        // Check if there are non zero digits afterwards
        bool otherNoneZero = false;
        for (int j = i - 1; j >= 0; j--) {
            if (digits[j] > 0) {
                otherNoneZero = true;
                break;
            }
        }
        if (!otherNoneZero) { return; }

        digits[i] = 9;
        decrementDigit(i - 1);
    }
    frequencyChanged = true;
}

void FrequencySelect::moveCursorToDigit(int i) {
    double xpos, ypos;
    backend::getMouseScreenPos(xpos, ypos);
    double nxpos = (digitTopMaxs[i].x + digitTopMins[i].x) / 2.0;
    backend::setMouseScreenPos(nxpos, ypos);
}

void FrequencySelect::draw() {
    auto window = ImGui::GetCurrentWindow();
    auto io = ImGui::GetIO();
    widgetPos = ImGui::GetWindowContentRegionMin();
    ImVec2 cursorPos = ImGui::GetCursorPos();
    widgetPos.x += window->Pos.x + cursorPos.x;
    ImGui::PushFont(style::bigFont);
    ImVec2 digitSz = ImGui::CalcTextSize("0");
    ImVec2 commaSz = ImGui::CalcTextSize(".");
    widgetPos.y = window->Pos.y + cursorPos.y - ((digitSz.y / 2.0f) - ceilf(15 * style::uiScale) - 5);

    if (widgetPos.x != lastWidgetPos.x || widgetPos.y != lastWidgetPos.y) {
        lastWidgetPos = widgetPos;
        onPosChange();
    }

    ImU32 disabledColor = ImGui::GetColorU32(ImGuiCol_Text, 0.3f);
    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

    
    int digitWidth = digitSz.x;
    int commaOffset = 0;
    float textOffset = 11.0f * style::uiScale;
    bool zeros = true;

    ImGui::ItemSize(ImRect(digitTopMins[0], ImVec2(digitBottomMaxs[getNumberOfDigits()-1].x + 15, digitBottomMaxs[getNumberOfDigits()-1].y)));

    for (int i = 0; i < getNumberOfDigits(); i++) {
        if (digits[i] != 0) {
            zeros = false;
        }
        snprintf(buf, sizeof buf, "%d", digits[i]);
        window->DrawList->AddText(ImVec2(widgetPos.x + (i * digitWidth) + commaOffset, widgetPos.y),
                                  zeros ? disabledColor : textColor, buf);
        if (((getNumberOfDigits() - 1 - i) % 3 == 0) && i < getNumberOfDigits()-1) {
            commaOffset += commaSz.x;
            window->DrawList->AddText(ImVec2(widgetPos.x + (i * digitWidth) + commaOffset + textOffset, widgetPos.y),
                                      zeros ? disabledColor : textColor, ".");
        }
    }

    if (!gui::mainWindow.lockWaterfallControls && ImGui::GetTopMostPopupModal() == NULL) {
        ImVec2 mousePos = ImGui::GetMousePos();
        bool leftClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool rightClick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        int mw = io.MouseWheel;
        bool onDigit = false;
        bool hovered = false;

        for (int i = 0; i < getNumberOfDigits(); i++) {
            onDigit = false;
            if (isInArea(mousePos, digitTopMins[i], digitTopMaxs[i])) {
                window->DrawList->AddRectFilled(digitTopMins[i], digitTopMaxs[i], IM_COL32(255, 0, 0, 75));
                if (leftClick) {
                    pressActive = true;
                    pressTop = true;
                    pressDigit = i;
                    pressPos = mousePos;
                    shuttleActive = false;
                }
                onDigit = true;
            }
            if (isInArea(mousePos, digitBottomMins[i], digitBottomMaxs[i])) {
                window->DrawList->AddRectFilled(digitBottomMins[i], digitBottomMaxs[i], IM_COL32(0, 0, 255, 75));
                if (leftClick) {
                    pressActive = true;
                    pressTop = false;
                    pressDigit = i;
                    pressPos = mousePos;
                    shuttleActive = false;
                }
                onDigit = true;
            }
            if (onDigit) {
                hovered = true;
                if (rightClick || (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
                    for (int j = i; j < getNumberOfDigits(); j++) {
                        digits[j] = 0;
                    }

                    frequencyChanged = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                    incrementDigit(i);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                    decrementDigit(i);
                }
                if ((ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && i > 0) {
                    moveCursorToDigit(i - 1);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && i < getNumberOfDigits()-1) {
                    moveCursorToDigit(i + 1);
                }

                auto chars = io.InputQueueCharacters;

                // For each keyboard characters, type it
                for (int j = 0; j < chars.Size; j++) {
                    if (chars[j] >= '0' && chars[j] <= '9') {
                        digits[i + j] = chars[j] - '0';
                        if ((i + j) < getNumberOfDigits()-1) { moveCursorToDigit(i + j + 1); }
                        frequencyChanged = true;
                    }
                }

                if (mw != 0) {
                    int count = abs(mw);
                    for (int j = 0; j < count; j++) {
                        mw > 0 ? incrementDigit(i) : decrementDigit(i);
                    }
                }
            }
        }
        digitHovered = hovered;
        updateShuttle(mousePos);

        if (isInArea(mousePos, digitTopMins[0], digitBottomMaxs[11])) {
            bool shortcutKey = io.ConfigMacOSXBehaviors ? (io.KeyMods == ImGuiKeyModFlags_Super) : (io.KeyMods == ImGuiKeyModFlags_Ctrl);
            bool ctrlOnly = (io.KeyMods == ImGuiKeyModFlags_Ctrl);
            bool shiftOnly = (io.KeyMods == ImGuiKeyModFlags_Shift);
            bool copy  = ((shortcutKey && ImGui::IsKeyPressed(ImGuiKey_C)) || (ctrlOnly  && ImGui::IsKeyPressed(ImGuiKey_Insert)));
            bool paste = ((shortcutKey && ImGui::IsKeyPressed(ImGuiKey_V)) || (shiftOnly && ImGui::IsKeyPressed(ImGuiKey_Insert)));
            if (copy) {
                // Convert the freqency to a string
                std::string freqStr = hrfreq::toString(frequency);

                // Write it to the clipboard
                ImGui::SetClipboardText(freqStr.c_str());
            }
            if (paste) {
                // Attempt to parse the clipboard as a number
                const char* clip = ImGui::GetClipboardText();

                // If the clipboard is not empty, attempt to parse it
                if (clip) {
                    double newFreq;
                    if (hrfreq::fromString(clip, newFreq)) {
                        setFrequency(abs(newFreq));
                        frequencyChanged = true;
                    }
                }
            }
        }
    }

    uint64_t freq = 0;
    for (int i = 0; i < getNumberOfDigits(); i++) {
        freq += digits[i] * pow(10, (getNumberOfDigits()-1) - i);
    }

    uint64_t orig = freq;
    freq = std::clamp<uint64_t>(freq, minFreq, maxFreq);
    if (freq != orig && limitFreq) {
        setFrequency(freq);
    }
    else {
        frequency = orig;
    }

    ImGui::PopFont();

    drawShuttleBar();

    ImGui::SetCursorPosX(digitBottomMaxs[getNumberOfDigits()-1].x + (17.0f * style::uiScale));
}


// Shuttle speed steps, in units of the pressed digit per second.
static const double SHUTTLE_MULT[5] = { 1, 3, 10, 30, 100 };
static const float SHUTTLE_DEAD_ZONE = 0.03f;   // fraction of the screen width
static const float SHUTTLE_FULL_SCALE = 0.45f;  // fraction of the screen width for the fastest step

void FrequencySelect::updateShuttle(ImVec2 mousePos) {
    if (!pressActive) { return; }
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float dx = mousePos.x - pressPos.x;
        if (!shuttleActive && fabsf(dx) > 10.0f * style::uiScale) {
            shuttleActive = true;
            shuttleAccum = 0;
        }
        if (!shuttleActive) { return; }

        // Distances are fractions of the screen width so it works in portrait as well.
        float width = std::max(1.0f, io.DisplaySize.x);
        float norm = fabsf(dx) / width;
        int level = 0;
        if (norm > SHUTTLE_DEAD_ZONE) {
            float stepSize = (SHUTTLE_FULL_SCALE - SHUTTLE_DEAD_ZONE) / 5.0f;
            level = std::clamp((int)((norm - SHUTTLE_DEAD_ZONE) / stepSize) + 1, 1, 5);
        }
        shuttleLevel = level;
        shuttleDir = (dx < 0) ? -1 : 1;
        double place = pow(10.0, (getNumberOfDigits() - 1) - pressDigit);
        shuttleRateHz = (level > 0) ? place * SHUTTLE_MULT[level - 1] : 0.0;

        shuttleAccum += shuttleDir * shuttleRateHz * std::min(io.DeltaTime, 0.1f);
        double whole = (shuttleAccum < 0) ? ceil(shuttleAccum) : floor(shuttleAccum);
        if (whole != 0) {
            shuttleAccum -= whole;
            double maxValue = pow(10.0, getNumberOfDigits()) - 1;
            double lo = 0, hi = maxValue;
            if (limitFreq) {
                lo = (double)minFreq;
                hi = std::min(hi, (double)maxFreq);
            }
            double target = std::clamp((double)frequency + whole, lo, hi);
            if ((uint64_t)target != frequency) {
                setFrequency((int64_t)target);
                frequencyChanged = true;
            }
        }
        return;
    }

    // Released: a press without a drag is a tap on the upper (+) or lower (-) half of the digit.
    if (!shuttleActive) {
        if (pressTop) { incrementDigit(pressDigit); }
        else { decrementDigit(pressDigit); }
    }
    pressActive = false;
    shuttleActive = false;
    shuttleLevel = 0;
    shuttleRateHz = 0;
    shuttleAccum = 0;
}

static void formatRate(char* out, size_t n, double hz, int dir) {
    const char* sign = (dir < 0) ? "-" : "+";
    if (hz >= 1e9) { snprintf(out, n, "%s%.0f GHz/s", sign, hz / 1e9); }
    else if (hz >= 1e6) { snprintf(out, n, "%s%.0f MHz/s", sign, hz / 1e6); }
    else if (hz >= 1e3) { snprintf(out, n, "%s%.0f kHz/s", sign, hz / 1e3); }
    else { snprintf(out, n, "%s%.0f Hz/s", sign, hz); }
}

// Speed bar under the panel: zero in the middle, the fill grows towards the finger.
void FrequencySelect::drawShuttleBar() {
    if (!pressActive || !shuttleActive) { return; }
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float scale = style::uiScale;
    int n = getNumberOfDigits();

    // Highlight of the grabbed digit
    dl->AddRectFilled(digitTopMins[pressDigit], digitBottomMaxs[pressDigit], IM_COL32(255, 200, 0, 90));

    float margin = 16.0f * scale;
    float barH = 18.0f * scale;
    float x0 = margin, x1 = io.DisplaySize.x - margin;
    // Well below the digits and the finger, so neither the values nor the bar hide each other
    float y0 = std::max(digitBottomMaxs[n - 1].y + 120.0f * scale, io.DisplaySize.y * 0.35f);
    float y1 = y0 + barH;
    float cx = (x0 + x1) / 2.0f;
    float half = (x1 - x0) / 2.0f;

    dl->AddRectFilled(ImVec2(x0 - 6 * scale, y0 - 34 * scale), ImVec2(x1 + 6 * scale, y1 + 10 * scale), IM_COL32(0, 0, 0, 170), 8.0f * scale);
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(60, 60, 60, 255), 4.0f * scale);

    // Dead zone and the five steps
    float dz = half * (SHUTTLE_DEAD_ZONE / SHUTTLE_FULL_SCALE);
    float stepW = (half - dz) / 5.0f;
    for (int s = -1; s <= 1; s += 2) {
        for (int k = 0; k <= 5; k++) {
            float tx = cx + s * (dz + k * stepW);
            dl->AddLine(ImVec2(tx, y0), ImVec2(tx, y1), IM_COL32(150, 150, 150, 255), 1.0f);
        }
    }
    if (shuttleLevel > 0) {
        float fill = dz + shuttleLevel * stepW;
        ImU32 col = (shuttleDir < 0) ? IM_COL32(60, 140, 255, 255) : IM_COL32(255, 150, 40, 255);
        if (shuttleDir < 0) { dl->AddRectFilled(ImVec2(cx - fill, y0), ImVec2(cx, y1), col, 4.0f * scale); }
        else { dl->AddRectFilled(ImVec2(cx, y0), ImVec2(cx + fill, y1), col, 4.0f * scale); }
    }
    dl->AddLine(ImVec2(cx, y0 - 4 * scale), ImVec2(cx, y1 + 4 * scale), IM_COL32(255, 255, 255, 255), 2.0f);

    char label[64];
    if (shuttleLevel > 0) { formatRate(label, sizeof(label), shuttleRateHz, shuttleDir); }
    else { snprintf(label, sizeof(label), "0"); }
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(cx - ts.x / 2.0f, y0 - ts.y - 4.0f * scale), IM_COL32(255, 255, 255, 255), label);
}

void FrequencySelect::setFrequency(int64_t freq) {
    if (freq != frequency) {
        gui::vfoFrequencyChanged.emit({(float)freq});
    }

    freq = std::max<int64_t>(0, freq);
    int i = getNumberOfDigits() - 1;
    for (uint64_t f = freq; i >= 0; i--) {
        digits[i] = f % 10;
        f -= digits[i];
        f /= 10;
    }
    frequency = freq;

}