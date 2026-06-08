# Chihiros BLE Bridge — ESPHome

ESPHome configuration for an ESP32-S3 that controls Chihiros aquarium devices via Home Assistant, without the Chihiros app.

## How it works

Chihiros aquarium devices (CO2 controller, stirrer, fan, Doctor Mate, WRGB2 light, dosing pump) communicate via **Bluetooth Low Energy (BLE)**. Normally you control them through the Chihiros app on your phone.

This project replaces the app with an **ESP32-S3** — a small Wi-Fi + Bluetooth chip that:

1. Connects to your Chihiros devices over Bluetooth
2. Connects to your home Wi-Fi network
3. Exposes all controls to **Home Assistant** as regular entities (switches, sliders, buttons)

```
[Chihiros device] ←── Bluetooth ──→ [ESP32-S3] ←── Wi-Fi ──→ [Home Assistant]
```

## Connection Architecture

All connections are **non-persistent** — the ESP32 connects to a device, sends the necessary commands, then disconnects. This is by design.

Most Chihiros devices store their configuration internally (CO2 schedules, WRGB2 light schedule, stirrer speed/timer). The ESP32 only needs to push a new schedule when something changes, not keep a permanent connection. Keeping multiple BLE connections open simultaneously competes for radio time, making every device slower to respond.

| Device | When it connects |
|---|---|
| CO2 Controller | On boot + when schedule/times change |
| WRGB2 Light | On boot + when schedule/colors change |
| Magnetic Stirrer | On boot + when a channel is toggled or settings change |
| Doctor Mate | On boot + when TDS/volume settings change |
| Cooling Fan | **Every 5 minutes** (for temperature readings) + when settings change |
| Dosing Pump | Not yet implemented |

The fan is the only exception: it connects periodically to read water temperature, room temperature, and humidity into Home Assistant. The fan itself controls speed autonomously based on the temperature thresholds you push to it — it doesn't need an active BLE connection to operate.

**Benefit**: the BLE scanner is almost always free to discover devices quickly. Toggling a stirrer channel or pushing a new CO2 schedule typically completes within 2–4 seconds.

---

## Getting Started

### What you need
- ESP32-S3-N16R8 board (or similar ESP32-S3 with 16 MB flash)
- A running Home Assistant instance with ESPHome add-on installed

### Step 1 — Fill in your secrets

Create a `secrets.yaml` in the same folder:

```yaml
wifi_ssid_iot: "YourWiFiNetwork"
wifi_password_iot: "YourWiFiPassword"
encryption_key: ""        # generate one in ESPHome dashboard
ota_password: "choose_a_password"
web_user: "admin"
web_password: "choose_a_password"
```

### Step 2 — Fill in your MAC addresses

Open `aquarium-ble-bridge.yaml` and replace the placeholder MACs with your actual device MACs. You can find them via:
- The Chihiros app → device settings → device info
- A BLE scanner app (e.g. nRF Connect on Android/iOS)
- The ESPHome logs: unknown Chihiros devices (`DY` prefix) are logged automatically

### Step 3 — Enable the devices you want

In `aquarium-ble-bridge.yaml`, uncomment the packages you need:

```yaml
packages:
  schema:     !include aquarium-ble-bridge-schema.yaml     # fotoperiode times (shared by CO2 + WRGB2)
  co2:        !include aquarium-ble-bridge-co2.yaml
  roerder:    !include aquarium-ble-bridge-roerder.yaml
  ventilator: !include aquarium-ble-bridge-ventilator.yaml
  doctor:     !include aquarium-ble-bridge-doctor.yaml
  wrgb2:      !include aquarium-ble-bridge-wrgb2.yaml
  # dosing:   !include aquarium-ble-bridge-dosing.yaml     # pending protocol
```

`schema.yaml` defines the shared `fotoperiode_start` / `fotoperiode_eind` datetimes and the `co2_prestart` offset — used by both CO2 and WRGB2.

### Step 4 — Flash

Flash the first time via USB using the ESPHome dashboard (Web Serial in Chrome/Edge). After that, OTA works fine — flash with:

```bash
docker exec esphome esphome upload /config/aquarium-ble-bridge.yaml
```

