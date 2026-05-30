# Chihiros BLE Bridge — ESPHome

ESPHome configuration for an ESP32-S3 BLE bridge that controls Chihiros aquarium devices via Home Assistant, without the Chihiros app.

## Hardware

- **Board**: ESP32-S3-N16R8 (`esp32-s3-devkitc-1`, `variant: esp32s3`, `flash_size: 16MB`)
- **Framework**: `esp-idf` (required for reliable multi-client BLE)
- **BLE scan**: `interval: 320ms`, `window: 30ms`, continuous

The ESP32 can manage multiple BLE clients simultaneously (CO2, stirrer, fan, Doctor Mate) alongside the active scanner. Crashes can occur under BLE stack pressure — state persistence via NVS globals is essential (see below).

## Supported Devices

| Device | MAC (example) | Status |
|---|---|---|
| CO2 Controller | `CC:A0:27:8E:79:E9` | Working |
| Magnetic Stirrer | `EC:DE:A6:A0:61:1D` | Working |
| Cooling Fan | `D7:65:2F:EF:CC:BB` | Working |
| Doctor Mate | `CC:A0:E9:07:34:C6` | Config ready, pending flash |
| WRGB II | — | TODO |
| Dosing Pump | — | TODO |

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
| `header` | `0x5a` for auth/RTC/basic commands, `0xa5` for device-specific commands |
| `len` | Total byte count from `0x01` through the last data byte (inclusive) |
| `seq` | Incrementing counter per device, reset on every reconnect |
| `cmd` | Command code (device-specific) |
| `data` | Payload, variable length |
| `XOR-CRC` | XOR of all frame bytes excluding header and CRC itself |

The helper function `chihiros::pakket(header, cmd, {data...}, seq)` in `chihiros_ble.h` handles framing and CRC.

### State Persistence After Crash

The ESP32 crashes occasionally under BLE load. Template switches with `restore_mode: RESTORE_DEFAULT_OFF` report the wrong state to HA after a restart (OFF) while the physical device stayed ON. Fix: use a `global` with `restore_value: true` as the source of truth.

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
      - ble_client.ble_write: ...   # write BLE directly, not via script.execute with params
