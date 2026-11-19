# ElementIQ v1.0.1 – Smart Geyser / Water Heater Control

ESP32-C3-WROOM-02-N4 firmware: ADS1115 + NTC + relay, MQTT/HA, multi-page web UI.

## Modes

| Mode | Behaviour |
|------|-----------|
| **Off** | Heater forced off |
| **Auto** | Heat to setpoint; off; re-on at setpoint − hysteresis (default 5 °C). Respects schedule window. |
| **Boost** | Once-off heat to boost temp, then Auto. Optional soft-min (only if temp below threshold). |

## Features

- **Schedule** – enable/disable daily heat window (overnight OK). Outside window → Auto cannot heat; Boost still allowed.
- **Presets** – Home / Away / Eco setpoints (web + MQTT).
- **Runtime** – heater hours today / week / total (MQTT + Stats page).
- **Temp graph** – ~2 h history on Stats page (`/api/history`).
- **Sensor fault** – invalid readings force the relay off immediately; after 3 min they also raise the sensor-fault indicator. ADS1115 initialization is retried, and heating remains blocked until valid readings return.
- **Soft min boost** – optional °C threshold; boost skipped if already warmer.
- **Factory reset** – Update page button, or hold **BOOT (GPIO9)** 5 s at boot.
- **LED** – GPIO8 on when WiFi connected (active LOW on many boards).
- **OTA** – ArduinoOTA + optional GitHub `firmware.bin` releases.
- **Setup AP** – Starts immediately and is open on first boot. Configure a password from the captive portal or Connections page (8–31 characters); leave it open or explicitly clear it in Connections.
- **Nonblocking networking** – Station connection, portal scans, and GitHub OTA run asynchronously so normal heater-control updates continue.
- **Settings storage** – EEPROM records carry a schema version and CRC32; valid legacy records are migrated on startup, while corrupt or unsupported records are rejected with a serial diagnostic.

## Hardware (ESP32-C3-WROOM-02-N4)

| Part | Pin |
|------|-----|
| Relay | GPIO7 |
| ADS1115 SDA/SCL | GPIO4 / GPIO6 |
| ADS1115 ADDR | GND → I2C address 0x48 |
| NTC | ADS1115 A0 (3.3V — NTC — A0 — ~10.98 kΩ — GND) |
| Status LED | GPIO8 (optional) |
| Factory reset | GPIO9 (BOOT button) |

## Build

```bash
pio run -t upload
pio run -t uploadfs
```

Board: `esp32-c3-devkitm-1` (or compatible ESP32-C3 with 4 MB flash).

Focused thermostat/sensor tests are in `test/test_control/`. Build the test firmware without uploading or executing it with `pio test -e esp32-c3 --without-uploading --without-testing`; executing the Unity tests requires a connected ESP32-C3.

## Source layout

`src/main.cpp` contains the Arduino entry points and includes the ordered subsystem fragments in `src/modules/`:

- `controller.inc` – settings, sensor, thermostat, and runtime accounting
- `mqtt_ota.inc` – MQTT and firmware updates
- `api.inc` – HTTP API and settings input
- `web_ui.inc` – embedded web pages
- `portal.inc` – Wi-Fi setup portal

These fragments intentionally remain in one translation unit for now so existing shared state and callback behavior stay unchanged. Moving them into independent `.cpp` modules should follow after shared state is replaced with explicit interfaces.

## Web pages

| Path | Content |
|------|---------|
| `/` | Dial, Off/Auto/Boost, Home/Away/Eco |
| `/connections` | AP + MQTT |
| `/time` | Timezone |
| `/temperature` | Setpoints, hysteresis, min on/off, soft min boost |
| `/schedule` | Enable + on/off times |
| `/stats` | Runtime hours + temperature graph |
| `/update` | OTA + factory reset |

## MQTT (base `elementiq/<chipid>/`)

State JSON includes: temperature, setpoint, boost_temp, mode, preset, heater, sensor_fault, schedule_active, runtime_*_h.

Commands: `cmd/setpoint`, `cmd/boost_temp`, `cmd/mode`, `cmd/preset` (`boost`/`none`/`home`/`away`/`eco`).

Access point default: **ElementIQ-Setup** (open, no password)  
mDNS: **ElementIQ.local**

## Recent fixes (2026-11-16)

- **Serial on ESP32-C3**: Removed the `Serial0` redefine so that native USB CDC works with the DevKitM-1. Open the USB CDC port (not a secondary UART) at 115200.
- **WiFi scan on captive portal**: Scan is delayed slightly after softAP starts, results are logged, the portal page polls every 2.5 s, and an empty list triggers a new scan request. Networks should appear within a few seconds of joining the AP.
- Boot order remains: WiFi/AP first, then `server.begin()`.
- LittleFS is mounted with format-on-fail (`LittleFS.begin(true)`).

### Scan reliability (follow-up)
- Replaced unreliable async scan with a short **blocking** scan for the captive portal.
- Call `WiFi.disconnect(false)` when STA is not connected before scanning (fixes common AP+STA -2 failure).
- Portal still polls every 2.5 s; scan interval reduced to 8 s.
