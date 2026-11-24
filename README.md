# ElementIQ v1.0.1.19 – Smart Geyser / Water Heater Control

ESP32-C3 based firmware that turns a standard geyser into a smart, schedulable, Home Assistant-ready water heater with a polished local web UI, strong safety interlocks, and over-the-air updates.

---

## Modes

| Mode | Behaviour |
|------|-----------|
| **Off** | Heater forced off |
| **Auto** | Heat to setpoint; off; re-on at setpoint − hysteresis (default 5 °C). Respects schedule window. |
| **Boost** | Once-off heat to boost temperature, then returns to Auto. Optional soft-min (boost is skipped if already warm enough). |

---

## Features

- **Daily schedule** – enable/disable a heat window (overnight ranges such as 22:00 → 02:00 work correctly). Outside the window Auto cannot heat; Boost is still allowed.
- **Presets** – Home / Away / Eco setpoints, controllable from the web UI and MQTT / Home Assistant.
- **Runtime tracking** – heater hours today, this week, this month and total (shown on the Stats page and published over MQTT).
- **Live temperature graph** – approximately 2 hours of history on the Home page.
- **Soft-min boost** – optional temperature threshold so Boost is only used when the tank is actually cold.
- **First-time setup wizard** – captive portal guides you through Wi-Fi, optional password, hotspot password and time zone (auto-detected from the browser).
- **Status LED** – full-colour RGB indication of every major state (see below).
- **OTA updates** – upload a firmware image from the web UI, or install from a GitHub release. Automatic check (never auto-install) can be enabled.
- **Factory reset** – button on the Update page, or hold **BOOT (GPIO9)** for 5 seconds at boot. Clears admin login and the setup-complete flag.
- **Reboot** – Settings → About → **Reboot device**.
- **Non-blocking networking** – Wi-Fi connection, portal scans and GitHub OTA run in the background so temperature control is never stalled.

---

## Safety

ElementIQ is designed so the heater stays off whenever the firmware is not completely sure it is safe to heat.

- **Maximum setpoint 80 °C** (`MAX_SETPOINT`). The tank’s physical cut-out is typically 90 °C; this keeps a 10 °C software margin. Older saved setpoints above 80 °C are clamped.
- **Software over-temperature trip at 85 °C** (`TEMP_HARD_TRIP`): heater locked off, mode forced to Off, fault latched until the tank cools below 80 °C and the fault is acknowledged on the Home page.
- **Relay is forced off** in all of these situations:
  - first thing in `setup()`
  - for 20 s after every boot (`BOOT_HEATER_HOLD_MS`)
  - before, during and after any firmware update, and until the new image has been confirmed healthy (60 s probation)
  - before every controlled restart
  - while first-time setup is incomplete
- **Sensor fault** – invalid readings force the relay off immediately; after 3 minutes the sensor-fault indicator is raised. Heating stays blocked until valid readings return. The ADS1115 is automatically retried.
- **Element fault (no temperature rise)** – after the heater has been on for 15 minutes the temperature must have risen by at least a temperature-dependent amount (1.5 °C near 40 °C, tapering to 0.4 °C near 70 °C). If not, the element is declared faulty, the heater is locked off and mode is forced to Off until acknowledged. A rolling window keeps long heat cycles monitored. **Cold-water draws** (e.g. shower) that drop the tank temperature by ≥ 1 °C while the element is on automatically reset the rise window, so inlet water no longer triggers a false element fault. Thresholds were relaxed in v1.0.1.18 because many real tanks rise more slowly than the earlier 3.0 / 0.8 °C in 10 min values.
- **Task watchdog** (20 s) reboots the chip if the main loop stalls.
- All faults are visible in the web UI, over MQTT and on the status LED.

---

## Status LED

| Colour / pattern | Meaning |
|------------------|---------|
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
| White, blink | BOOT button confirmation window open (open-mode units) |

---

## Web interface

| Path | Content |
|------|---------|
| `/` | Temperature dial, Off / Auto / Boost, Home / Away / Eco, graph, runtime |
| `/schedule` | Enable schedule + on/off times |
| `/connections` | Wi-Fi, hotspot, MQTT |
| `/settings` | Device name, time zone, temperatures, appearance, web security |
| `/update` | Firmware upload, GitHub OTA, factory reset |
| `/about` | Version, uptime, IP, reboot, sign out |
| `/console` | Live serial-style log |
| `/login` | Sign-in (when a password is set) |