```

> **Note:** `script.execute` with inline `{ }` parameter syntax does not work reliably from `turn_on/turn_off_action`. Write BLE commands inline.

---

## CO2 Controller

### Connect Sequence
1. Auth: `0x5a` cmd=`0x04` data=`[0x01]`
2. RTC sync: `0x5a` cmd=`0x09` data=`[year-2000, month, weekday, hour, min, sec]`
3. Reset Auto Settings: `0x5a` cmd=`0x05` data=`[0x07, 0xFF, 0xFF]`

Only steps 1–3 on connect. **Do not resend the schedule on connect** — this causes a solenoid tick.

### Schedule Update
Send all three in sequence:
1. Schedule ON time: `0x5a` cmd=`0x16` data=`[hour, min, 0x64]`
2. Schedule OFF time: `0x5a` cmd=`0x16` data=`[hour, min, 0x00]`
3. Switch to Auto Mode: `0x5a` cmd=`0x05` data=`[0x12, 0xFF, 0xFF]`

---

## Magnetic Stirrer (4-channel)

### Connect Sequence
1. Auth: `0x5a` cmd=`0x04` data=`[0x01]`
2. RTC sync: `0x5a` cmd=`0x09`
3. For each channel (0–3): Enable + Speed + Timer
4. Apply: `0xa5` cmd=`0x1f` data=`[]`
5. Restore on/off state per channel

### Commands

| Action | Header | Cmd | Data |
|---|---|---|---|
| Enable channel | `0xa5` | `0x20` | `[channel, 0x01]` |
| Set speed | `0xa5` | `0x1b` | `[channel, speed_0_127]` |
| Set timer | `0xa5` | `0x15` | `[channel, on_min, interval_min]` |
| Apply config | `0xa5` | `0x1f` | `[]` |
| Toggle channel (runtime) | `0xa5` | `0x14` | `[ff ff ch0 ch1 ch2 ch3 ff ff ff ff]` |

For the runtime toggle (cmd=`0x14`): set the byte at position `2+channel` to `0x01` (on) or `0x00` (off); all other channel positions use `0xff` (do not touch).

Speed encoding: HA 0–100% → device 0–127 (multiply by `127/100`).

---

## Cooling Fan

### Connect Sequence (exact order matters)
1. Auth: `0xa5` cmd=`0x04` data=`[0x01]`
2. RTC sync ×2: `0x5a` cmd=`0x09` (app sends it twice)
3. Extra auth `[06]`: `0xa5` cmd=`0x04` data=`[0x06]`
4. Extra auth `[08]`: `0xa5` cmd=`0x04` data=`[0x08]`
5. Mode init: `0x5a` cmd=`0x05` data=`[mode, 0xff, 0xff]`

### Commands

| Action | Header | Cmd | Data |
|---|---|---|---|
| Silent mode ON | `0x5a` | `0x05` | `[0x22, 0xff, 0xff]` |
| Silent mode OFF | `0x5a` | `0x05` | `[0x23, 0xff, 0xff]` |
| Temperature threshold | `0xa5` | `0x21` | `[start_°C, max_°C, 0xff]` |
| Manual speed | `0x5a` | `0x07` | `[0xff, speed_0_20]` (0 = auto) |

> **Note:** Silent mode values are inverted: `0x22` = ON, `0x23` = OFF.

### Notifications (when `x[4] == 0x01`)

| Byte | Value |
|---|---|
| `x[5]` | Fan speed (%) |
| `x[6:7] / 256` | Room temperature (°C) |
| `x[11] / 10` | Water temperature (°C) |
| `x[12]` | Humidity (%) |

---

## Doctor Mate

> Config implemented, not yet tested in the field. Notification format is partially confirmed.

### BLE
Advertises Nordic UART UUID — same service/characteristics as other Chihiros devices.  
GATT handles (for reference): TX write = `0x0010`, RX notify = `0x0012`, CCCD = `0x0013`.

### Connect Sequence
1. Auth: `0xa5` cmd=`0x04` data=`[0x01]`
2. RTC sync: `0x5a` cmd=`0x09`
3. TDS target: `0xa5` cmd=`0x01` data=`[0x00, ec_µS_per_cm]` ← **position 1**
4. Volume: `0xa5` cmd=`0x01` data=`[0x00, liters × 2]` ← **position 2**

> **Critical:** TDS and Volume use identical frame format (same cmd, same `data[0]=0x00`). The device distinguishes them **only by order**: first write after RTC = TDS, second = Volume. Always send both, always in this order.

### Settings Encoding

| Setting | Input | Encoding | Example |
|---|---|---|---|
| TDS | ppm | EC (µS/cm) = ppm ÷ 0.4 | 100 ppm → 250 = `0xfa` |
| Volume | liters | liters × 2 | 50 L → 100 = `0x64` |

Single-byte values → max ~102 ppm TDS (EC 255 µS/cm), max ~127 L volume.

### Profiles (TDS presets)

| Profile | TDS | EC (µS/cm) | Byte |
|---|---|---|---|
| Plant | 80 ppm | 200 | `0xc8` |
| Fish | 93 ppm | 233 | `0xe9` |
| Shrimp | 66 ppm | 166 | `0xa6` |

Profiles are just TDS presets — selecting a profile = sending that EC value as TDS.

### Notifications

Known notification after auth: `[5b 02 0a 00 01 0a 01 ff ff ff ff 02]`  
`byte[5]` = `0x0a` (10) — believed to be EC measurement in µS/cm.  
`0xff 0xff 0xff 0xff` = sensors not yet ready.

---

## ESPHome Tips

- Flash via the **ESPHome dashboard** (Web Serial in Chrome/Edge) — OTA is unreliable with an active BLE scanner on this board.
- Use `logger: level: INFO` with `hardware_uart: USB_SERIAL_JTAG` for serial logging over USB.
- `secrets.yaml` is not included. Required secrets: `wifi_ssid_iot`, `wifi_password_iot`, `encryption_key`, `ota_password`.
