# Chihiros BLE Bridge — ESPHome

ESPHome configuration for an ESP32-S3 BLE bridge that controls Chihiros aquarium devices via Home Assistant, without the Chihiros app.

## Hardware

- **Board**: ESP32-S3-N16R8 (`esp32-s3-devkitc-1`, `variant: esp32s3`, `flash_size: 16MB`)
- **Framework**: `esp-idf` (required for reliable multi-client BLE)
- **BLE scan**: `interval: 320ms`, `window: 30ms`, continuous

The ESP32 manages multiple BLE clients simultaneously alongside the active scanner. Crashes can occur under BLE stack pressure — state persistence via NVS globals is essential (see below).

## Supported Devices

| Device | MAC | Status |
|---|---|---|
| CO2 Controller | `CC:A0:27:8E:79:E9` | Working |
| Magnetic Stirrer | `EC:DE:A6:A0:61:1D` | Working |
| Cooling Fan | `D7:65:2F:EF:CC:BB` | Working |
| Doctor Mate | `CC:A0:E9:07:34:C6` | Config ready, pending flash |
| WRGB II | TBD | Config ready, pending MAC |
| Dosing Pump | TBD | Skeleton ready, pending MAC + protocol |

## Helper Library (`chihiros_ble.h`)

All protocol helpers are in `chihiros_ble.h`. Use these instead of raw hex literals.

### Headers
```cpp
chihiros::hdr::BASE   // 0x5a — auth, RTC, CO2, fan mode/speed
chihiros::hdr::DEVICE // 0xa5 — stirrer, fan threshold, Doctor Mate, WRGB2 schedule
```

### Commands
```cpp
chihiros::cmd::AUTH        // 0x04
chihiros::cmd::RTC         // 0x09
chihiros::cmd::MODE        // 0x05 — CO2 reset / fan mode init
chihiros::cmd::SCHEMA      // 0x16 — CO2 schedule time slots
chihiros::cmd::BRIGHTNESS  // 0x07 — WRGB2 per-channel brightness
chihiros::cmd::FAN_SPEED   // 0x07 — fan manual speed (same byte as BRIGHTNESS)
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
chihiros::data::SILENT_ON    // 0x22 — fan silent mode on (note: inverted)
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
// Build a complete BLE frame with CRC
chihiros::pakket(header, cmd, {data...}, seq)
chihiros::pakket(header, cmd, std::vector<uint8_t>, seq)  // vector overload

// Build RTC sync packet (encodes weekday correctly)
chihiros::rtc_pakket(ESPTime t, seq)

// Stirrer channel toggle (sets one channel, SKIP on others)
chihiros::roerder_toggle(seq, channel, on)

// Safe sequence increment — skips 0x5a (required for WRGB2)
chihiros::next_seq(uint8_t& counter)
```

> **Important:** The sequence byte must never equal `0x5a` (90) as this is the frame header byte. Use `next_seq()` for WRGB2 where the counter may wrap around during a session. Other devices rarely reach seq=90 within a single connection.

---

## General Protocol

All Chihiros devices use **Nordic UART Service (NUS)** over BLE:

| Role | UUID |
|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (write) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (notify) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

### Frame Format

```
[header] [0x01] [len] [0x00] [seq] [cmd] [data...] [XOR-CRC]
```

| Field | Description |
|---|---|
| `header` | `0x5a` (BASE) or `0xa5` (DEVICE) |
| `len` | 5 + data length |
| `seq` | Incrementing counter per device, reset on every reconnect |
| `cmd` | Command code |
| `XOR-CRC` | XOR of all frame bytes excluding header and CRC itself |

### State Persistence After Crash

```yaml
globals:
  - id: device_on
    type: bool
    restore_value: true      # survives crash via NVS flash

switch:
  - platform: template
    lambda: return id(device_on);   # no restore_mode here
    turn_on_action:
      - lambda: id(device_on) = true;
      - ble_client.ble_write: ...   # inline — not via script.execute with params
```

> `script.execute` with inline `{ }` parameter syntax does not work reliably from `turn_on/turn_off_action`.

---

## CO2 Controller

### Connect Sequence
1. Auth: `BASE`, `AUTH`, `{AUTH_BASE}`
2. RTC sync
3. `MODE` + `RESET_SCHEMA`
4. Schema slots (conditional on current time)
5. `MODE` + `RESET_SCHEMA`

**Do not resend the full schedule on connect** — this causes a solenoid tick.

### Schedule Update (`stuur_schema` script)
1. Clear both slots: `SCHEMA` `[hour, min, CO2_EMPTY]`
2. `MODE` + `RESET_AUTO`
3. Set ON slot: `SCHEMA` `[hour, min, CO2_ON]`
4. Set OFF slot: `SCHEMA` `[hour, min, CO2_OFF]`
5. `MODE` + `RESET_SCHEMA`

---

## Magnetic Stirrer (4-channel)

### Connect Sequence
1. `BASE` `AUTH` → RTC → for each channel: `STIR_ENABLE` + `STIR_SPEED` + `STIR_TIMER` → `STIR_APPLY` → restore on/off state