### Step 5 — Check the logs

After booting, each device connects once, runs its init sequence, and disconnects:

```
[I][wrgb2]: Auth
[I][wrgb2]: RTC 2026-06-08 14:23:01
[I][wrgb2]: schema 08:00→22:00 ramp=30 R=61 G=45 B=80
[I][wrgb2]: init klaar, verbinding verbroken
[I][co2]: Auth
[I][co2]: CO2 schema verstuurd
[I][co2]: instellingen verstuurd, verbinding verbroken
```

This connect → configure → disconnect pattern is expected and correct.

---

## Hardware

- **Board**: ESP32-S3-N16R8 (`esp32-s3-devkitc-1`, `variant: esp32s3`, `flash_size: 16MB`)
- **Framework**: `esp-idf` (required for reliable multi-client BLE)
- **BLE connections**: up to 7 (`CONFIG_BT_ACL_CONNECTIONS: "7"`)
- **BLE scan**: `interval: 320ms`, `window: 60ms`, continuous

## Supported Devices

| Device | Status |
|---|---|
| CO2 Controller | Working ✅ |
| Magnetic Stirrer (4-channel) | Working ✅ |
| Cooling Fan | Working ✅ |
| Doctor Mate | Working ✅ |
| WRGB II | Working ✅ |
| Dosing Pump | Skeleton only — protocol not yet reversed |

---

## Helper Library (`chihiros_ble.h`)

All protocol helpers are in `chihiros_ble.h`. Use these instead of raw hex.

### Headers
```cpp
chihiros::hdr::BASE   // 0x5a — auth, RTC, CO2, fan mode/speed
chihiros::hdr::DEVICE // 0xa5 — stirrer, fan threshold, Doctor Mate, WRGB2 schedule
```

### Commands
```cpp
chihiros::cmd::AUTH        // 0x04
chihiros::cmd::RTC         // 0x09
chihiros::cmd::MODE        // 0x05
chihiros::cmd::SCHEMA      // 0x16 — CO2 schedule slots
chihiros::cmd::BRIGHTNESS  // 0x07 — WRGB2 per-channel brightness
chihiros::cmd::FAN_SPEED   // 0x07 — fan manual speed
chihiros::cmd::SETTINGS    // 0x01 — Doctor Mate TDS / volume
chihiros::cmd::SCHEDULE    // 0x19 — WRGB2 auto schedule
chihiros::cmd::STIR_TOGGLE // 0x14
chihiros::cmd::STIR_TIMER  // 0x15
chihiros::cmd::STIR_SPEED  // 0x1b
chihiros::cmd::STIR_ENABLE // 0x20
chihiros::cmd::STIR_APPLY  // 0x1f
chihiros::cmd::TEMP_THRESH // 0x21 — fan temperature threshold
```

### Data constants
```cpp
chihiros::data::AUTH_BASE    // 0x01
chihiros::data::AUTH_EXT1    // 0x06 — fan extra auth step 1
chihiros::data::AUTH_EXT2    // 0x08 — fan extra auth step 2
chihiros::data::RESET_SCHEMA // 0x07 — evaluate schedule now
chihiros::data::RESET_AUTO   // 0x12 — switch to auto mode
chihiros::data::SILENT_ON    // 0x22 — fan silent mode on (inverted: 0x22=ON, 0x23=OFF)
chihiros::data::SILENT_OFF   // 0x23 — fan silent mode off
chihiros::data::CO2_ON       // 0x64 — CO2 valve open
chihiros::data::CO2_OFF      // 0x00 — CO2 valve closed
chihiros::data::CO2_EMPTY    // 0x6f — schedule slot unused
chihiros::data::SKIP         // 0xff — don't touch this position
chihiros::data::WRGB_R       // 0x00 — WRGB2 red channel index
chihiros::data::WRGB_G       // 0x01 — WRGB2 green channel index
chihiros::data::WRGB_B       // 0x02 — WRGB2 blue channel index
```

