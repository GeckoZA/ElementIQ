# ElementIQ v1.0.1.17 – Smart Geyser / Water Heater Control

ESP32-C3-WROOM-02-N4 firmware: ADS1115 + NTC + relay, MQTT/HA, multi-page web UI.

## Modes

| Mode | Behaviour |
|------|-----------|
| **Off** | Heater forced off |
| **Auto** | Heat to setpoint; off; re-on at setpoint − hysteresis (default 5 °C). Respects schedule window. |
| **Boost** | Once-off heat to boost temp, then Auto. Optional soft-min (only if temp below threshold). |

## Features

- **Schedule** – enable/disable daily heat window (overnight OK, e.g. 22:00 → 02:00). Outside window → Auto cannot heat; Boost still allowed.
- **Presets** – Home / Away / Eco setpoints (web + MQTT).
- **Runtime** – heater hours today / week / total (MQTT + Stats page).
- **Temp graph** – ~2 h history on the Home page (`/api/history`).
- **Sensor fault** – invalid readings force the relay off immediately; after 3 min they also raise the sensor-fault indicator. ADS1115 initialization is retried, and heating remains blocked until valid readings return.
- **Soft min boost** – optional °C threshold; boost skipped if already warmer.
- **Factory reset** – Update page button, or hold **BOOT (GPIO9)** 5 s at boot. Also clears the admin login and the setup-complete flag.
- **Reboot** – Settings → About → **Reboot device**.
- **Status LED** – analog RGB LED, see [Status LED](#status-led).
- **OTA** – web upload of `firmware.bin` (Update page) and optional GitHub releases. See [OTA](#ota).
- **First-time setup wizard** – on a new unit the captive portal walks through Wi-Fi → password → time zone (auto-detected from the browser) → connect.
- **Nonblocking networking** – Station connection, portal scans, and GitHub OTA run asynchronously so normal heater-control updates continue.
- **Settings storage** – EEPROM records carry a schema version and CRC32; valid legacy records are migrated on startup, while corrupt or unsupported records are rejected with a serial diagnostic.

## Safety

- **Max setpoint is 80 °C** (`MAX_SETPOINT`). The tank has a 90 °C physical cut-out; this keeps a 10 °C margin. Older saved setpoints above 80 are clamped, not discarded.
- **Software over-temperature trip at 85 °C** (`TEMP_HARD_TRIP`): heater locked off, mode set to Off, fault latched until the tank is below 80 °C and the fault is acknowledged on the Home page.
- **Relay is always off when the firmware isn't sure:**
  - driven LOW as the first line of `setup()`
  - held off for 20 s after every boot (`BOOT_HEATER_HOLD_MS`)
  - forced off before, during and after any firmware update, and until the new image has been confirmed healthy (`update_probation`, `OTA_CONFIRM_MS` = 60 s)
  - forced LOW before every restart (`safeRestart()`)
  All of this is enforced in one place, `heaterInterlock()`, and shown as the "blocked" reason in the UI/MQTT.
- **Task watchdog** (`WDT_TIMEOUT_S` = 20 s) resets the chip if `loop()` stalls.
- **Fault indications:** sensor fault, element fault ("no temperature rise") and over-temperature each lock the heater off and are visible in the UI, MQTT and the status LED.

## Hardware (ESP32-C3-WROOM-02-N4)

| Part | Pin |
|------|-----|
| Relay | GPIO7 |
| ADS1115 SDA/SCL | GPIO4 / GPIO6 |
| ADS1115 ADDR | GND → I2C address 0x48 |
| NTC | ADS1115 A0 (3.3V — NTC — A0 — ~10.98 kΩ — GND) |
| Status LED (analog RGB) | **R = GPIO10, G = GPIO5, B = GPIO3** (each via its own series resistor) |
| Factory reset / "confirm" button | GPIO9 (BOOT button) |
| Reserved (future accessories) | GPIO0, GPIO1 (leak-detect / spare); water-pressure sensor on ADS1115 A1 |

The ESP32-C3 strapping pins are GPIO2, GPIO8 and GPIO9 (GPIO9 is only used for the BOOT button). GPIO18/19 are USB and GPIO20/21 are UART0, so none of those are used for the LED. Set `STATUS_LED_COMMON_ANODE` in `include/config.h` to 1 for a common-anode LED.

## Status LED

| LED | Meaning |
|---|---|
| White | Booting |
| Blue, fast blink | Firmware update / restarting |
| Red, fast blink | Over-temperature or element fault (heater locked off) |
| Red, slow blink | Temperature sensor fault |
| Magenta, breathing | Waiting for first-time setup |
| Blue, blink | Joining Wi-Fi |
| Amber, slow blink | Wi-Fi lost / hotspot active |
| Orange, solid | Heating |
| Dim white | Mode OFF |
| Green, breathing | Idle, everything OK |
| White, blink | BOOT button confirmation window open (units without a password, see below) |

## OTA

- **Web upload** (Update page): raw `firmware.bin`. The image must start with the ESP magic byte, be built for the ESP32-C3 and contain the `ElementIQ-FW` marker. The marker is searched for across the whole image and across network chunk boundaries (`include/ota_marker.h`). Host test: `g++ -std=c++17 -Iinclude tools/test_marker.cpp -o /tmp/tm && /tmp/tm`.
- **GitHub release**: the tag must be newer than the installed version (e.g. `v1.0.1.17` > `v1.0.1.14`) and have a `firmware.bin` asset. Downloads are encrypted but the server certificate is **not** verified (unchanged from v1.0.1.14). Parked work to verify it is in `extras/tls_pinning/` (not built).
- **Rollback:** after an update the new image has 60 s to prove itself. If it crashes or watchdog-resets twice (`OTA_MAX_CRASH_BOOTS`) before then, the previous slot is booted again. Serial shows `[BOOT] ...` lines.
- `ENABLE_ARDUINO_OTA` is 0 (LAN OTA has no authentication). Set to 1 for development only.

## Web security

- **Login:** cookie sessions (`HttpOnly; SameSite=Strict`), salted + iterated SHA-256 password hash, lock-out after 5 failed attempts (60 s). Sessions expire after 30 min idle / 12 h total. The login page is `/login`; **Sign out** is on the About page.
- **Request-forgery protection:** every non-GET request must carry an `X-EIQ-CSRF` token (obtained from `GET /api/session`) and, if the browser sends an `Origin`, it must match the device. A page on another site cannot do this, so it cannot send commands to the heater. This stays on even with no password.
- **First-time setup:** the wizard asks for a password (on by default) and a hotspot password. The setup portal (`/scan`, `/connect`, `/portal-status`) is only open until the wizard completes; afterwards it needs a login.
- **Open mode (no password):** the wizard has a "Protect with a password" box that can be unticked. In open mode, firmware upload/install, factory reset and Wi-Fi changes need the BOOT button held for 3 s (60 s window, LED blinks white). A password can be added later under **Settings → Web security**.
- **Hotspot:** the controller's own Wi-Fi (default name `ElementIQ-Setup`) has a password chosen in the wizard and is off once the unit is on your Wi-Fi, unless you ticked "keep hotspot on". It comes back automatically if your Wi-Fi is lost.
- Traffic is plain HTTP on port 80 (not encrypted) – use only on a trusted LAN. MQTT and the stored Wi-Fi/MQTT passwords are not encrypted.

## Storage map

| Location | Contents |
|----------|----------|
| **EEPROM** | Wi‑Fi SSID/password, MQTT, setpoints, schedule, theme, AP settings, mode/preset |
| **NVS** `eiq` | First-time-setup-complete flag |
| **NVS** `eiq-boot` | OTA probation / crash counter (rollback) |
| **LittleFS** `/logo.png` | Brand logo (web UI) |
| **LittleFS** `/serial.txt` | Device serial (survives OTA) |
| **LittleFS** `/webauth.txt` | Admin username + salted password hash (never the password) |
| **LittleFS** `/hist.bin` | Temperature history ring (7 days @ 5 min → 2016 samples, also held in RAM) |
| **LittleFS** `/runtime.bin` | Heater runtime counters (today / week / month / total) |
| **LittleFS** `/ota_ok.txt` | One-shot OTA success marker |

Partition table `partitions_elementiq.csv` (4 MB flash): **~1.69 MB** per OTA app slot, **~576 KB** LittleFS. Flash is tight (≈95 %+ of the app slot), so the web pages are stored **gzip-compressed** in `src/generated/web_gz.inc`; keep it that way.

## Build

```bash
pio run -t upload
pio run -t uploadfs
```

Board: `esp32-c3-devkitm-1` (or compatible ESP32-C3 with 4 MB flash). Check the flash percentage in the build output after any change.

Focused thermostat/sensor tests are in `test/test_control/`. Build the test firmware without uploading or executing it with `pio test -e esp32-c3 --without-uploading --without-testing`; executing the Unity tests requires a connected ESP32-C3.

## Source layout

`src/main.cpp` contains the Arduino entry points and includes the ordered subsystem fragments in `src/modules/`:

- `controller.inc` – settings, sensor, thermostat, heater interlock, password hashing, runtime accounting
- `mqtt_ota.inc` – MQTT and GitHub firmware updates
- `api.inc` – sessions/login/CSRF, HTTP API, settings input, web firmware upload
- `web_ui.inc` – page routes for the embedded web pages
- `portal.inc` – Wi-Fi setup portal and wizard backend
- `system.inc` – watchdog, safe restart, OTA boot guard/rollback, BOOT-button confirmation, status LED

Other folders:

- `include/` – `config.h` (all tunables), `control_logic.h`, `ota_marker.h`
- `src/generated/web_gz.inc` – the compressed web pages
- `web_src/` – readable source for the login page and the setup wizard
- `tools/web_gz_tool.py` – `extract` the compressed pages to HTML for editing, `pack` them back
- `tools/test_marker.cpp` – host test for the firmware-marker scanner
- `extras/tls_pinning/` – parked, not built (see OTA)

These fragments intentionally remain in one translation unit for now so existing shared state and callback behavior stay unchanged. Moving them into independent `.cpp` modules should follow after shared state is replaced with explicit interfaces.

## Web pages

| Path | Content |
|------|---------|
| `/` | Dial, Off/Auto/Boost, Home/Away/Eco, temperature graph, runtime |
| `/schedule` | Enable + on/off times |
| `/connections` | Wi-Fi, hotspot, MQTT |
| `/settings` | Device, time zone, temperatures, appearance, web security |
| `/update` | Firmware upload, GitHub OTA, factory reset |
| `/about` | Version, uptime, IP, reboot, sign out |
| `/console` | Live log |
| `/login` | Sign-in (shown when a password is set) |
| `/time`, `/temperature`, `/stats` | Redirect to `/settings` or `/` |

## MQTT (base `elementiq/<chipid>/`)

State JSON includes: temperature, setpoint, boost_temp, mode, control, preset, preset_select, heater, sensor_fault, element_fault, schedule_active, runtime_*_h.

- **control** / **mode**: `off` | `auto` | `boost`
- **preset_select**: `none` | `home` | `away` | `eco`

Commands:

- `cmd/setpoint`, `cmd/boost_temp`, `cmd/hysteresis`
- `cmd/control` or `cmd/mode` — **Control**: `off` / `auto` / `boost`
- `cmd/preset` — **Presets**: `none` / `home` / `away` / `eco`
- `cmd/heater` (`ON` / `OFF`)

Home Assistant discovery publishes a **climate** entity plus two **select** entities:
**Control** (Off / Auto / Boost) and **Preset** (None / Home / Away / Eco).

mDNS: **ElementIQ.local**

## Changes in v1.0.1.17 (since v1.0.1.14)

- **Fix:** device time showed UTC. `configTime()` resets the timezone, so `TZ` is now set after it.
- **Fix:** overnight schedules (e.g. 22:00–02:00) no longer also heat in the early hours of the same day.
- **New:** Reboot button on the About page.
- **Safety:** 80 °C max setpoint, 85 °C software trip, watchdog, relay-off interlock around boot/OTA, OTA rollback after crashes.
- **Security:** session login + CSRF protection (replaces HTTP Basic), hashed password, setup portal closed after setup, LAN ArduinoOTA off by default.
- **New:** first-time setup wizard (Wi-Fi, optional password, hotspot password, auto-detected time zone).
- **New:** analog RGB status LED (GPIO10/5/3).
- **Fix:** web firmware upload could reject a valid image (marker only searched in the first 256 KB and not across chunk boundaries) and could show "Network error" after a successful update (restart raced the reply). Same marker fix applied to GitHub installs.
- **Fix (OTA keep):** new firmware was rolled back to the previous version after a reboot because confirmation required a valid NTC reading. Confirmation now only needs a clean 60 s run (sensor faults no longer force rollback). Leave the unit powered for ≥60 s after an OTA reboot so the image is marked permanent.
- **Fix (factory reset):** holding BOOT across the restart could loop forever with no AP. Reset now waits for the button to be released before rebooting. Stored Wi-Fi credentials are fully erased (`disconnect(true, true)`) so the captive portal at 192.168.4.1 comes up cleanly.
- **Safety:** relay forced off while first-time setup is incomplete (`!setupDone`). Default mode is Off (was Auto), so a factory-reset or new unit cannot heat during the setup portal after the 20 s boot hold expires.
- GitHub OTA TLS behaviour is unchanged from v1.0.1.14.

## Earlier changes

### v1.0.1.7
- Web dialogs use custom modals titled **ElementIQ Message / Warning / Error** (no more browser “elementiq.local says”).
- HA MQTT: `operation` state + `select` discovery for Off / Auto / Boost / Home / Away / Eco.
- Removed unused `logo64.png` from filesystem image.

### 2026-11-16
- **Serial on ESP32-C3**: Removed the `Serial0` redefine so that native USB CDC works with the DevKitM-1. Open the USB CDC port (not a secondary UART) at 115200.
- **WiFi scan on captive portal**: Scan is delayed slightly after softAP starts, results are logged, the portal page polls every 2.5 s, and an empty list triggers a new scan request. Networks should appear within a few seconds of joining the AP.
- Boot order remains: WiFi/AP first, then `server.begin()`.
- LittleFS is mounted with format-on-fail (`LittleFS.begin(true)`).
- Replaced unreliable async scan with a short **blocking** scan for the captive portal; call `WiFi.disconnect(false)` when STA is not connected before scanning (fixes common AP+STA -2 failure); portal polls every 2.5 s; scan interval 8 s.
