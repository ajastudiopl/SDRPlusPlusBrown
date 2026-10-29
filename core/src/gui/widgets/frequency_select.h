#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <stdint.h>

class FrequencySelect {
public:
    FrequencySelect();
    void init();
    void draw();
    void setFrequency(int64_t freq);

    uint64_t frequency;
    bool frequencyChanged = false;
    bool digitHovered = false;

    bool limitFreq;
    uint64_t minFreq;
    uint64_t maxFreq;

private:
    void onPosChange();
    void onResize();
    void incrementDigit(int i);
    void decrementDigit(int i);
    void moveCursorToDigit(int i);
    void updateShuttle(ImVec2 mousePos);
    void drawShuttleBar();

    ImVec2 widgetPos;
    ImVec2 lastWidgetPos;

    int digits[12];
    ImVec2 digitBottomMins[12];
    ImVec2 digitTopMins[12];
    ImVec2 digitBottomMaxs[12];
    ImVec2 digitTopMaxs[12];

    char buf[100];

    // Press on a digit: a tap changes the digit on release, a horizontal drag
    // turns into a shuttle (the distance from the press point sets the speed).
    bool pressActive = false;
    bool pressTop = false;
    int pressDigit = -1;
    ImVec2 pressPos;
    bool shuttleActive = false;
    int shuttleLevel = 0;        // 0 = dead zone, 1..5 speed steps
    int shuttleDir = 0;          // -1 / +1
    double shuttleRateHz = 0;    // current rate in Hz per second
    double shuttleAccum = 0;
};