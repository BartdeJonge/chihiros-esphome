# Chihiros BLE Bridge — ESPHome

ESPHome configuration for an ESP32-S3 that controls Chihiros aquarium devices via Home Assistant, without the Chihiros app.

## How it works

Chihiros aquarium devices (CO2 controller, stirrer, fan, Doctor Mate, WRGB2 light, dosing pump) communicate via **Bluetooth Low Energy (BLE)**. Normally you control them through the Chihiros app on your phone.

This project replaces the app with an **ESP32-S3** — a small Wi-Fi + Bluetooth chip that:

1. Connects to your Chihiros devices over Bluetooth
2. Connects to your home Wi-Fi network
3. Exposes all controls to **Home Assistant** as regular entities (switches, sliders, buttons)

```mermaid
graph LR
    CO2["CO2 Controller"]
    STI["Magnetic Stirrer"]
    FAN["Cooling Fan"]
    DOC["Doctor Mate"]
    WRG["WRGB2 Light"]
    ESP["ESP32-S3"]
    HA["Home Assistant"]

    CO2 <-->|BLE| ESP
    STI <-->|BLE| ESP
    FAN <-->|BLE| ESP
    DOC <-->|BLE| ESP
    WRG <-->|BLE| ESP
    ESP <-->|Wi-Fi| HA
```

## Connection Architecture

All connections are **non-persistent** — the ESP32 connects to a device, sends the necessary commands, then disconnects. This is by design.

Most Chihiros devices store their configuration internally (CO2 schedules, WRGB2 light schedule, stirrer speed/timer). The ESP32 only needs to push a new schedule when something changes, not keep a permanent connection. Keeping multiple BLE connections open simultaneously competes for radio time, making every device slower to respond.

```mermaid
sequenceDiagram
    participant HA as Home Assistant
    participant ESP as ESP32-S3
    participant Dev as Chihiros Device

    HA->>ESP: API connect (boot)
    ESP->>Dev: BLE connect
    ESP->>Dev: AUTH + RTC
    ESP->>Dev: Config (schedule / settings)
    Dev-->>ESP: Notification (ack)
    ESP->>Dev: BLE disconnect
    Note over Dev: Runs autonomously on internal RTC
```

| Device | When it connects |
|---|---|
| CO2 Controller | On boot + when schedule/times change |
| WRGB2 Light | On boot + when schedule/colors change |
| Magnetic Stirrer | On boot + when a channel is toggled or settings change |
| Doctor Mate | On boot + when TDS/volume settings change |
| Cooling Fan | **Every 5 minutes** (for temperature readings) + when settings change |
| Dosing Pump | Not yet implemented |

**Benefit**: the BLE scanner is almost always free. Toggling a stirrer channel or pushing a new CO2 schedule typically completes within 2–4 seconds.

---

## Getting Started

### What you need
- ESP32-S3-N16R8 board (or similar ESP32-S3 with 16 MB flash)
- A running Home Assistant instance with ESPHome add-on installed

### Step 1 — Fill in your secrets

Create a `secrets.yaml` in the same folder:

```yaml
wifi_sid: "YourWiFiNetwork"
wifi_password_iot: "YourWiFiPassword"
encryption_key: ""        # generate one in ESPHome dashboard
ota_password: "choose_a_password"
web_user: "admin"
web_password: "choose_a_password"
```

### Step 2 — Find your device MACs

Open `aquarium-ble-bridge.yaml` and replace the placeholder MACs with your actual device MACs.

