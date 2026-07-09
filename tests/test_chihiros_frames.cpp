// Golden-frame tests voor chihiros_ble.h / chihiros_devices.h.
// De verwachte bytes komen 1-op-1 uit btsnoop HCI captures van de Chihiros app
// (capture-datum per blok in de comments) — deze tests leggen het protocol vast.
//
// Host-side draaien:
//   g++ -std=c++17 -Wall -o /tmp/test_frames tests/test_chihiros_frames.cpp && /tmp/test_frames

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Minimale stub voor esphome::ESPTime — alleen de velden die de headers gebruiken
namespace esphome {
struct ESPTime {
    uint16_t year;
    uint8_t month, day_of_month, hour, minute, second;
    uint8_t day_of_week;  // 1 = zondag … 7 = zaterdag (ESPHome-conventie)
    bool valid = true;
    bool is_valid() const { return valid; }
};
}  // namespace esphome

#include "../components/chihiros_ble/chihiros_ble.h"
#include "../components/chihiros_ble/chihiros_devices.h"

static int fouten = 0;

static std::vector<uint8_t> hex2bytes(const std::string &hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back((uint8_t)strtol(hex.substr(i, 2).c_str(), nullptr, 16));
    return out;
}

static void check(const char *naam, const std::vector<uint8_t> &frame, const std::string &verwacht_hex) {
    auto verwacht = hex2bytes(verwacht_hex);
    if (frame == verwacht) { printf("OK   %s\n", naam); return; }
    fouten++;
    printf("FOUT %s\n  verwacht: %s\n  gekregen: %s\n", naam, verwacht_hex.c_str(),
           chihiros::hex_dump(frame).c_str());
}

int main() {
    using namespace chihiros;

    // 2026-05-29 was een vrijdag: ESPHome day_of_week=6 (1=zondag) → app-encoding 5 (1=maandag)
    esphome::ESPTime vrijdag{2026, 5, 29, 11, 57, 12, /*day_of_week=*/6};

    // ── btsnoop_hci_fan.log (2026-05-29) ──
    check("auth",                auth(0x18),                    "5a0106001804011a");
    check("rtc_pakket",          rtc_pakket(vrijdag, 0x19),     "5a010b0019091a05050b390c3e");
    check("auth_ext1",           auth_ext1(0x1b),               "a50106001b04061e");
    check("auth_ext2",           auth_ext2(0x1c),               "a50106001c040817");
    check("fan_temp_thresh",     fan_temp_thresh(24, 28, 0x1d), "a50108001d21181cffce");
    check("set_mode SILENT_OFF", set_mode(data::SILENT_OFF, 0x1f), "5a0108001f0523ffff30");
    check("fan_speed",           fan_speed(20, 0x21),           "5a0107002107ff14cb");

    // ── btsnoop_hci_co21400.log + co22100 (2026-05-30) ──
    check("reset_schema",     reset_schema(0x19),                     "5a010800190507ffff12");
    check("co2_schema ON",    co2_schema(12, 30, data::CO2_ON,  0x1a), "5a0108001a160c1e6473");
    check("co2_schema OFF",   co2_schema(21,  0, data::CO2_OFF, 0x1b), "5a0108001b1615000011");
    check("co2_schema EMPTY", co2_schema(12, 30, data::CO2_EMPTY, 0x1d), "5a0108001d160c1e6f7f");

    // ── btsnoop_hciwrgbactive.log (2026-05-30): 14:00-21:00, ramp 30, RGB 61/45/80 ──
    check("wrgb_schedule", wrgb_schedule(14, 0, 21, 0, 30, 0x7f, 61, 45, 80, 0x1b),
          "a50113001b190e0015001e7f3d2d50ffffffffffd5");

    // ── btsnoop_hci_tds.log (2026-05-29): TDS 100 ppm → EC 250 ──
    check("auth_device",     auth_device(0x18),          "a50106001804011a");
    check("device_settings", device_settings(250, 0x1a), "a50107001a0100fae7");

    // ── btsnoop_hci_dosing_manual.log (2026-06-08): pomp 3 (idx 2), 1.0 mL ──
    check("auth_dose1", auth_dose1(0x1b),       "a50106001b04041c");
    check("auth_dose2", auth_dose2(0x1c),       "a50106001c04051a");
    check("dose_pump",  dose_pump(2, 10, 0x1d), "a5010a001d1b020000000a05");

    // ── btsnoop_hci_roerder_snelheid.log (2026-06-11): kanaal 2, voorloop 36s, snelheid 2 ──
    check("stir_schema", stir_schema(2, 36, 2, 0x16), "a5010900162a0200240210");

    // ── Gedragstests op de device-queues ──

    // seq reset per prepare(): twee identieke prepares leveren byte-identieke frames
    {
        CO2Device d;
        std::vector<std::vector<uint8_t>> run1, run2;
        d.prepare(vrijdag, true, 14, 0, 21, 0, 90);
        while (d.has_next()) run1.push_back(d.next());
        d.prepare(vrijdag, true, 14, 0, 21, 0, 90);
        while (d.has_next()) run2.push_back(d.next());
        if (run1 == run2 && !run1.empty()) printf("OK   seq reset per prepare\n");
        else { fouten++; printf("FOUT seq reset per prepare\n"); }
    }

    // Ventilator met silent UIT moet drempels én de mode-stand sturen (regressie fix 2026-07-09)
    {
        VentilatorDevice d;
        d.prepare(vrijdag, /*silent=*/false, 24, 28, 0, true);
        bool thresh = false, mode_uit = false;
        while (d.has_next()) {
            auto f = d.next();
            if (f.size() > 5 && f[5] == cmd::TEMP_THRESH) thresh = true;
            if (f.size() > 6 && f[5] == cmd::MODE && f[6] == data::SILENT_OFF) mode_uit = true;
        }
        if (thresh && mode_uit) printf("OK   ventilator settings bij silent uit\n");
        else { fouten++; printf("FOUT ventilator settings bij silent uit (thresh=%d mode=%d)\n", thresh, mode_uit); }
    }

    // Doctor 130 ppm → EC 325 → 16-bit [0x01, 0x45] (regressie uint8-wrap fix 2026-07-09)
    {
        DoctorDevice d;
        d.prepare(vrijdag, 130.0f, 125.0f);
        bool ok = false;
        while (d.has_next()) {
            auto f = d.next();
            if (f.size() == 9 && f[5] == cmd::SETTINGS && f[6] == 0x01 && f[7] == 0x45) ok = true;
        }
        if (ok) printf("OK   doctor 16-bit EC\n");
        else { fouten++; printf("FOUT doctor 16-bit EC\n"); }
    }

    if (fouten == 0) { printf("\nAlle tests geslaagd\n"); return 0; }
    printf("\n%d test(s) GEFAALD\n", fouten);
    return 1;
}
