#pragma once
#include <deque>
#include <vector>
#include <cstdint>
#include <cmath>
#include "chihiros_ble.h"

// CommandQueue-based device classes for Chihiros BLE protocol.
//
// Pattern: caller calls prepare(), then YAML while-loop drains the queue
// via has_next() / next(). Delays between writes stay in YAML.
//
// NTP guard: all prepare() methods accept an ESPTime value.
// RTC writes are only queued when time.is_valid() is true.
//
// Dispatch flags (rtc_only, manual_mode) and the sequence counter live in the
// C++ object — not in separate ESPHome globals.

namespace chihiros {

class CommandQueue {
    // Sequence counter: starts at 1, never resets mid-session.
    // Skips 0x5a (frame header byte) — required for WRGB2, harmless for others.
    uint8_t seq_ = 1;
    uint8_t adv_seq() {
        uint8_t v = seq_++;
        if (seq_ == 90) seq_ = 91;
        return v;
    }

protected:
    std::deque<std::vector<uint8_t>> queue_;
    bool rtc_only_ = false;

    void push(std::vector<uint8_t> cmd) { queue_.push_back(std::move(cmd)); }

    void push_auth_rtc_once(esphome::ESPTime time) {
        push(auth(adv_seq()));
        if (time.is_valid())
            push(rtc_pakket(time, adv_seq()));
    }
    void push_auth_rtc_twice(esphome::ESPTime time) {
        push(auth(adv_seq()));
        if (time.is_valid()) {
            push(rtc_pakket(time, adv_seq()));
            push(rtc_pakket(time, adv_seq()));
        }
    }

public:
    bool has_next() const { return !queue_.empty(); }
    std::vector<uint8_t> next() {
        auto cmd = queue_.front(); queue_.pop_front(); return cmd;
    }
    void clear() { queue_.clear(); }
    void set_rtc_only() { rtc_only_ = true; }

