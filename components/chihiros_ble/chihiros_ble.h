#pragma once
#include <vector>
#include <cstdint>
#include <cstdio>
#include <string>
#include <initializer_list>

// Chihiros Nordic UART protocol helpers.
// Frame format: [header] 01 [len] 00 [seq] [cmd] [data...] [XOR-CRC]
// header BASE (0x5a): auth, RTC, CO2, fan mode/speed
// header DEVICE (0xa5): stirrer, fan threshold, Doctor Mate

namespace chihiros {

namespace hdr {
  constexpr uint8_t BASE   = 0x5a;
  constexpr uint8_t DEVICE = 0xa5;
}

namespace cmd {
  constexpr uint8_t AUTH        = 0x04;
  constexpr uint8_t RTC         = 0x09;
  constexpr uint8_t MODE        = 0x05;  // CO2 reset / fan mode init
  constexpr uint8_t SCHEMA      = 0x16;  // CO2 schedule time slots
  constexpr uint8_t FAN_SPEED   = 0x07;  // fan manual speed
  constexpr uint8_t BRIGHTNESS  = 0x07;  // WRGB2 per-channel brightness (same byte as FAN_SPEED)
  constexpr uint8_t SETTINGS    = 0x01;  // Doctor Mate TDS / volume
  constexpr uint8_t SCHEDULE    = 0x19;  // WRGB2 auto schedule (add/delete)
  constexpr uint8_t STIR_TOGGLE = 0x14;
  constexpr uint8_t STIR_TIMER  = 0x15;
  constexpr uint8_t STIR_SPEED  = 0x1b;
  constexpr uint8_t STIR_ENABLE = 0x20;
  constexpr uint8_t STIR_APPLY  = 0x1f;
  constexpr uint8_t TEMP_THRESH = 0x21;
}

namespace data {
  constexpr uint8_t AUTH_BASE    = 0x01;
  constexpr uint8_t AUTH_EXT1    = 0x06;  // fan extra auth step 1
  constexpr uint8_t AUTH_EXT2    = 0x08;  // fan extra auth step 2
  constexpr uint8_t AUTH_DOSE1   = 0x04;  // dosing pump extra auth step 1
  constexpr uint8_t AUTH_DOSE2   = 0x05;  // dosing pump extra auth step 2
  constexpr uint8_t RESET_SCHEMA = 0x07;  // CO2: evaluate schedule now
  constexpr uint8_t RESET_AUTO   = 0x12;  // CO2: switch to auto mode
  constexpr uint8_t SILENT_ON    = 0x22;
  constexpr uint8_t SILENT_OFF   = 0x23;
  constexpr uint8_t CO2_ON       = 0x64;
  constexpr uint8_t CO2_OFF      = 0x00;
  constexpr uint8_t CO2_EMPTY    = 0x6f;  // schedule slot unused
  constexpr uint8_t SKIP         = 0xff;  // don't touch this position
  // WRGB2 color channel indices
  constexpr uint8_t WRGB_R      = 0x00;
  constexpr uint8_t WRGB_G      = 0x01;
  constexpr uint8_t WRGB_B      = 0x02;
}

namespace detail {
inline std::vector<uint8_t> wrap(uint8_t header, const std::vector<uint8_t>& frame) {
    uint8_t crc = 0;
    for (auto b : frame) crc ^= b;
    std::vector<uint8_t> p = {header};
    p.insert(p.end(), frame.begin(), frame.end());
    p.push_back(crc);
    return p;
}
} // namespace detail

inline std::vector<uint8_t> pakket(uint8_t header, uint8_t cmd,
                                    std::initializer_list<uint8_t> d, uint8_t seq) {
    uint8_t len = 5 + d.size();
    std::vector<uint8_t> frame = {0x01, len, 0x00, seq, cmd};
    frame.insert(frame.end(), d.begin(), d.end());
    return detail::wrap(header, frame);
}

inline std::vector<uint8_t> pakket(uint8_t header, uint8_t cmd,
                                    const std::vector<uint8_t>& d, uint8_t seq) {
    uint8_t len = 5 + (uint8_t)d.size();
    std::vector<uint8_t> frame = {0x01, len, 0x00, seq, cmd};
    frame.insert(frame.end(), d.begin(), d.end());
    return detail::wrap(header, frame);
}

// Returns current counter value and advances to next safe value, skipping 0x5a (frame header).
// Required for WRGB2; other devices rarely reach seq=90 in a session but safe to use everywhere.
inline uint8_t next_seq(uint8_t& counter) {
    uint8_t v = counter++;
    if (counter == 90) counter = 91;
    return v;
}

// Builds an RTC sync packet from an ESPTime value.
inline std::vector<uint8_t> rtc_pakket(esphome::ESPTime t, uint8_t seq) {
    return pakket(hdr::BASE, cmd::RTC, {
        (uint8_t)(t.year - 2000), (uint8_t)t.month,
        (uint8_t)((t.day_of_week + 5) % 7 + 1),
        (uint8_t)t.hour, (uint8_t)t.minute, (uint8_t)t.second
    }, seq);
}

// Sends an on/off command for one stirrer channel (STIR_TOGGLE).
// Other channel positions are set to SKIP = "don't touch".
inline std::vector<uint8_t> roerder_toggle(uint8_t seq, int channel, bool on) {
    std::vector<uint8_t> d(10, data::SKIP);
    d[2 + channel] = on ? 0x01 : 0x00;
    return pakket(hdr::DEVICE, cmd::STIR_TOGGLE, d, seq);
}

// ── Auth ──────────────────────────────────────────────────────────────────────

inline std::vector<uint8_t> auth(uint8_t seq) {
    return pakket(hdr::BASE, cmd::AUTH, {data::AUTH_BASE}, seq);
}
inline std::vector<uint8_t> auth_device(uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::AUTH, {data::AUTH_BASE}, seq);
}
inline std::vector<uint8_t> auth_ext1(uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::AUTH, {data::AUTH_EXT1}, seq);
}
inline std::vector<uint8_t> auth_ext2(uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::AUTH, {data::AUTH_EXT2}, seq);
}
inline std::vector<uint8_t> auth_dose1(uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::AUTH, {data::AUTH_DOSE1}, seq);
}
inline std::vector<uint8_t> auth_dose2(uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::AUTH, {data::AUTH_DOSE2}, seq);
}