**Easiest method:** flash the firmware first (Steps 3–4), then open the ESPHome logs. Any Chihiros device within BLE range that is not yet in your config will be identified automatically — no separate BLE scanner needed. See [Auto-detection](#auto-detection) below for details.

You can also find MACs via:
- The Chihiros app → device settings → device info
- A BLE scanner app (e.g. nRF Connect on Android/iOS)

> **One device per type.** This configuration assumes exactly one of each device type. If you have multiple WRGB2 lights, you would need to duplicate the package and rename all IDs.

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

Flash the first time via USB using the ESPHome dashboard (Web Serial in Chrome/Edge). After that, OTA works fine:

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

> **BLE 5.0 required.** The original ESP32 only supports BLE 4.2 and cannot handle multiple simultaneous BLE clients reliably. Use an **ESP32-S3** (or ESP32-C3/C6) which supports BLE 5.0. The S3 is recommended for its extra RAM, which is needed when running the BLE scanner alongside multiple client connections.

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

```mermaid
sequenceDiagram
    participant ESP as ESP32-S3
    participant CO2 as CO2 Controller

    ESP->>CO2: BLE connect
    ESP->>CO2: AUTH
    ESP->>CO2: RTC
    ESP->>CO2: RTC (2nd)
    ESP->>CO2: RESET_SCHEMA (clears all slots)
    alt schema active
        ESP->>CO2: SCHEMA slot → CO2_ON  at (fotoperiode_start − prestart)
        ESP->>CO2: SCHEMA slot → CO2_OFF at fotoperiode_eind
    else schema inactive
        ESP->>CO2: SCHEMA slot → CO2_EMPTY
        ESP->>CO2: SCHEMA slot → CO2_EMPTY
    end
    ESP->>CO2: BLE disconnect
    Note over CO2: Opens/closes valve autonomously on internal RTC
```

`RESET_SCHEMA` clears all existing slots — no old values need to be tracked when times change.

> **Note:** The CO2 valve physically actuates (audible click) on every connect. This is normal — the controller briefly opens and then closes the valve as it evaluates the new schedule against the current time. Avoid connecting more often than necessary.

---

## Magnetic Stirrer (4-channel)

```mermaid
sequenceDiagram
    participant ESP as ESP32-S3
    participant STI as Stirrer

    ESP->>STI: BLE connect
    ESP->>STI: AUTH
    ESP->>STI: RTC
    loop for each channel 0..3
        ESP->>STI: STIR_ENABLE (channel)
        ESP->>STI: STIR_SPEED  (channel, 0–127)
        ESP->>STI: STIR_TIMER  (channel, duration, interval)
    end
    ESP->>STI: STIR_APPLY
    ESP->>STI: STIR_RESTORE (on/off bitmask all 4 channels)
    ESP->>STI: BLE disconnect
    Note over STI: Runs timer cycles autonomously
```

Channel on/off state is stored in NVS globals (`restore_value: true`) so after an ESP reboot the stirrer returns to the last-known state. Speed encoding: HA 0–100% → device 0–127.

---

## Cooling Fan

```mermaid
sequenceDiagram
    participant ESP as ESP32-S3
    participant FAN as Cooling Fan

    loop Every 5 minutes
        ESP->>FAN: BLE connect
        ESP->>FAN: AUTH
        ESP->>FAN: RTC
        ESP->>FAN: RTC (2nd)
        ESP->>FAN: AUTH_EXT1 (device-specific)
        ESP->>FAN: AUTH_EXT2 (device-specific)
        ESP->>FAN: SET_MODE (silent on/off)
        ESP->>FAN: TEMP_THRESH (start °C, max °C)
        ESP->>FAN: FAN_SPEED (0 = auto)
        Note over ESP: Wait 2s for notification
        FAN-->>ESP: Notification: fan%, room temp, water temp, humidity
        ESP->>FAN: BLE disconnect
    end
    Note over FAN: Controls speed autonomously based on water temp
```

Notifications (`x[4] == 0x01`):

| Byte | Value |
|---|---|
| `x[5]` | Fan speed (%) |
| `x[6:7] / 256` | Room temperature (°C) |
| `x[11] / 10` | Water temperature (°C) |
| `x[12]` | Humidity (%) |

---

## Doctor Mate

```mermaid
sequenceDiagram
    participant ESP as ESP32-S3
    participant DOC as Doctor Mate

    ESP->>DOC: BLE connect
    ESP->>DOC: AUTH (DEVICE header 0xa5)
    ESP->>DOC: RTC
    ESP->>DOC: SETTINGS pos1 → TDS (EC µS/cm)
    ESP->>DOC: SETTINGS pos2 → Volume (liters × 2)
    ESP->>DOC: BLE disconnect
    Note over DOC: Doses automatically based on EC measurement
```

TDS and Volume use **identical frames** — device distinguishes them **only by send order**. Always send both in this order.

Encoding: EC (µS/cm) = ppm ÷ 0.4. Volume = liters × 2.

---

## WRGB II

> Protocol verified via btsnoop HCI analysis (2026-05-30).

```mermaid
sequenceDiagram
    participant ESP as ESP32-S3
    participant WRG as WRGB2 Light

    ESP->>WRG: BLE connect
    ESP->>WRG: AUTH
    ESP->>WRG: RTC
    ESP->>WRG: RTC (2nd)
    WRG-->>ESP: Notification (current state)
    alt auto mode
        ESP->>WRG: RESET_SCHEMA
        ESP->>WRG: SCHEDULE (on_h, on_m, off_h, off_m, ramp_min, weekdays, R, G, B)
        ESP->>WRG: RESET_AUTO (switch to auto mode)
        ESP->>WRG: RTC (triggers immediate schedule evaluation)
    else manual mode
        ESP->>WRG: BRIGHTNESS R
        ESP->>WRG: BRIGHTNESS G
        ESP->>WRG: BRIGHTNESS B
    end
    ESP->>WRG: BLE disconnect
    Note over WRG: Follows schedule with fade — no BLE needed
```

Schedule data: `[on_h, on_m, off_h, off_m, ramp_min, weekdays, R, G, B, 0xff×5]`

- `ramp_min`: fade duration 0–150 min. **Value 90 is forbidden** (= 0x5a frame header) → use 89.
- `weekdays`: bitmask Mon=64..Sun=1; 127 = every day.
- `R/G/B = 0xff` = delete marker (deactivate schedule).

---

## Dosing Pump

> **Skeleton only.** Protocol not yet reverse-engineered.

Connect + auth + notification logging is implemented. To reverse-engineer: flash the skeleton, trigger a manual dose in the Chihiros app while watching ESPHome logs, identify the command bytes.

---

## Auto-detection

Every Chihiros device advertises a BLE name in the format `DY{type}{MAC}` — the type prefix is encoded directly in the name. The firmware uses this to automatically identify any Chihiros device within range that is not yet in your configuration:

```
[I][ble_scan]: Chihiros found: WRGB2 light         -> set as wrgb2_mac | MAC=CF:20:3B:6D:17:C1 RSSI=-62
[I][ble_scan]: Chihiros found: CO2 controller      -> set as co2_mac   | MAC=CC:A0:27:8E:79:E9 RSSI=-58
[I][ble_scan]: Chihiros found: Magnetic stirrer    -> set as roerder_mac | MAC=D3:A1:88:0F:7C:42 RSSI=-71
```

```mermaid
sequenceDiagram
    participant ESP as ESP32-S3 (scanning)
    participant WRG as WRGB2 (not yet configured)

    WRG-->>ESP: BLE advertise "DYNT90CF203B6D17C1"
    Note over ESP: prefix DYNT90 → WRGB2 light<br/>MAC = CF:20:3B:6D:17:C1
    ESP->>ESP: log "WRGB2 light -> set as wrgb2_mac | MAC=CF:20:3B:6D:17:C1"
```

Known prefixes:

| BLE name prefix | Device type | Substitution variable |
|---|---|---|
| `DYNT90` | WRGB2 light | `wrgb2_mac` |
| `DYPCO2` | CO2 controller | `co2_mac` |
| `DYMIX` | Magnetic stirrer | `roerder_mac` |
| `DYNFAN` | Cooling fan | `ventilator_mac` |
| `DYNDOC` | Doctor Mate | `doctor_mac` |
| `DYDOSE` | Dosing pump | `dosing_mac` |

Devices already present in your substitutions are silently ignored — only unconfigured devices are logged. Once you have all MACs, fill them in and reflash.

---

## ESPHome Tips

- State persistence across reboots: use `globals` with `restore_value: true`. The lambda `return id(my_global)` reflects current state even after a crash/reboot.
- `script.execute` with inline `{ }` parameter syntax is unreliable from `turn_on/turn_off_action`. Use inline `ble_client.ble_write` instead.
- Secrets required: `wifi_sid`, `wifi_password`, `encryption_key`, `ota_password`, `web_user`, `web_password`.