mDNS hostname: **ElementIQ.local**

---

## Home Assistant & MQTT

Base topic: `elementiq/<chipid>/`

State JSON includes temperature, setpoint, boost temperature, mode, control, preset, heater status, sensor/element faults, schedule status and runtime hours.

Useful commands:

- `cmd/setpoint`, `cmd/boost_temp`, `cmd/hysteresis`
- `cmd/control` or `cmd/mode` → `off` / `auto` / `boost`
- `cmd/preset` → `none` / `home` / `away` / `eco`
- `cmd/heater` → `ON` / `OFF`
- `cmd/update` → `install` or `check`

Home Assistant MQTT discovery publishes a **climate** entity plus two **select** entities (Control and Preset) and an **Update** entity that appears under Settings → Updates.

---

## Web security

- Cookie-based sessions (`HttpOnly; SameSite=Strict`), salted + iterated SHA-256 password hash, lock-out after 5 failed attempts.
- CSRF protection on every non-GET request (required even when no password is set).
- First-time setup wizard asks for a password by default; the setup portal is locked once the wizard finishes.
- **Open mode** (no password) is possible: firmware upload, factory reset and Wi-Fi changes then require holding the BOOT button for 3 s.
- Traffic is plain HTTP on port 80 – intended for a trusted LAN only.

---

## OTA updates

- **Web upload** – drop a `firmware.bin` on the Update page. The image must be built for the ESP32-C3 and contain the `ElementIQ-FW` product marker (searched across the whole image and across network chunks).
- **GitHub releases** – tag must be newer than the installed version and contain a `firmware.bin` asset. Certificate verification is currently disabled (work-in-progress TLS pinning lives in `extras/tls_pinning/`).
- **Rollback** – after an update the new image has 60 s to prove itself. Two crash / watchdog resets before confirmation cause an automatic roll-back to the previous slot.
- Automatic GitHub check (when enabled) runs every 10 days and never installs by itself.

---

## Hardware (ESP32-C3-WROOM-02-N4)

| Part | Pin |
|------|-----|
| Relay | GPIO7 |
| ADS1115 SDA / SCL | GPIO4 / GPIO6 |
| ADS1115 ADDR | GND → I2C address 0x48 |
| NTC | ADS1115 A0 (3.3 V — NTC — A0 — ≈10.98 kΩ — GND) |
| Status LED (analog RGB) | R = GPIO10, G = GPIO5, B = GPIO3 (series resistors) |
| Factory reset / confirm button | GPIO9 (BOOT) |
| Reserved | GPIO0, GPIO1 (future accessories); pressure sensor on ADS1115 A1 |

Set `STATUS_LED_COMMON_ANODE` in `include/config.h` to 1 for a common-anode LED.

---

## Changelog

### v1.0.1.19

- **Safety (element fault vs cold-water draw):** while the heater is on, a temperature drop of ≥ 1 °C (typical of a shower or other hot-water use refilling the tank with cold water) now resets the 15-minute rise window. This prevents false “element fault” trips caused by inlet water rather than a failed element.
- **UI (history graph):** temperature (°C) labels on the Y-axis, time labels on the X-axis, and a hover tooltip showing the exact temperature and timestamp of the nearest sample.

### v1.0.1.18

- **Fix (compile):** `queueOtaOperation` was not declared in scope inside `mqttCallback` / `processOtaAutoCheck`. The OTA worker queue, `initializeOtaWorker()` and `queueOtaOperation()` are now defined early in `mqtt_ota.inc` (worker body remains later). Added explicit FreeRTOS `queue.h` / `task.h` includes.
- **Safety (element fault less sensitive):** window increased from 10 min to 15 min; expected rise reduced from 3.0 °C → 0.8 °C to **1.5 °C → 0.4 °C** (temperature-dependent). Reduces false “element fault” trips on tanks that heat more slowly than expected.

### v1.0.1.17 (since v1.0.1.14)

