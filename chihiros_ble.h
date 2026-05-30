#pragma once
#include <vector>
#include <cstdint>
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
inline std::vector<uint8_t> rtc_pakket(ESPTime t, uint8_t seq) {
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
inline std::vector<uint8_t> stir_speed(uint8_t channel, uint8_t speed_0_127, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_SPEED, {channel, speed_0_127, 0x01, 0x00, 0x00, 0x00}, seq);
}
inline std::vector<uint8_t> stir_timer(uint8_t channel, uint8_t duration, uint8_t interval, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_TIMER, {channel, 0x00, duration, interval, 0x00, 0x00}, seq);
}
inline std::vector<uint8_t> stir_apply(uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_APPLY, {0x00}, seq);
}
// Restores all 4 channels in one write; each kN is 0x01 (on) or 0x00 (off).
inline std::vector<uint8_t> stir_restore(uint8_t k0, uint8_t k1, uint8_t k2, uint8_t k3, uint8_t seq) {
    return pakket(hdr::DEVICE, cmd::STIR_TOGGLE,
        {data::SKIP, data::SKIP, k0, k1, k2, k3, data::SKIP, data::SKIP, data::SKIP, data::SKIP}, seq);
}

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

} // namespace chihiros