// ── Mode ──────────────────────────────────────────────────────────────────────

inline std::vector<uint8_t> reset_schema(uint8_t seq) {
    return pakket(hdr::BASE, cmd::MODE, {data::RESET_SCHEMA, data::SKIP, data::SKIP}, seq);
}
inline std::vector<uint8_t> reset_auto(uint8_t seq) {
    return pakket(hdr::BASE, cmd::MODE, {data::RESET_AUTO, data::SKIP, data::SKIP}, seq);
}
// Fan silent mode / WRGB2 mode init — caller supplies the mode byte
inline std::vector<uint8_t> set_mode(uint8_t mode_byte, uint8_t seq) {
    return pakket(hdr::BASE, cmd::MODE, {mode_byte, data::SKIP, data::SKIP}, seq);
}

// ── CO2 ───────────────────────────────────────────────────────────────────────

inline std::vector<uint8_t> co2_schema(uint8_t hour, uint8_t minute, uint8_t val, uint8_t seq) {
    return pakket(hdr::BASE, cmd::SCHEMA, {hour, minute, val}, seq);
}

// ── Fan ───────────────────────────────────────────────────────────────────────

inline std::vector<uint8_t> fan_speed(uint8_t speed, uint8_t seq) {
    return pakket(hdr::BASE, cmd::FAN_SPEED, {data::SKIP, speed}, seq);
}
inline std::vector<uint8_t> fan_temp_thresh(uint8_t start_c, uint8_t max_c, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::TEMP_THRESH, {start_c, max_c, data::SKIP}, seq);
}

// ── Stirrer ───────────────────────────────────────────────────────────────────

inline std::vector<uint8_t> stir_enable(uint8_t channel, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_ENABLE, {channel, 0x00, 0x01}, seq);
}
// Weekdays for the autonomous schedule. byte[1] = bitmask (same encoding as WRGB2/dosing):
// Ma=64 Di=32 Wo=16 Do=8 Vr=4 Za=2 Zo=1 — every day = 0x7f (127).
// Confirmed btsnoop 2026-06-11 (schema + Run): STIR_SPEED (0x1b) is NEVER a speed command.
// Speed (both schema and Run mode) always uses CMD_2A via stir_schema().
inline std::vector<uint8_t> stir_weekdays(uint8_t channel, uint8_t weekdays, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_SPEED, {channel, weekdays, 0x01, 0x00, 0x00, 0x00}, seq);
}
// Daily clock schedule: run for duration_sec starting at HH:MM every day.
// Confirmed from btsnoop 2026-06-11: [ch, 0x03, hour, minute, 0x00, duration_sec]
inline std::vector<uint8_t> stir_timer(uint8_t channel, uint8_t hour, uint8_t minute, uint8_t duration_sec, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_TIMER, {channel, 0x03, hour, minute, 0x00, duration_sec}, seq);
}
// Persistent schema settings per channel: lead time (seconds) + speed (0-20 app scale).
// Confirmed from btsnoop 2026-06-11: ch2 voorloop=36s speed=20 → 02002414, speed=2 → 02002402.
inline std::vector<uint8_t> stir_schema(uint8_t channel, uint8_t voorloop_sec, uint8_t snelheid_0_20, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::CMD_2A, {channel, 0x00, voorloop_sec, snelheid_0_20}, seq);
}

inline std::vector<uint8_t> stir_apply(uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_APPLY, {0x00}, seq);
}
// Restores all 4 channels in one write; each kN is 0x01 (on) or 0x00 (off).
inline std::vector<uint8_t> stir_restore(uint8_t k0, uint8_t k1, uint8_t k2, uint8_t k3, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_TOGGLE,
        {data::SKIP, data::SKIP, k0, k1, k2, k3, data::SKIP, data::SKIP, data::SKIP, data::SKIP}, seq);
}

