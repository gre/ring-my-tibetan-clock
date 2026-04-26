# Tibetan Clock — Firmware

PlatformIO + Arduino firmware for an ESP32-C3 SuperMini that drives two MG90S servos to strike Tibetan singing bowls, controllable from Home Assistant over MQTT.

The hardware behavior (pinout, the strike + smooth-decay algorithm, the MQTT topic shapes) was ported verbatim from a working ESP-IDF native-C implementation kept locally outside this repo.

## Hardware

**MCU**: ESP32-C3 SuperMini (160 MHz single-core RISC-V, 4 MB embedded flash, USB-Serial/JTAG).

**Servos**: 2× MG90S, driven via 50 Hz PWM (14-bit duty resolution, 600 µs–2400 µs pulse, 90° = neutral).

**Servo power gate**: N-channel MOSFET (or similar) on the servo rail. Gate driven by GPIO 6. **Blue LED** on the board lights when the gate is conducting (hardware indicator, not firmware-controlled).

**Status LEDs**: 2× LEDs (green = success, red = error) directly driven by GPIOs 0 and 1.

### Pinout

| GPIO | Direction | Role | Notes |
|------|-----------|------|-------|
| 0 | OUT | Status LED — green | active-high |
| 1 | OUT | Status LED — red | active-high |
| 3 | OUT (PWM) | Bell A servo signal | LEDC, 50 Hz, 14-bit |
| 4 | OUT (PWM) | Bell B servo signal | LEDC, 50 Hz, 14-bit |
| 6 | OUT | Bell power MOSFET gate | active-high (`BELL_POWER_ACTIVE_HIGH=1`); blue LED comes on when high |
| USB | – | USB-Serial/JTAG (`Serial`) | flashing + console |

Polarity of GPIO 6 is configurable via `BELL_POWER_ACTIVE_HIGH` in `src/servo.h`.

### Schematic (logical)

```
          +5V (servo rail) ───┬──────────────┐
                              │              │
                         [MOSFET S]      [Blue LED]
                              │              │
GPIO6 ──[gate]── (rail) ──[MOSFET D] ────────┘  (LED indicates rail powered)
                                              │
                                              ├──────► Bell A servo VCC
                                              └──────► Bell B servo VCC

GPIO3 ──────────────────────────────────────────────► Bell A servo SIG
GPIO4 ──────────────────────────────────────────────► Bell B servo SIG
GND  ───────── common to ESP32, both servos, MOSFET source

GPIO0 ──[R]──[Green LED]── GND
GPIO1 ──[R]──[Red LED] ──── GND

USB-C ── ESP32-C3 SuperMini USB (debug + power for the MCU)
```

> The ESP32 is USB-powered; the servo rail is a separate +5 V supply gated by the MOSFET. **Don't share** the MCU's 3V3 pin with the servos — under stall they spike enough current to brown the MCU out (this is why the firmware sleeps `capacitor_stabilization_ms` after toggling the gate).

## Architecture

The firmware is split into small modules, each with a `Begin/Tick` (or `Init`) pair so `loop()` stays simple.

```
firmware/
├── platformio.ini             pioarduino fork, esp32-c3-devkitm-1, secrets via extra_configs
├── private_config.ini.template build_flag stanza for WiFi/MQTT secrets
├── private_config.ini          gitignored — local credentials
└── src/
    ├── main.cpp                setup() init order, loop() pumps each module's Tick
    ├── servo.{h,cpp}           bell ring algorithm + rail power manager
    ├── config.{h,cpp}          NVS-persisted per-bell intensity / count / center trim
    ├── leds.{h,cpp}            LED state machine (Booting / WifiConnecting / Connected / Error / Ringing)
    ├── wifi_conn.{h,cpp}       WiFi STA, auto-reconnect, exponential backoff
    └── mqtt.{h,cpp}            PubSubClient + ArduinoJson, HA discovery, command dispatch
```

### Servo / ring algorithm

For each `ringBell(bell, count, intensity)`:

