# AeroGarden firmware and controls

Editable web assets now live in `web/` inside this firmware project. PlatformIO automatically compresses them into `data/` before building. The separate sibling AeroGarden-Webpage folder is an older copy and is not used by this build.

## Using the new controls

- **Pump:** switching it is immediate and cancels any schedule. After switching off, the hour/minute card opens as in the supplied design. “Schedule next switch” also lets you schedule turning an already-running pump off. A timer switches the current state once after 1 minute to 7 days. “Cancel” dismisses the editor; “Stop schedule” cancels the active timer/cycle on the device while retaining the current pump state.
- **Automatic pump cycle:** enter independent ON/OFF durations in minutes. It starts a full interval from the current state and repeats until stopped or manually overridden. Durations must be whole minutes from 1 to 10080. This is elapsed-time scheduling, not a calendar/time-of-day schedule.
- **Automatic light:** set a nonzero red/blue mix, enable Automatic light, choose a target and save. Equal slider values produce equal red/blue output; a 1:2 mix remains approximately 1:2 at every brightness, allowing for 8-bit rounding. Manual mode uses the exact 0–255 slider values. Automatic mode normalizes the mix and varies brightness across the full range in 5% steps every two seconds, with a 3% deadband.
- **Light target:** a percentage of the raw 12-bit ADC range, not lux or a plant-specific illumination recommendation. Tune it using the actual sensor and lamp placement. The sensor can see ambient and grow light; the controller increases/decreases output towards the target.
- **Wi-Fi:** Settings accepts the network name and a new password (empty only for an open network). It tries the connection after returning the HTTP response, with a 20-second timeout. Credentials are saved to ESP32 Preferences before connecting. A failed attempt clears the saved network name and password and returns to the credentials-only setup screen. This also applies if saved credentials fail at boot or reconnection. Passwords are never returned by the API.

Light settings, floor count and repeating pump settings survive reboots. The pump always boots OFF; a saved cycle starts with a complete OFF interval. Manual ON state and one-shot timers do not survive a restart. Refresh rate is stored in the browser.

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

The fifteen web tests cover confirmed state updates, server/network failures, input validation, cancellation, overlapping commands, stale poll responses, unsaved edits, onboarding visibility/save/success/failure transitions, missing Wi-Fi state, and recovery after a failed initial status request. Fourteen C++ assertions cover one-shot switching in both directions, repeating cycles, manual override, replacement timers, millis rollover, light response/deadband/bounds and color ratios.

Hardware verification reported by the project owner on 2026-10-12: the setup form, pump schedule, and LED controls work on the prototype. Automatic light adjustment, incorrect-credential recovery, and persistence after a power cycle still need hardware verification.

Detailed hardware checks:

- Pump ON/OFF and LED saves return HTTP 200 without the old error toast; measure physical pump response separately from UI response.
- Use one-minute ON/OFF intervals and a one-minute timer in both directions. Cancel and manually override each; reload the browser to confirm the device state.
- Shade and illuminate the sensor; brightness should move gradually while the red/blue ratio remains stable. Verify fully lit and fully dark limits.
- Try incorrect Wi-Fi credentials, then correct credentials from the setup AP. Confirm home-network access via aerogarden.local, saved credentials after reboot and continued setup access when the router is absent.
- Power-cycle with a saved pump cycle and confirm the pump starts OFF.

Hardware details retained: ESP32-C3; pump GPIO 1; NeoPixels GPIO 0, 32 LEDs per floor, 3 floors; DHT11 GPIO 7; DS18B20 GPIO 8. All four light readings currently map to **GPIO 3**, so they represent one physical input. Change TEMT_PIN only when distinct ADC inputs are wired. GPIO 8 is a strapping pin on the C3; the existing board wiring needs hardware verification.

The old global request-body callback was replaced with registered JSON handlers that assemble fragmented bodies and issue a single response. Sensor conversions no longer block for the DS18B20 conversion time, HTTP state is mutex-protected, and polling cannot overwrite a newer command result. These fixes remove identified software causes; the reported 30-second physical delay cannot be confirmed eliminated without hardware testing.

References: [ESP32 Wi-Fi API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html), [Espressif mDNS example](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESPmDNS/examples/mDNS_Web_Server/mDNS_Web_Server.ino).