    // Expose adv_seq for subclasses that need it in prepare()
    uint8_t seq() { return adv_seq(); }
};

// ── CO2 controller ───────────────────────────────────────────────────────────
class CO2Device : public CommandQueue {
public:
    void prepare(esphome::ESPTime time, bool schema_actief,
                 uint8_t fp_uur, uint8_t fp_min,
                 uint8_t eind_uur, uint8_t eind_min,
                 int prestart_min) {
        clear();
        if (rtc_only_) {
            push_auth_rtc_once(time);
            rtc_only_ = false;
            return;
        }
        push_auth_rtc_twice(time);
        push(reset_schema(seq()));
        int total = co2_start_minuten(fp_uur, fp_min, prestart_min);
        uint8_t start_h = (uint8_t)(total / 60), start_m = (uint8_t)(total % 60);
        if (schema_actief) {
            push(co2_schema(start_h, start_m, data::CO2_ON,    seq()));
            push(co2_schema(eind_uur, eind_min, data::CO2_OFF, seq()));
        } else {
            push(co2_schema(start_h, start_m, data::CO2_EMPTY,   seq()));
            push(co2_schema(eind_uur, eind_min, data::CO2_EMPTY, seq()));
        }
    }
};

// ── Koelventilator ───────────────────────────────────────────────────────────
class VentilatorDevice : public CommandQueue {
public:
    void prepare(esphome::ESPTime time, bool silent_mode,
                 uint8_t start_temp, uint8_t max_temp, uint8_t speed) {
        clear();
        push_auth_rtc_twice(time);
        push(auth_ext1(seq()));
        push(auth_ext2(seq()));
        if (!silent_mode) {
            // Silent: 6× alternerende mode-commando's, geen thresh/speed/final-auth
            for (int i = 0; i < 3; i++) {
                push(set_mode(data::SILENT_ON,  seq()));
                push(set_mode(data::SILENT_OFF, seq()));
            }
        } else {
            // Normaal: thresh + twee mode-commando's + speed + final auth
            push(fan_temp_thresh(start_temp, max_temp, seq()));
            push(set_mode(data::SILENT_OFF, seq()));
            push(set_mode(data::SILENT_ON,  seq()));
            if (speed > 0)
                push(fan_speed(speed, seq()));
            push(auth_ext1(seq()));
            push(auth_ext2(seq()));
        }
    }
};

// ── Doctor Mate ───────────────────────────────────────────────────────────────
// TDS first (positie 1), volume second (positie 2) — apparaat onderscheidt op volgorde.
class DoctorDevice : public CommandQueue {
public:
    void prepare(esphome::ESPTime time, float tds_ppm, float volume_l) {
        clear();
        push(auth_device(seq()));
        if (time.is_valid())
            push(rtc_pakket(time, seq()));
        if (rtc_only_) { rtc_only_ = false; return; }
        push(device_settings(0x00, (uint8_t)roundf(tds_ppm / 0.4f), seq()));
        push(device_settings(0x00, (uint8_t)(volume_l * 2.0f),       seq()));
    }
};

// ── WRGB2 LED ─────────────────────────────────────────────────────────────────
class WRGB2Device : public CommandQueue {
public:
    void prepare(esphome::ESPTime time, bool auto_modus,
                 uint8_t fp_start_h, uint8_t fp_start_m,
                 uint8_t fp_eind_h,  uint8_t fp_eind_m,
                 uint8_t ramp_min, uint8_t r, uint8_t g, uint8_t b) {
        clear();
        push(auth(seq()));
        if (time.is_valid()) {
            push(rtc_pakket(time, seq()));
            push(rtc_pakket(time, seq()));
        }
        if (rtc_only_) { rtc_only_ = false; return; }
        if (auto_modus) {
            push(reset_schema(seq()));
            push(wrgb_schedule(fp_start_h, fp_start_m, fp_eind_h, fp_eind_m,
                               wrgb2_ramp_veilig(ramp_min), 0x7f, r, g, b, seq()));
            push(reset_auto(seq()));
            if (time.is_valid())
                push(rtc_pakket(time, seq()));  // triggers lamp schedule evaluation
        } else {
            push(wrgb_channel(data::WRGB_R, r, seq()));
            push(wrgb_channel(data::WRGB_G, g, seq()));
            push(wrgb_channel(data::WRGB_B, b, seq()));
        }
    }
};

// ── Dosing pump ───────────────────────────────────────────────────────────────
// Manual dose: call set_manual_dose() before triggering the BLE connection.
class DosingDevice : public CommandQueue {
    bool    manual_mode_ = false;
    uint8_t manual_pump_ = 0;
    uint8_t manual_vol_  = 0;
public:
    void set_manual_dose(uint8_t pump_idx, uint8_t vol_01ml) {
        manual_mode_ = true;
        manual_pump_ = pump_idx;
        manual_vol_  = vol_01ml;
    }
    void prepare(esphome::ESPTime time,
                 bool    actief[4],
                 uint8_t weekdays[4],
                 uint8_t uur[4],
                 uint8_t min_[4],
                 float   vol[4]) {
        clear();
        push_auth_rtc_twice(time);
        push(auth_dose1(seq()));
        push(auth_dose2(seq()));
        if (manual_mode_) {
            push(dose_pump(manual_pump_, manual_vol_, seq()));
            manual_mode_ = false;
        } else {
            for (int p = 0; p < 4; p++) {
                uint8_t vol_01ml = (uint8_t)(vol[p] * 10.0f + 0.5f);
                push(dose_schedule_enable((uint8_t)p, actief[p],              seq()));
                push(dose_schedule_speed( (uint8_t)p, weekdays[p], uur[p], min_[p], vol_01ml, seq()));
                push(dose_schedule_timer( (uint8_t)p, uur[p],               seq()));
            }
        }
    }
};

// ── Magnetisch roerder ────────────────────────────────────────────────────────
class RoerderDevice : public CommandQueue {
public:
    void prepare(esphome::ESPTime time,
                 uint8_t uur[4], uint8_t min_[4],
                 uint8_t vrlp[4], uint8_t spd[4], uint8_t dur[4],
                 bool k0, bool k1, bool k2, bool k3) {
        clear();
        push_auth_rtc_once(time);
        if (rtc_only_) { rtc_only_ = false; return; }
        for (int ch = 0; ch < 4; ch++) {
            push(stir_enable((uint8_t)ch, seq()));
            push(stir_weekdays((uint8_t)ch, 0x7f, seq()));
            push(stir_schema((uint8_t)ch, vrlp[ch], spd[ch], seq()));
            push(stir_timer((uint8_t)ch, uur[ch], min_[ch], dur[ch], seq()));
        }
        push(stir_apply(seq()));
        push(stir_restore((uint8_t)k0, (uint8_t)k1, (uint8_t)k2, (uint8_t)k3, seq()));
    }
};

} // namespace chihiros
