# AeroGarden firmware and controls

Editable web assets now live in `web/` inside this firmware project. PlatformIO automatically compresses them into `data/` before building. The separate sibling AeroGarden-Webpage folder is an older copy and is not used by this build.

## Using the new controls

- **Pump:** switching it is immediate and cancels any schedule. After switching off, the hour/minute card opens as in the supplied design. “Schedule next switch” also lets you schedule turning an already-running pump off. A timer switches the current state once after 1 minute to 7 days. “Cancel” dismisses the editor; “Stop schedule” cancels the active timer/cycle on the device while retaining the current pump state.
- **Automatic pump cycle:** enter independent ON/OFF durations in minutes. It starts a full interval from the current state and repeats until stopped or manually overridden. Durations must be whole minutes from 1 to 10080. This is elapsed-time scheduling, not a calendar/time-of-day schedule.
- **Automatic light:** set a nonzero red/blue mix, enable Automatic light, choose a target and save. Equal slider values produce equal red/blue output; a 1:2 mix remains approximately 1:2 at every brightness, allowing for 8-bit rounding. Manual mode uses the exact 0–255 slider values. Automatic mode normalizes the mix and calculates brightness separately for each of the four ambient-only sensors every two seconds. Brightness is clamped to 0–100% using `1 - ambient / target`: darkness gives full output, half the target gives half output, and reaching or exceeding the target turns the corresponding section off. There is no accumulating feedback loop and no averaging between sensors.
- **Light target:** a percentage of the raw 12-bit ADC range, not lux or a plant-specific illumination recommendation. Tune it using the actual sensor and lamp placement. Sensors measure natural ambient light and are not affected by the LEDs. The target is the ambient level at which supplemental light becomes unnecessary. Each sensor controls the same 32-LED section on every active floor, preserving the selected red/blue ratio.
- **Wi-Fi:** Settings accepts the network name and a new password (empty only for an open network). It tries the connection after returning the HTTP response, with a 20-second timeout. Credentials are saved to ESP32 Preferences before connecting. A failed attempt clears the saved network name and password and returns to the credentials-only setup screen. This also applies if saved credentials fail at boot or reconnection. Passwords are never returned by the API.

Light settings, floor count and repeating pump settings survive reboots. The pump always boots OFF; a saved cycle starts with a complete OFF interval. Manual ON state and one-shot timers do not survive a restart. Refresh rate is stored in the browser.

## Daily LED schedule

In LED Control, save your desired color and Auto Light settings, then enable Daily LED schedule. Set Turn on at to 06:00 and Turn off at to 18:00 for a daily 6 a.m.–6 p.m. window. Save LED schedule separately from Save lights. The start is inclusive and the end exclusive. Overnight windows such as 18:00–06:00 work too; equal start/end times are rejected. Disable and save the schedule for unrestricted lighting.

Scheduling gates all LED output: outside the window the LEDs are off, even if sliders are saved or Auto Light is enabled. During the ON period, the saved manual channels or automatic brightness control apply. Choose nonzero channels if you want visible light; Auto Light may reduce output to zero if ambient light is sufficient. The daily schedule overrides all four sections, forcing them off outside the ON window. Sensor readings continue while off, so the next ON period uses current ambient light.

The UTC offset defaults to −180 minutes (UTC−03:00, Argentina). It is editable and saved with the schedule. This is a fixed offset; adjust it yourself for daylight-saving changes in other regions. Garden time and the current ON/OFF period are displayed in the controls.

After Wi-Fi connects, the ESP32 requests UTC time asynchronously from pool.ntp.org and time.google.com. If internet time is unavailable, press Use this device’s time to set the clock from your phone/computer. Its clock must be correct. The schedule then runs on the ESP32 without an open browser and continues through network outages while powered. A power loss preserves the schedule but not a reliable wall clock: scheduled LEDs remain off until network time or browser time is available. With scheduling disabled, existing lighting works without a clock.

Firmware checks schedule boundaries continuously and applies schedule/time changes immediately. Configuration is stored as a single Preferences record. New API routes: POST /api/leds/schedule accepts enabled, startMinute, endMinute and utcOffsetMinutes; POST /api/time accepts UTC epoch seconds. Both require completed Wi-Fi setup, like the other controls.

Validation for this addition covers daily/overnight boundaries, UTC date rollover, an unset clock, disabling scheduling, restoring form values, invalid times, and browser clock synchronization. Hardware checks still needed: set a window a few minutes ahead, verify ON/OFF with manual and automatic light, reboot with internet unavailable (lights should stay off), then set browser time and verify the current window is applied.

## First-start setup

At startup the web page checks the device state before revealing any garden controls. Without saved Wi-Fi credentials, it shows only Configuration / Your Garden and the SSID/password form, following the supplied design. Save persists the credentials, hides the form, and shows a connection-progress screen. On success, the garden dashboard appears automatically. On failure after 20 seconds, credentials are erased and the empty form reappears with an error. Polling preserves unfinished entries while the form is open.

The form is visible by default. Missing Wi-Fi status or a failed status request shows an explanatory message rather than leaving the form permanently hidden. Upload firmware and filesystem together to keep the API and web interface compatible.

With saved credentials, startup goes directly to the connection-progress screen. During setup or connection, firmware rejects garden-control POST requests with HTTP 409, even from an already-open browser. The setup AP remains reachable throughout. Returning to the beginning means restarting the setup flow, not rebooting the MCU; existing automatic pump/light operation continues.

## Addresses for the manual

