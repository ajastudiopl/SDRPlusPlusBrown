#pragma once
#include <vector>
#include <stdint.h>
#include <string>

namespace backend {
    struct DevVIDPID {
        uint16_t vid;
        uint16_t pid;
    };

    extern const std::vector<DevVIDPID> AIRSPY_VIDPIDS;
    extern const std::vector<DevVIDPID> AIRSPYHF_VIDPIDS;
    extern const std::vector<DevVIDPID> HACKRF_VIDPIDS;
    extern const std::vector<DevVIDPID> HYDRASDR_VIDPIDS;
    extern const std::vector<DevVIDPID> RTL_SDR_VIDPIDS;

    int getDeviceFD(int& vid, int& pid, const std::vector<DevVIDPID>& allowedVidPids);
    // What the phone sees on USB, one device per line ("1d50:6089 ready"), empty = nothing.
    std::string getUsbStatus();
    // Devices whose permission was refused are asked for again.
    void retryUsbPermission();

    std::string httpGet(const std::string& url);

    std::string getAudioSinkIds();
    std::string getAudioSourceIds();
    std::string scanAudioDevices();
    std::string startBtSco();
    std::string stopBtSco();

    void doPermissionsDialogs();
    // 0 = landscape, 1 = follow the sensor (default), 2 = portrait
    void setScreenOrientation(int mode);
    std::string getBatteryLevel();

}