### Functions
```cpp
chihiros::pakket(header, cmd, {data...}, seq)
chihiros::pakket(header, cmd, std::vector<uint8_t>, seq)
chihiros::rtc_pakket(ESPTime t, seq)
chihiros::roerder_toggle(seq, channel, on)
chihiros::next_seq(uint8_t& counter)   // skips 0x5a — required for WRGB2
```

> The sequence byte must never equal `0x5a` (the frame header). Use `next_seq()` for WRGB2 where the counter runs through an entire session without resetting.

---

## General Protocol

All devices use **Nordic UART Service (NUS)**:

| Role | UUID |
|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (write) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (notify) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

Frame format:
```
[header] [0x01] [len] [0x00] [seq] [cmd] [data...] [XOR-CRC]
```

---

## CO2 Controller

> Protocol verified via btsnoop HCI analysis (2026-05-30).

**Connect → configure → disconnect.** On connect: `AUTH` → `RTC` × 2 → `RESET_SCHEMA` → time slots → disconnect.

`RESET_SCHEMA` clears all existing slots — no old values need to be tracked when times change.

Deactivating: send `CO2_EMPTY` slots instead of `CO2_ON`/`CO2_OFF`.

---

## Magnetic Stirrer (4-channel)

**Connect → configure → disconnect.** On connect: `AUTH` → `RTC` → config for all 4 channels → `STIR_APPLY` → restore on/off state → disconnect.

Channel on/off state is stored in NVS globals (`restore_value: true`) so after an ESP reboot the stirrer returns to the last-known state.

Speed encoding: HA 0–100% → device 0–127.

---

## Cooling Fan

**Periodic connect** (every 5 minutes). On connect: `AUTH` → `RTC` × 2 → `AUTH_EXT1` → `AUTH_EXT2` → silent mode → temperature threshold → manual speed → wait 2 s for notifications → disconnect.

The fan controls speed autonomously based on the temperature threshold. The 2-second wait after init is to receive the notification packet containing water temperature, room temperature, fan speed, and humidity.

Notifications (`x[4] == 0x01`):

| Byte | Value |
|---|---|
| `x[5]` | Fan speed (%) |
| `x[6:7] / 256` | Room temperature (°C) |
| `x[11] / 10` | Water temperature (°C) |
| `x[12]` | Humidity (%) |

---

## Doctor Mate

**Connect → configure → disconnect.** On connect: `AUTH` → `RTC` → `SETTINGS` (TDS) → `SETTINGS` (Volume) → disconnect.

TDS and Volume use identical frames — device distinguishes them **only by send order**. Always send both in this order.

Encoding: TDS = EC (µS/cm) = ppm ÷ 0.4. Volume = liters × 2.

---

## WRGB II

> Protocol verified via btsnoop HCI analysis (2026-05-30).

**Connect → configure → disconnect.** The lamp stores its schedule internally and runs autonomously. BLE is only needed to push a new schedule.

On connect in auto mode: `AUTH` → `RTC` × 2 → `RESET_SCHEMA` → `SCHEDULE` → `RESET_AUTO` → final `RTC` (triggers schedule evaluation) → disconnect.

Schedule `DEVICE SCHEDULE` data: `[on_h, on_m, off_h, off_m, ramp_min, weekdays, R, G, B, 0xff×5]`

- `ramp_min`: fade duration 0–150 min. **Value 90 is forbidden** (= 0x5a frame header) → use 89.
- `weekdays`: bitmask Mon=64..Sun=1; 127 = every day.
- `R/G/B = 0xff` = delete marker (deactivate).

---

## Dosing Pump

> **Skeleton only.** Protocol not yet reverse-engineered.

Connect + auth + notification logging is implemented. To reverse-engineer: flash the skeleton, trigger a manual dose in the Chihiros app while watching ESPHome logs, identify the command bytes.

---

## ESPHome Tips

- State persistence across reboots: use `globals` with `restore_value: true`. The lambda `return id(my_global)` reflects current state even after a crash/reboot.
- `script.execute` with inline `{ }` parameter syntax is unreliable from `turn_on/turn_off_action`. Use inline `ble_client.ble_write` instead.
- Secrets required: `wifi_ssid_iot`, `wifi_password_iot`, `encryption_key`, `ota_password`, `web_user`, `web_password`.