// Confirmed btsnoop 2026-06-11 (Run mode speed=10): speed in Run mode also uses CMD_2A,
// not STIR_SPEED. STIR_SPEED (0x1b) is exclusively a weekdays bitmask command — use stir_weekdays().

// ── WRGB2 ─────────────────────────────────────────────────────────────────────

inline std::vector<uint8_t> wrgb_channel(uint8_t channel, uint8_t brightness, uint8_t seq) {
    return pakket(hdr::BASE, cmd::BRIGHTNESS, {channel, brightness}, seq);
}
// weekdays bitmask: Mon=64 Tue=32 Wed=16 Thu=8 Fri=4 Sat=2 Sun=1; 127 = every day
// ramp_min must not equal 90 (= 0x5a frame header) — caller must sanitize
inline std::vector<uint8_t> wrgb_schedule(uint8_t on_h, uint8_t on_m,
                                           uint8_t off_h, uint8_t off_m,
                                           uint8_t ramp_min, uint8_t weekdays,
                                           uint8_t r, uint8_t g, uint8_t b,
                                           uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::SCHEDULE,
        {on_h, on_m, off_h, off_m, ramp_min, weekdays, r, g, b,
         data::SKIP, data::SKIP, data::SKIP, data::SKIP, data::SKIP}, seq);
}

// ── Doctor Mate / Dosing ──────────────────────────────────────────────────────

// Doctor Mate: b1=0x00 always; b2=ec for TDS (pos 1) or volume for Volume (pos 2).
inline std::vector<uint8_t> device_settings(uint8_t b1, uint8_t b2, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::SETTINGS, {b1, b2}, seq);
}

// Dosing pump: trigger a manual dose for one pump.
// pump_idx: 0=pomp1, 1=pomp2, 2=pomp3
// vol_01ml: volume in 0.1 mL units (10 = 1.0 mL, 255 = 25.5 mL)
// cmd reuses STIR_SPEED (0x1b) — confirmed from btsnoop 2026-06-08
inline std::vector<uint8_t> dose_pump(uint8_t pump_idx, uint8_t vol_01ml, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_SPEED, {pump_idx, 0x00, 0x00, 0x00, vol_01ml}, seq);
}

// Dosing pump schedule — three separate writes needed per pump.
// Time encoding: hour split across STIR_TIMER[2]=hour>>1 and STIR_SPEED[2]=hour&1
// weekdays: Mon=64 Tue=32 Wed=16 Thu=8 Fri=4 Sat=2 Sun=1 (same as WRGB2)
// vol_01ml: 0.1 mL per unit. minute: 0-59 (use 0 if unsure).
// Disable: send dose_schedule_enable with enable=false.
inline std::vector<uint8_t> dose_schedule_enable(uint8_t pump_idx, bool enable, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_ENABLE, {pump_idx, 0x00, enable ? (uint8_t)0x01 : (uint8_t)0x00}, seq);
}
inline std::vector<uint8_t> dose_schedule_speed(uint8_t pump_idx, uint8_t weekdays, uint8_t hour, uint8_t minute, uint8_t vol_01ml, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_SPEED, {pump_idx, weekdays, (uint8_t)(hour & 1), minute, 0x00, vol_01ml}, seq);
}
inline std::vector<uint8_t> dose_schedule_timer(uint8_t pump_idx, uint8_t hour, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_TIMER, {pump_idx, 0x00, (uint8_t)(hour >> 1), 0x00, 0x00, 0x00}, seq);
}

// ── Bridge utilities ─────────────────────────────────────────────────────────

// Formats a BLE notification payload as a hex string for logging.
inline std::string hex_dump(const std::vector<uint8_t>& data) {
    std::string s;
    s.reserve(data.size() * 3);
    for (auto b : data) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02x ", b);
        s += buf;
    }
    return s;
}

// Calculates CO2 start time: fotoperiode_start minus prestart minutes, midnight-safe.
// Returns minutes since midnight (0–1439).
inline int co2_start_minuten(int fotoperiode_uur, int fotoperiode_min, int prestart_min) {
    int total = fotoperiode_uur * 60 + fotoperiode_min - prestart_min;
    if (total < 0) total += 1440;
    return total;
}

// Parsed sensor values from a ventilator BLE notification frame.
struct VentilatorNotificatie {
    float fan_speed;   // %
    float kamer_temp;  // °C  (byte[6:7] / 256)
    float water_temp;  // °C  (byte[11] / 10)
    float humidity;    // %   (byte[12])
    bool  valid;
};

inline VentilatorNotificatie parse_ventilator_notificatie(const std::vector<uint8_t>& x) {
    VentilatorNotificatie d{};
    if (x.size() >= 13 && x[4] == 0x01) {
        d.valid      = true;
        d.fan_speed  = (float)x[5];
        d.kamer_temp = ((x[6] << 8) | x[7]) / 256.0f;
        d.water_temp = x[11] / 10.0f;
        d.humidity   = (float)x[12];
    }
    return d;
}

// WRGB2: ramp waarde 90 is verboden (= 0x5a frame header) — kap af naar 89.
inline uint8_t wrgb2_ramp_veilig(uint8_t ramp) {
    return ramp == 90u ? 89u : ramp;
}

} // namespace chihiros
