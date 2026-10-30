#include "band_plan.h"
#include <cstring>

namespace spectrum {
    namespace {
        const char* HAM_HF = "Amateur HF";
        const char* HAM_VU = "Amateur VHF/UHF";
        const char* BROADCAST = "Broadcast";
        const char* SERVICES = "Aviation, marine, services";
        const char* MOBILE = "Mobile, Wi-Fi";
        const char* MICS = "Wireless mics (approx.)";
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
            { MOBILE, "LTE B28 700 DL", 758.0e6, 803.0e6 },
            { MOBILE, "LTE B20 800 DL", 791.0e6, 821.0e6 },
            { MOBILE, "LTE B20 800 UL", 832.0e6, 862.0e6 },
            { MOBILE, "GSM/LTE B8 900 UL", 880.0e6, 915.0e6 },
            { MOBILE, "GSM/LTE B8 900 DL", 925.0e6, 960.0e6 },
            { MOBILE, "GSM/LTE B3 1800 UL", 1710.0e6, 1785.0e6 },
            { MOBILE, "GSM/LTE B3 1800 DL", 1805.0e6, 1880.0e6 },
            { MOBILE, "DECT", 1880.0e6, 1900.0e6 },
            { MOBILE, "UMTS/LTE B1 2100 UL", 1920.0e6, 1980.0e6 },
            { MOBILE, "UMTS/LTE B1 2100 DL", 2110.0e6, 2170.0e6 },
            { MOBILE, "LTE B7 2600 UL", 2500.0e6, 2570.0e6 },
            { MOBILE, "LTE B7 2600 DL", 2620.0e6, 2690.0e6 },
            { MOBILE, "5G n78 3.5 GHz", 3400.0e6, 3800.0e6 },
            { MOBILE, "Wi-Fi 2.4 GHz ch 1-13", 2400.0e6, 2483.5e6 },
            { MOBILE, "Wi-Fi 2.4 ch 1", 2401.0e6, 2423.0e6 },
            { MOBILE, "Wi-Fi 2.4 ch 6", 2426.0e6, 2448.0e6 },
            { MOBILE, "Wi-Fi 2.4 ch 11", 2451.0e6, 2473.0e6 },
            { MOBILE, "Wi-Fi 5 GHz (all)", 5150.0e6, 5875.0e6 },
            { MOBILE, "Wi-Fi 5 low (ch 36-64)", 5150.0e6, 5350.0e6 },
            { MOBILE, "Wi-Fi 5 high (ch 100-140)", 5470.0e6, 5725.0e6 },
            { MOBILE, "Wi-Fi 5 upper (ch 149-177)", 5725.0e6, 5875.0e6 },
            { MOBILE, "Wi-Fi 6E (lower 6 GHz)", 5925.0e6, 6425.0e6 },
            { MOBILE, "GPS L1", 1565.0e6, 1586.0e6 },
            // Radio microphones: ranges as printed on the equipment (approximate, check the label)
            { MICS, "Mics 400-600", 400.0e6, 600.0e6 },
            { MICS, "Mics UHF 470-694", 470.0e6, 694.0e6 },
            { MICS, "Sennh. A1 470-516", 470.0e6, 516.0e6 },
            { MICS, "Sennh. A 516-558", 516.0e6, 558.0e6 },
            { MICS, "Sennh. G 566-608", 566.0e6, 608.0e6 },
            { MICS, "Sennh. GB 606-648", 606.0e6, 648.0e6 },
            { MICS, "Sennh. B 626-668", 626.0e6, 668.0e6 },
            { MICS, "Sennh. C 734-776", 734.0e6, 776.0e6 },
            { MICS, "Sennh. D 780-822", 780.0e6, 822.0e6 },
            { MICS, "Sennh. E 823-865", 823.0e6, 865.0e6 },
            { MICS, "Shure G50 470-534", 470.0e6, 534.0e6 },
            { MICS, "Shure H50 534-598", 534.0e6, 598.0e6 },
            { MICS, "Shure J50 572-636", 572.0e6, 636.0e6 },
            { MICS, "Shure K51 606-670", 606.0e6, 670.0e6 },
            { MICS, "Shure L50 632-696", 632.0e6, 696.0e6 },
            { MICS, "Shure P51 710-782", 710.0e6, 782.0e6 },
            { MICS, "Shure Q51 794-806", 794.0e6, 806.0e6 },
            { MICS, "Mics 823-832", 823.0e6, 832.0e6 },
            { MICS, "Mics 863-865", 863.0e6, 865.0e6 },
            { MICS, "Mics 1785-1805", 1785.0e6, 1805.0e6 },
        };
        return plan;
    }

    std::vector<const char*> bandGroups() {
        return { HAM_HF, HAM_VU, BROADCAST, SERVICES, MOBILE, MICS };
    }
}