### Runtime Toggle
`DEVICE` `STIR_TOGGLE` `[SKIP, SKIP, ch0, ch1, ch2, ch3, SKIP, SKIP, SKIP, SKIP]`  
Set `0x01`/`0x00` at position `2+channel`; other positions use `SKIP`.

Speed encoding: HA 0–100% → device 0–127 (multiply by `127/100`).

---

## Cooling Fan

### Connect Sequence (exact order)
1. `BASE` `AUTH` `{AUTH_BASE}`
2. RTC ×2
3. `DEVICE` `AUTH` `{AUTH_EXT1}`
4. `DEVICE` `AUTH` `{AUTH_EXT2}`
5. `BASE` `MODE` `{silent_mode, SKIP, SKIP}`
6. `DEVICE` `TEMP_THRESH` `{start_°C, max_°C, SKIP}`

### Commands

| Action | Header | Cmd | Data |
|---|---|---|---|
| Silent mode | `BASE` | `MODE` | `{SILENT_ON/OFF, SKIP, SKIP}` |
| Temperature threshold | `DEVICE` | `TEMP_THRESH` | `{start_°C, max_°C, SKIP}` |
| Manual speed | `BASE` | `FAN_SPEED` | `{SKIP, speed_0_20}` (0 = auto) |

> Silent mode values are inverted: `SILENT_ON` (0x22) = ON, `SILENT_OFF` (0x23) = OFF.

### Notifications (when `x[4] == 0x01`)

| Byte | Value |
|---|---|
| `x[5]` | Fan speed (%) |
| `x[6:7] / 256` | Room temperature (°C) |
| `x[11] / 10` | Water temperature (°C) |
| `x[12]` | Humidity (%) |

---

## Doctor Mate

### Connect Sequence
1. `DEVICE` `AUTH` `{AUTH_BASE}`
2. RTC sync
3. `DEVICE` `SETTINGS` `{0x00, ec_µS_per_cm}` ← **position 1 = TDS**
4. `DEVICE` `SETTINGS` `{0x00, liters × 2}` ← **position 2 = Volume**

> **Critical:** TDS and Volume use identical frames. Device distinguishes them **only by order**. Always send both, always in this order.

### Encoding

| Setting | Encoding | Example |
|---|---|---|
| TDS | EC (µS/cm) = ppm ÷ 0.4 | 100 ppm → 250 = `0xfa` |
| Volume | liters × 2 | 50 L → 100 = `0x64` |

### Profiles (TDS presets)

| Profile | TDS | EC (µS/cm) |
|---|---|---|
| Plant | 80 ppm | 200 |
| Fish | 93 ppm | 233 |
| Shrimp | 66 ppm | 166 |

---

## WRGB II

Protocol source: [TheMicDiet/chihiros-led-control](https://github.com/TheMicDiet/chihiros-led-control)

No auth needed. Supports manual mode (direct brightness) and auto mode (internal schedule).

### Connect Sequence
- **Manual mode**: RTC → restore brightness per channel
- **Auto mode**: RTC → `stuur_wrgb2_schema` (reset + schedule + activate)

### Manual Brightness
```
BASE  BRIGHTNESS  {WRGB_R/G/B, brightness_0_100}
```

### Schedule (auto mode)
```
DEVICE  SCHEDULE  {on_h, on_m, off_h, off_m, ramp_min, weekdays, R, G, B, 0xff×5}
```

| Field | Notes |
|---|---|
| `ramp_min` | Fade duration 0–150 min. Value 90 is forbidden (= 0x5a) → use 89 |
| `weekdays` | Bitmask: Mon=64, Tue=32, Wed=16, Thu=8, Fri=4, Sat=2, Sun=1; 127=every day |
| `R/G/B` | Peak brightness 0–100 per channel |
| `0xff×5` | Padding |

Delete schedule = same frame with R/G/B = `0xff`.

#### Activate auto mode
1. `BASE` `MODE` `{RESET_SCHEMA, SKIP, SKIP}` — clear all schedules
2. `DEVICE` `SCHEDULE` `{...}` — add schedule
3. `BASE` `MODE` `{RESET_AUTO, SKIP, SKIP}` — activate
4. RTC — device evaluates schedule against current time

---

## Dosing Pump

> **Status: skeleton only.** Connect + logging implemented. Protocol not yet reverse-engineered.

### What is known
- Same Nordic UART service as other Chihiros devices
- Auth assumed identical to Doctor Mate (`DEVICE` `AUTH` `{AUTH_BASE}`) — confirm via logs

### TODO after BLE sniffing
1. Flash the skeleton config
2. Open ESPHome logs while triggering a manual dose in the Chihiros app
3. Observe `dosing: Notificatie` log lines to identify command codes
4. Map bytes for: manual dose trigger, dose volume, schedule

---

## ESPHome Notes

- Flash via **ESPHome dashboard** (Web Serial in Chrome/Edge) — OTA is unreliable with an active BLE scanner on this board.
- Use `logger: level: INFO` with `hardware_uart: USB_SERIAL_JTAG` for serial logging over USB.
- `secrets.yaml` is not included. Required secrets: `wifi_ssid_iot`, `wifi_password_iot`, `encryption_key`, `ota_password`.