1. **Rail-up** (skipped if rail is already powered from a recent ring). Staggered cold-start: pre-arm only the targeted servo's PWM line at center, energize the MOSFET, wait `RAIL_WARMUP_MS` (1 s) for the cap to charge with a single servo loaded, then pre-arm the other servo. Avoids brownouts caused by both MG90S seeking 90° simultaneously through a charging cap.
2. Re-issue the targeted servo at center (in case it drifted between rings), sleep `capacitor_stabilization_ms`.
3. Repeat `count` times:
   a. Snap servo to `center + intensity`.
   b. Hold `release_pause_ms` (the strike dwell — what gives the bowl a chance to sing).
   c. Step back to center **one degree at a time**, sleeping `step_return_delay_ms` per step (prevents secondary clang on snap-back).
   d. Pad remaining time to `total_ring_delay_ms`.
4. Re-center both servos to their per-bell centers (so the un-commanded bell can't drift after brownout pressure during the strike).
5. **Schedule** a MOSFET-off `RAIL_IDLE_HOLD_MS` (1.5 s) in the future, executed by `servoTick()` from the main loop. If another `ringBell` lands inside that window, the schedule is cancelled and the rail stays up — back-to-back rings share a single power cycle.

Defaults (from `src/servo.h`):
- `capacitor_stabilization_ms = 222`
- `release_pause_ms = 500`
- `step_return_delay_ms = 5`
- `total_ring_delay_ms = 4000`
- `intensity = 20` (clamped to 5–50, the angular amplitude in degrees)
- `center = 90°` per bell, configurable from HA (60–120) for mechanical mounting trim

### MQTT / Home Assistant integration

Auto-discovery via the `homeassistant/...` topic prefix. The device exposes **3 buttons + 6 number sliders** (all under one HA device entry "Tibetan Clock"), plus an availability binary sensor. The bell number for ring commands is taken **from the topic, not the payload**.

**Entities:**

| HA entity | Component | Range | Section |
|-----------|-----------|-------|---------|
| `Ring Bell A` | button | – | Controls |
| `Ring Bell B` | button | – | Controls |
| `Calibrate` | button | – | Controls |
| `Bell A Intensity` | number | 5–50° | Configuration |
| `Bell B Intensity` | number | 5–50° | Configuration |
| `Bell A Count` | number | 1–9 | Configuration |
| `Bell B Count` | number | 1–9 | Configuration |
| `Bell A Center` | number | 60–120° | Configuration |
| `Bell B Center` | number | 60–120° | Configuration |

The `Bell X Intensity` / `Count` / `Center` sliders are persisted in NVS and used as defaults when `Ring Bell X` is pressed without a payload (or with HA's default `"PRESS"` body).

Pressing `Ring Bell A` with an explicit JSON payload overrides per-press:

```json
{ "intensity": 30, "count": 3 }
```

**Calibration workflow:** Press `Calibrate` → the rail powers up and both servos hold at their per-bell centers for 10 s. While holding, dragging `Bell A/B Center` sliders moves the corresponding servo live (each slider change resets the 10 s timer). When alignment looks right, stop interacting; after 10 s of inactivity the rail powers off.

**Topics** (`homeassistant/...` prefix):

| Topic | Direction | Retain | Notes |
|-------|-----------|--------|-------|
| `binary_sensor/tibetan_clock/availability` | publish | yes | `online` / `offline`. LWT auto-publishes `offline` on unclean disconnect — HA depends on this. |
| `binary_sensor/tibetan_clock/state` | publish | no | `ringing` / `idle` (informational) |
| `button/tibetan_clock/ring_bell_a/config` + `_b/config` + `calibrate/config` | publish | yes | discovery payloads for the 3 buttons |
| `button/tibetan_clock/ring_bell_a/command` + `_b/command` + `calibrate/command` | subscribe | – | trigger ring or calibration |
| `number/tibetan_clock/bell_{a,b}_{intensity,count,center}/{config,state,set}` | bidirectional | state retained | the 6 config sliders |

#### PubSubClient gotchas baked into the code

- `setBufferSize(1024)` is called **before** `connect()` (calling after has no effect on outgoing publishes — [issue #764](https://github.com/knolleary/pubsubclient/issues/764)).
- `setKeepAlive(60)` so a 12 s ring doesn't trip the default 15 s keepalive.
- Commands are **deferred to the main loop**, not run inside the MQTT callback — ringing inside the callback would starve `client.loop()` for ~12 s.

## Build / flash / monitor

PlatformIO is at `/Users/gre/.platformio/penv/bin/pio`. Add it to your shell PATH:

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

Then:

```sh
cd firmware
pio run                                                # build
pio run -t upload --upload-port /dev/cu.usbmodem2101  # flash
pio device monitor --port /dev/cu.usbmodem2101 --echo  # serial console
```

The `--echo` flag enables local echo so you can see what you type. Exit with **Ctrl+C**.

> If `pio run -t upload` reports "could not open port", a serial monitor is holding the port — close it first. The C3's USB-Serial/JTAG also re-enumerates between flash stages; if a single upload fails mid-write, just rerun.

### Configuration

`platformio.ini` pulls secrets from `private_config.ini` via `extra_configs`. The latter is gitignored — copy from the template to set up a fresh checkout:

```sh
cp firmware/private_config.ini.template firmware/private_config.ini
$EDITOR firmware/private_config.ini
```

Defines (all required for networking):

- `WIFI_SSID`, `WIFI_PASSWORD`
- `MQTT_HOST`, `MQTT_PORT` (default 1883)
- `MQTT_USER`, `MQTT_PASSWORD`
- `MQTT_CLIENT_ID`

Backslash-escape internal quotes: `-DWIFI_SSID=\"YourSSID\"`.

Other compile-time toggles (in `platformio.ini`):

- `ENABLE_NETWORKING=0` — strips out WiFi/MQTT init for pure servo testing (M1 mode).

## Serial commands

The boot is silent (no self-test ring). The serial console accepts the same kind of commands as MQTT, plus a couple of diagnostics:

| Command | Action |
|---------|--------|
| `0` | Ring Bell A using its configured intensity / count from NVS |
| `1` | Ring Bell B using its configured intensity / count from NVS |
| `0 30 3` | Bell A, override to intensity 30, 3 strikes (NVS values not changed) |
| `1 50 1` | Bell B, override to intensity 50, 1 strike |
| `p` | Toggle MOSFET (blue LED on/off, no servo motion) — diagnostic |
| `s 0` / `s 1` | Sweep one servo 60→120→90 without striking — diagnostic |
| `?` | Help |

## Status LEDs

Centralized through `leds.{h,cpp}` so WiFi and MQTT layers don't fight over them.

| State | Green | Red |
|-------|-------|-----|
| Booting | fast blink | fast blink |
| Connecting WiFi | off | slow blink |
| WiFi connected | solid | off |
| MQTT connected | solid | off |
| Ringing | fast blink | off |
| Error | off | solid |

## NVS layout

Persistent values live under the `tibetan` Preferences namespace (`config.cpp`):

| Key | Type | Range | Default |
|-----|------|-------|---------|
| `ver` | uint16 | – | schema version (currently 3 — bumping resets all values to defaults) |
| `int_a` / `int_b` | uint8 | 5–50 | 20 |
| `cnt_a` / `cnt_b` | uint8 | 1–9 | 1 |
| `ctr_a` / `ctr_b` | uint8 | 60–120 | 90 |

Values are clamped on load; flash is rewritten only if clamping actually changed a value.

## Possible future work

- OTA (would need a partition-table swap to `min_spiffs.csv` first)
- Web UI / captive portal for first-boot WiFi provisioning (replaces hardcoded creds)
- mDNS for `tibetan-clock.local`
- Per-bell custom timings (release pause, step delay) exposed in HA

## License

GPL v3 — see [`LICENSE`](LICENSE).
