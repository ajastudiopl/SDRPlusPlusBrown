#include <gui/widgets/bandplan.h>
#include <fstream>
#include <utils/wstr.h>
#include <utils/flog.h>
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace bandplan {
    std::map<std::string, BandPlan_t> bandplans;
    std::vector<std::string> bandplanNames;
    std::string bandplanNameTxt;
    std::map<std::string, BandPlanColor_t> colorTable;

    void generateTxt() {
        bandplanNameTxt = "";
        for (int i = 0; i < bandplanNames.size(); i++) {
            bandplanNameTxt += bandplanNames[i];
            bandplanNameTxt += '\0';
        }
    }

    void to_json(json& j, const Band_t& b) {
        j = json{
            { "name", b.name },
            { "type", b.type },
            { "start", b.start },
            { "end", b.end },
        };
    }

    void from_json(const json& j, Band_t& b) {
        j.at("name").get_to(b.name);
        j.at("type").get_to(b.type);
        j.at("start").get_to(b.start);
        j.at("end").get_to(b.end);
    }

    void to_json(json& j, const BandPlan_t& b) {
        j = json{
            { "name", b.name },
            { "country_name", b.countryName },
            { "country_code", b.countryCode },
            { "author_name", b.authorName },
            { "author_url", b.authorURL },
            { "bands", b.bands }
        };
    }

    void from_json(const json& j, BandPlan_t& b) {
        j.at("name").get_to(b.name);
        j.at("country_name").get_to(b.countryName);
        j.at("country_code").get_to(b.countryCode);
        j.at("author_name").get_to(b.authorName);
        j.at("author_url").get_to(b.authorURL);
        j.at("bands").get_to(b.bands);
    }

    void to_json(json& j, const BandPlanColor_t& ct) {
        flog::error("ImGui color to JSON not implemented!!!");
    }

    void from_json(const json& j, BandPlanColor_t& ct) {
        std::string col = j.get<std::string>();
        if (col[0] != '#' || !std::all_of(col.begin() + 1, col.end(), ::isxdigit)) {
            return;
        }
        uint8_t r, g, b, a;
        r = std::stoi(col.substr(1, 2), NULL, 16);
        g = std::stoi(col.substr(3, 2), NULL, 16);
        b = std::stoi(col.substr(5, 2), NULL, 16);
        a = std::stoi(col.substr(7, 2), NULL, 16);
        ct.colorValue = IM_COL32(r, g, b, a);
        ct.transColorValue = IM_COL32(r, g, b, 100);
    }

    // Wi-Fi channels are added to every plan (type "wifi"), so they show in the band strip with the
    // other bands. 20 MHz wide, named with their first and last frequency in MHz.
    static void appendWifiChannels(BandPlan_t& plan) {
        for (const auto& b : plan.bands) {
            if (b.type == "wifi") { return; }
        }
        // (the microphone ranges below are added together with the Wi-Fi channels)
        auto add = [&](int channel, double centerHz) {
            char name[64];
            snprintf(name, sizeof(name), "Wi-Fi %d %.0f-%.0f", channel, (centerHz - 10e6) / 1e6, (centerHz + 10e6) / 1e6);
            plan.bands.push_back({ name, "wifi", centerHz - 10e6, centerHz + 10e6 });
        };
        // 2.4 GHz: channels 1-13 overlap (20 MHz wide, 5 MHz apart), so each one is drawn as the 5 MHz
        // slot around its centre, i.e. the frequencies closest to that channel. The name holds the
        // 20 MHz the channel occupies; the strip shows just the number where the name does not fit.
        for (int ch = 1; ch <= 13; ch++) {
            double c = 2412e6 + (ch - 1) * 5e6;
            char name[64];
            snprintf(name, sizeof(name), "Wi-Fi %d %.0f-%.0f", ch, (c - 10e6) / 1e6, (c + 10e6) / 1e6);
            plan.bands.push_back({ name, "wifi", c - 2.5e6, c + 2.5e6 });
        }
        // 5 GHz: 36-64, 100-144, 149-177 (centre = 5000 MHz + 5 MHz * channel)
        for (int ch = 36; ch <= 64; ch += 4) { add(ch, 5000e6 + ch * 5e6); }
        for (int ch = 100; ch <= 144; ch += 4) { add(ch, 5000e6 + ch * 5e6); }
        for (int ch = 149; ch <= 177; ch += 4) { add(ch, 5000e6 + ch * 5e6); }
        // 6 GHz (Wi-Fi 6E, European lower band)
        plan.bands.push_back({ "Wi-Fi 6E 5945-6425", "wifi", 5945e6, 6425e6 });
        // Radio microphones: Sennheiser frequency ranges as printed on the equipment (approximate;
        // some overlap) and the licence-free DECT-guard range
        struct Mic { const char* name; double a, b; };
        for (const Mic& m : { Mic{ "Sennh. A1 470-516", 470e6, 516e6 }, Mic{ "Sennh. A 516-558", 516e6, 558e6 }, Mic{ "Sennh. G 566-608", 566e6, 608e6 },
                              Mic{ "Sennh. GB 606-648", 606e6, 648e6 }, Mic{ "Sennh. B 626-668", 626e6, 668e6 }, Mic{ "Sennh. C 734-776", 734e6, 776e6 },
                              Mic{ "Sennh. D 780-822", 780e6, 822e6 }, Mic{ "Sennh. E 823-865", 823e6, 865e6 }, Mic{ "Mics 1785-1805", 1785e6, 1805e6 } }) {
            plan.bands.push_back({ m.name, "mic", m.a, m.b });
        }
    }

    void loadBandPlan(std::string path) {
        std::ifstream file(wstr::str2wstr(path.c_str()));
        json data;
        file >> data;
        file.close();

        BandPlan_t plan = data.get<BandPlan_t>();
        appendWifiChannels(plan);
        if (bandplans.find(plan.name) != bandplans.end()) {
            flog::error("Duplicate band plan name ({0}), not loading.", plan.name);
            return;
        }
        bandplans[plan.name] = plan;
        bandplanNames.push_back(plan.name);
        generateTxt();
    }

    void loadFromDir(std::string path) {
        if (!std::filesystem::exists(path)) {
            flog::error("Band Plan directory does not exist");
            return;
        }
        if (!std::filesystem::is_directory(path)) {
            flog::error("Band Plan directory isn't a directory...");
            return;
        }
        bandplans.clear();
        for (const auto& file : std::filesystem::directory_iterator(path)) {
            std::string path = file.path().generic_string();
            if (file.path().extension().generic_string() != ".json") {
                continue;
            }
            loadBandPlan(path);
        }
    }

    void loadColorTable(json table) {
        colorTable = table.get<std::map<std::string, BandPlanColor_t>>();
        if (colorTable.find("mic") == colorTable.end()) {
            colorTable["mic"] = { IM_COL32(255, 150, 40, 255), IM_COL32(255, 150, 40, 100) };
        }
        if (colorTable.find("wifi") == colorTable.end()) {
            colorTable["wifi"] = { IM_COL32(0, 200, 200, 255), IM_COL32(0, 200, 200, 100) };
        }
    }
};