1. On the same home network, open **http://aerogarden.local** (requires mDNS support on the client/network).
2. For a fixed numeric address, join **AeroGarden-Setup**, password **aerogarden**, and open **http://192.168.4.1**. This access point stays available, including while testing replacement Wi-Fi credentials. It does not provide Internet access.
3. On first use after this update, use that access point to configure Wi-Fi. Credentials formerly compiled into the source have been removed.

The station IP still comes from the router. A universal fixed station IP cannot work across arbitrary home subnets; reserve an address for this ESP32 in the router if a numeric address on the home LAN is required. The setup AP uses the 192.168.4.0/24 subnet; a home router using that same subnet can cause routing conflicts and should use a different subnet.

API access follows the existing local-network trust model: anyone on the home network or setup AP can control the device. Do not expose port 80 to the Internet.

## Responsive interface

Phones keep the existing single-column interface and fixed bottom navigation. From 768px, the hub expands to the available width, monitoring/settings cards use two columns, and each LED/pump control card sits beside its schedule. Navigation moves above the content. From 1100px, monitoring/settings use three columns and navigation sits beside the header. Content is capped at 1280px for readability. Wi-Fi onboarding stays centered with a compact form. Browser zoom is enabled; layouts respond to the resulting viewport size. Sensor cards remain expandable and the existing LED/Pump tabs still select their respective controls.

## Build and upload

From this folder with PlatformIO installed:

```powershell
pio run
pio run -t buildfs
```

Build outputs use the normal `.pio/build` directory. On Windows, keep the project in a path without accented characters (the current folder is `Practicas`). The ESP32 linker failed when this folder was named `Prácticas`. If moving the project is not possible, set `$env:PLATFORMIO_BUILD_DIR = Join-Path $env:TEMP "aerogarden-build"` before running PlatformIO.

After connecting the ESP32 by USB, install **both** firmware and web filesystem:

```powershell
pio run -t upload
pio run -t uploadfs
```

If `pio` is not on PATH, use `& "$env:USERPROFILE/.platformio/penv/Scripts/platformio.exe"` in its place.

Optional local preview (simulated device; does not contact ESP32):

```powershell
node scripts/preview.cjs
# Open http://127.0.0.1:8765; use SSID "fail" to simulate rejection.
# Any other SSID simulates success after three seconds.
```

## Verification

```powershell
node --test test/web.test.cjs
# Compile-time tests execute the actual control logic through C++14 constexpr:
& "$env:USERPROFILE/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-g++.exe" -std=c++14 -I include -fsyntax-only test/control_static.cpp
```

The twenty web tests cover confirmed state updates, server/network failures, input validation, cancellation, overlapping commands, stale poll responses, unsaved edits, onboarding visibility/save/success/failure transitions, missing Wi-Fi state, and recovery after a failed initial status request. Thirty-three C++ assertions cover one-shot switching in both directions, repeating cycles, manual override, replacement timers, millis rollover, light response/deadband/bounds and color ratios.

Hardware verification reported by the project owner on 2026-10-12: the setup form, pump schedule, and LED controls work on the prototype. Automatic light adjustment, incorrect-credential recovery, and persistence after a power cycle still need hardware verification.

Detailed hardware checks:

- Pump ON/OFF and LED saves return HTTP 200 without the old error toast; measure physical pump response separately from UI response.
- Use one-minute ON/OFF intervals and a one-minute timer in both directions. Cancel and manually override each; reload the browser to confirm the device state.
- Shade and illuminate the sensor; brightness should move gradually while the red/blue ratio remains stable. Verify fully lit and fully dark limits.
- Try incorrect Wi-Fi credentials, then correct credentials from the setup AP. Confirm home-network access via aerogarden.local, saved credentials after reboot and continued setup access when the router is absent.
- Power-cycle with a saved pump cycle and confirm the pump starts OFF.

## Confirmed wiring and section mapping

ESP32-C3: LED data GPIO 21; pump GPIO 20; DHT22 GPIO 7; DS18B20 GPIO 8. Ambient sensors 1–4 use GPIOs 0, 1, 2, 3 respectively. The separate LED on GPIO 5 is left unchanged. Each floor has 128 serial LEDs (four sections of 32), with up to three floors / 384 LEDs. Number of Floors in Settings determines how many complete 128-LED floors are active; remaining pixels are off.

Zero-based, inclusive LED ranges:

| Sensor / GPIO | Floor 1 | Floor 2 | Floor 3 |
| --- | --- | --- | --- |
| 1 / 0 | 0–31 | 128–159 | 256–287 |
| 2 / 1 | 32–63 | 160–191 | 288–319 |
| 3 / 2 | 64–95 | 192–223 | 320–351 |
| 4 / 3 | 96–127 | 224–255 | 352–383 |

The UI reports output for each sensor's sections. API sectionOutput contains four red/blue pairs in sensor order; legacy actualRed/actualBlue fields are their averages. Manual mode applies the same chosen channels to every active section, independent of sensor readings.

Hardware verification for the new mapping: illuminate one sensor at a time and confirm only its sections dim on all active floors. Shade that sensor and confirm those sections brighten. Check the 31/32, 127/128 and 255/256 boundaries, inactive floors, manual mode, color ratio, and schedule OFF behavior. Existing hardware confirmations predate this rewiring and per-section change.

The old global request-body callback was replaced with registered JSON handlers that assemble fragmented bodies and issue a single response. Sensor conversions no longer block for the DS18B20 conversion time, HTTP state is mutex-protected, and polling cannot overwrite a newer command result. These fixes remove identified software causes; the reported 30-second physical delay cannot be confirmed eliminated without hardware testing.

References: [ESP32 Wi-Fi API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html), [Espressif mDNS example](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESPmDNS/examples/mDNS_Web_Server/mDNS_Web_Server.ino).