- **Fix:** device time showed UTC. `configTime()` resets the timezone, so `TZ` is now set after it.
- **Fix:** overnight schedules (e.g. 22:00–02:00) no longer also heat in the early hours of the same day.
- **New:** Reboot button on the About page.
- **Safety:** 80 °C max setpoint, 85 °C software trip, watchdog, relay-off interlock around boot/OTA, OTA rollback after crashes.
- **Security:** session login + CSRF protection (replaces HTTP Basic), hashed password, setup portal closed after setup, LAN ArduinoOTA off by default.
- **New:** first-time setup wizard (Wi-Fi, optional password, hotspot password, auto-detected time zone).
- **New:** analog RGB status LED (GPIO10/5/3).
- **Fix:** web firmware upload could reject a valid image (marker only searched in the first 256 KB and not across chunk boundaries) and could show “Network error” after a successful update. Same marker fix applied to GitHub installs.
- **Fix (OTA keep):** new firmware was rolled back because confirmation required a valid NTC reading. Confirmation now only needs a clean 60 s run.
- **Fix (factory reset):** holding BOOT across the restart could loop forever with no AP. Reset now waits for button release; Wi-Fi credentials are fully erased.
- **Safety:** relay forced off while first-time setup is incomplete. Default mode is Off.
- **MQTT / Home Assistant:** firmware version sensor; MQTT Update entity; install via MQTT or the HA update card.
- **OTA auto-check:** when enabled, GitHub is polled every 10 days (check-only).

### Earlier

**v1.0.1.7**
- Custom web dialogs (no more browser “elementiq.local says”).
- HA MQTT operation state + select discovery.
- Removed unused `logo64.png`.

**2026-11-16**
- Serial on ESP32-C3 uses native USB CDC (no `Serial0` redefine).
- Improved captive-portal Wi-Fi scan reliability.
- LittleFS mounted with format-on-fail.

---

## Build (PlatformIO)

```bash
pio run -t upload
pio run -t uploadfs
```

Board: `esp32-c3-devkitm-1` (or compatible ESP32-C3 with 4 MB flash). Watch the flash percentage in the build output — the app slot is tight (≈95 %+).

Focused thermostat / sensor unit tests live in `test/test_control/`. Build them without uploading:

```bash
pio test -e esp32-c3 --without-uploading --without-testing
```

---

## Source layout

`src/main.cpp` contains the Arduino entry points and includes the ordered subsystem fragments in `src/modules/`:

| File | Responsibility |
|------|----------------|
| `controller.inc` | Settings, sensor, thermostat, heater interlock, password hashing, runtime accounting |
| `mqtt_ota.inc` | MQTT client and GitHub / local OTA |
| `api.inc` | Sessions, login, CSRF, HTTP API, web firmware upload |
| `web_ui.inc` | Page routes for the embedded web UI |
| `portal.inc` | Wi-Fi setup portal and wizard backend |
| `system.inc` | Watchdog, safe restart, OTA boot guard / rollback, BOOT-button confirmation, status LED |

Other important paths:

- `include/config.h` – all tunables (temperatures, timeouts, pins, version string)
- `include/control_logic.h` – pure thermostat / fault decision helpers
- `include/ota_marker.h` – product-marker scanner used during OTA
- `src/generated/web_gz.inc` – gzip-compressed web pages (keep them compressed)
- `web_src/` – readable HTML sources for login and portal
- `tools/web_gz_tool.py` – extract / pack the compressed pages
- `tools/test_marker.cpp` – host-side test for the OTA marker scanner
- `extras/tls_pinning/` – parked work for GitHub certificate verification (not built)

The modules remain in one translation unit for the moment so shared state and callbacks stay simple. Splitting into independent `.cpp` files is planned once explicit interfaces replace the current shared globals.

---

## Storage map

| Location | Contents |
|----------|----------|
| **EEPROM** | Wi-Fi, MQTT, setpoints, schedule, theme, AP settings, mode / preset |
| **NVS** `eiq` | First-time-setup-complete flag |
| **NVS** `eiq-boot` | OTA probation / crash counter (rollback) |
| **LittleFS** `/logo.png` | Brand logo |
| **LittleFS** `/serial.txt` | Device serial (survives OTA) |
| **LittleFS** `/webauth.txt` | Admin username + salted password hash |
| **LittleFS** `/hist.bin` | Temperature history ring (7 days @ 5 min) |
| **LittleFS** `/runtime.bin` | Heater runtime counters |
| **LittleFS** `/ota_ok.txt` | One-shot OTA success marker |

Partition table (`partitions_elementiq.csv`, 4 MB flash): **≈1.69 MB** per OTA app slot, **≈576 KB** LittleFS.
