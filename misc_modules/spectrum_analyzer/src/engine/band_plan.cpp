#include "band_plan.h"
#include <cstring>

namespace spectrum {
    namespace {
        const char* HAM_HF = "Amateur HF";
        const char* HAM_VU = "Amateur VHF/UHF";
        const char* BROADCAST = "Broadcast";
        const char* SERVICES = "Aviation, marine, services";
    }

    const std::vector<BandEntry>& bandPlan() {
        static const std::vector<BandEntry> plan = {
            { HAM_HF, "160 m", 1.810e6, 2.000e6 },
            { HAM_HF, "80 m", 3.500e6, 3.800e6 },
            { HAM_HF, "60 m", 5.3515e6, 5.3665e6 },
            { HAM_HF, "40 m", 7.000e6, 7.200e6 },
            { HAM_HF, "30 m", 10.100e6, 10.150e6 },
            { HAM_HF, "20 m", 14.000e6, 14.350e6 },
            { HAM_HF, "17 m", 18.068e6, 18.168e6 },
            { HAM_HF, "15 m", 21.000e6, 21.450e6 },
            { HAM_HF, "12 m", 24.890e6, 24.990e6 },
            { HAM_HF, "10 m", 28.000e6, 29.700e6 },
            { HAM_HF, "All HF (1.8-30 MHz)", 1.8e6, 30.0e6 },
            { HAM_VU, "6 m", 50.0e6, 52.0e6 },
            { HAM_VU, "4 m", 70.0e6, 70.5e6 },
            { HAM_VU, "2 m", 144.0e6, 146.0e6 },
            { HAM_VU, "70 cm", 430.0e6, 440.0e6 },
            { HAM_VU, "23 cm", 1240.0e6, 1300.0e6 },
            { BROADCAST, "LW", 148.5e3, 283.5e3 },
            { BROADCAST, "MW", 526.5e3, 1606.5e3 },
            { BROADCAST, "SW 49 m", 5.90e6, 6.20e6 },
            { BROADCAST, "SW 41 m", 7.20e6, 7.45e6 },
            { BROADCAST, "SW 31 m", 9.40e6, 9.90e6 },
            { BROADCAST, "SW 25 m", 11.60e6, 12.10e6 },
            { BROADCAST, "SW 19 m", 15.10e6, 15.80e6 },
            { BROADCAST, "SW 13 m", 21.45e6, 21.85e6 },
            { BROADCAST, "FM", 87.5e6, 108.0e6 },
            { BROADCAST, "DAB+ (band III)", 174.0e6, 240.0e6 },
            { SERVICES, "Airband", 118.0e6, 137.0e6 },
            { SERVICES, "Marine VHF", 156.0e6, 162.05e6 },
            { SERVICES, "CB 27 MHz", 26.96e6, 27.41e6 },
            { SERVICES, "PMR446", 446.0e6, 446.2e6 },
            { SERVICES, "LPD 433", 433.05e6, 434.79e6 },
            { SERVICES, "ISM 868", 863.0e6, 870.0e6 },
            { SERVICES, "ADS-B 1090", 1085.0e6, 1095.0e6 },
            { SERVICES, "Weather sat 137", 137.0e6, 138.0e6 },
            { SERVICES, "ISM 2.4 GHz", 2400.0e6, 2483.5e6 },
        };
        return plan;
    }

    std::vector<const char*> bandGroups() {
        return { HAM_HF, HAM_VU, BROADCAST, SERVICES };
    }
}
