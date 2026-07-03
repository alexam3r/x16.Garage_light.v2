# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP8266 (NodeMCU) garage-light firmware. There are two parallel branches of the project:

- `docs/lua-original/` — historical reference: the same project written in Lua, intended to be loaded as a single Lua File System image (`LFS.img`) into the original NodeMCU firmware.
- `src/` — the current C++/PlatformIO port. **`src/` is now where development happens.**

When reading the legacy Lua sources, treat them as the spec being ported — verify a behaviour in `docs/lua-original/main.lua` (or mqttset.lua etc.) before changing the equivalent module in `src/`. The back-story and Lua quirks (LFS bundling, `rtcmem`-driven HTTP-recovery, `node.LFS.reload` at boot) do NOT apply to the C++ port — they exist for context only.

## Build and tooling (C++ port)

- **Build system:** PlatformIO. Run `pio run` to compile the `env:nodemcu` environment; the target config is `platformio.ini`.
- **Compile defines** (see `platformio.ini`, `src/Config.h`):
  - `MQTT_MAX_PACKET_SIZE=512` (set both in `build_flags` and via `PubSubClient::setBufferSize(512)`).
- **Secrets** live in `src/Secrets.h`. That file is in `.gitignore` and is initially populated with empty placeholder values; copy from `src/Secrets.h.sample` and edit before flashing.
- **No tests, no linter, no CI.** Each change is verified by `pio run -t upload` plus an MQTT client (e.g. `mosquitto_sub`) and an early-pass serial log.

## Architecture and file layout (C++ port)

All `.cpp` modules compose greedily into static singletons declared in `src/main.cpp`:

| File | Role |
|---|---|
| `src/main.cpp` | `setup()` + `loop()` + 1-Hz `Ticker`. Composes all modules. Owns the dispatch table mapping topic-leaf → handler. |
| `src/Config.h` | Pin assignments, timing constants, MQTT defaults, compile switches (`CFG_ENABLE_*`). **No secrets here.** |
| `src/Secrets.h(.sample)` | Wi-Fi SSID/pass, MQTT user/pass, broker address. Single editable file. |
| `src/State.{h,cpp}` | Global `g_state` struct (replaces Lua's `dat`). Holds clntid, light state, broker connected flag, error counter, message string. |
| `src/Logger.{h,cpp}` | Serial wrapper. One unified prefix: `[L][<uptime>ms][<module>] <msg>`. |
| `src/WifiManager.{h,cpp}` | Thin wrapper over `WiFi`. Sets autod-reconnect, registers got-IP / disconnected events. |
| `src/MqttClient.{h,cpp}` | Owns a single `PubSubClient`. Holds the LWT. Reconnect loop with exponential backoff. Forwards incoming messages to `MqttDispatch`. **Buffers incoming payload into a String** before handing off — required because PubSubClient reuses the buffer. |
| `src/MqttDispatch.{h,cpp}` | Topic → handler routing by last-segment match (Lua-equivalent of `string.match(topic, "./(%w+)$")`). |
| `src/MqttPublisher.{h,cpp}` | FIFO outgoing queue (replaces Lua's `topub`). Single in-flight publication to avoid stacking packets on the TCP socket. |
| `src/LightController.{h,cpp}` | Light actuator: ON/OFF via MQTT, GPIO5/GPIO6, auto-off timer. Provides `onLightCommandStatic` registered in `main.cpp`'s dispatch table. |

## Conventions for the C++ code

- One singleton per module, declared in its `.cpp` file as `extern`-exported. `main.cpp` composes them. Don't create a second instance.
- All allocations happen in module-internal `namespace { … }` blocks (e.g. `MqttPublisher`'s queue is a translation-unit-private static array, not a class field) so a future static-analysis pass can audit `class` RAM usage.
- Functions that need to be visible across translation units are exposed through `extern` in headers; internals stay in `anonymous namespace`.
- `Logger` is the only place that uses `Serial.println` — no `print()` calls in module code.
- Topic layout mirrors the Lua legacy tree: per-device under `garage/light/<clntid>/...`, shared under `garage/light/...`. Don't introduce new prefixes without updating home-assistant / monitoring tools that already index the tree.

## Open items / known TODOs not in code yet

These were deferred per the current iteration plan (kept as a parking lot so a future session can resume without re-reading the README):

- Home Assistant MQTT discovery (`homeassistant/.../config` retain payloads) — not yet published; `CFG_ENABLE_HA_DISCOVERY` stays at 0.
- AM2320 / I²C sensor — not yet implemented; `CFG_ENABLE_AM2320` stays at 0.
- Telemetry as a single JSON state topic — not yet published; `CFG_PUBLISH_TELEMETRY_JSON` stays at 0.
- HTTP-recovery / OTA — not yet implemented; `CFG_ENABLE_HTTP_RECOVERY` stays at 0.
- `select` MAIN/EDISON switching via `…/lightSelected` — only `light` ON/OFF is wired in dispatch.
- Hardware watchdog — disabled by default.
- PIR GPIO handling — `LightController` doesn't yet listen to `pinPIR` interrupts.

## How code is developed and deployed

There is **no local build / lint / test step**. The workflow is:

1. Edit `.lua` files under `main/`.
2. Use the NodeMCU **Lua File System** tool (`node.LFS`) to pack `main/*.lua` into `LFS.img` (this happens embedded — see `init.lua`); an out-of-band `luac.cross` → `node-flash-tool` flow is used to push the resulting firmware image.
3. Flash the resulting firmware (containing `LFS.img`) to the ESP8266 over serial.

To verify changes there is no unit-test harness — bugs surface by watching the device's serial console (REPL-mode Lua prints) and the MQTT traffic on the broker (`dat.brok`, default `10.0.2.1`). Diagnostic topics:
- `garage/light/<clntid>/state` — LWT (`ON`/`OFF`)
- `garage/light/<clntid>/heap`, `garage/light/<clntid>/uptime` — published every 60 s from `main.lua`
- Topics like `garage/light/airTemp`, `garage/light/hum`, `garage/light/lightNow`, `garage/light/light`, `garage/light/message`, `garage/light/lightSelected`, `garage/light/lightMoveDetection` for state

`init.lua` includes an **HTTP-recovery mode**: writing `501` to `rtcmem` word `0` before reboot makes the device boot into an HTTP server (`node.LFS.get('http')()`) instead of running the app — useful when the LFS image is corrupted. The check for the `to_lfs` marker file at boot is what triggers `node.LFS.reload('LFS.img')` after a reflash.

## Architecture and file layout

All `.lua` files share a **single global namespace** (`_G`). State lives in two globals:
- `dat` — application state table, populated by `setglobals.lua` (loaded first, by convention via `dofile`), used/referenced across every file.
- `topub` — outgoing MQTT message queue (`{[topic_path]=value, [qos], [retain]}`-ish nested tables). Producers in `main.lua`, `check_air_temp.lua`, `mqttanalise.lua`, `light()` push into it; the consumer is `mqttpub.lua`, drained by the 1-Hz alarm registered at the bottom of `main.lua`.

`btbl` (note: declared by `mqttset.lua` but mutated by the `on("message")` handler) is the **inbound** MQTT queue, drained synchronously by `mqttanalise.lua` each time a message arrives.

### Module roles (read top-down)

| File | Role |
|---|---|
| `init.lua` | Boot router. Either reloads a freshly-pushed `LFS.img` (when `to_lfs` marker exists), or falls through to `setglobals.lua`. Also routes the HTTP-recovery branch via `rtcmem.read32(0) == 501`. |
| `setglobals.lua` | Single source of truth for boot-time config: `dat` defaults, MQTT broker creds, GPIO pin assignments (`pinPIR`, `pinLightMain`, `pinLightEdison`), GPIO mode setup. Then chains `mqttset.lua` → `main.lua`. **Edit this when adding new pins, MQTT topics, or boot-time config.** |
| `mqttset.lua` | Builds the `m` (mqtt.Client), configures LWT, registers the `offline` reconnect and `message` handlers. Triggers initial connect via `mqttget.lua`. |
| `mqttget.lua` | Reconnect loop. Re-runs itself (`dofile('mqttget.lua')`) on failure. Resubscribes after each (re)connect — so subscriptions live here, not in `mqttset.lua`. Add new topic subscriptions here. |
| `mqttanalise.lua` | Inbound dispatch. Parses the last segment of the topic (`string.match(topic, "./(%w+)$")`) and routes to `light()`, the motion-detection toggle, or the special `ide` (force HTTP-recovery reboot) / `restart` commands. |
| `mqttpub.lua` | Outbound publisher. Pops from `topub` and chains publishes via `punow` callback so messages keep order on a single MQTT socket. |
| `check_air_temp.lua` | One-shot AM2320 read over I2C. Uses `i2c.setup(0, 2, 1, i2c.SLOW)` and the `am2320` module; failures land in `dat.message` so they surface as the `message` topic. Called from `main.lua` both at boot and during the 60-tick reset. |
| `main.lua` | Application core. Holds the `createLightTimer` / `destroyLightTimer` and `createMoveDetectionTimer` / `destroyMoveDetectionTimer` helpers, the `light(state)` actuator (also re-applies current state when `lightSelected` changes), and the `moveDetected` PIR callback. The trailing `tmr.create():alarm(1000, ALARM_AUTO, …)` is the 1-Hz heartbeat that drives the whole system. |

### Globals worth knowing about

- `lightTimer`, `moveDetectionTimer` — owned by `main.lua` helpers.
- `light()`, `moveDetected()`, `createLightTimer()`, `destroyLightTimer()`, `createMoveDetectionTimer()`, `destroyMoveDetectionTimer()` — declared in `main.lua`, **called back** from `mqttanalise.lua` (e.g. `light(dat.light)`, `createMoveDetectionTimer()`). Do not move or rename these without updating the cross-file call sites.
- `pinLightMain` / `pinLightEdison` are *both* a `dat` field (string `"ON"`/`"OFF"` published to MQTT, set in `setglobals.lua`) **and** a global integer pin number used by `gpio.write`. Don't conflate them — the strings are status, the numbers are GPIO.
- `dat.error_no` increments on WiFi/MQTT failures; `main.lua` calls `node.restart()` once it exceeds 100 — useful when debugging hangs.
- `dat.message` is the catch-all error string for the most recent recoverable failure published as the `message` topic.

### Timing constants worth knowing about

- `dat.lightTimeout = 600` seconds — how long the light stays on after motion (overridden to `3600` in `mqttanalise.lua` when the light is turned on remotely via MQTT).
- `dat.moveDetectionTimout = 6870947` ms (~1h 54m) — grace window after motion is disabled before re-arming PIR via the timer instead of gpio.trig.
- The 60-tick heartbeat in `main.lua` republishes heap/uptime and resets `dat.count` — keep the `if dat.count == 30` guard in sync with the `dat.count >= 60` block if you change the cadence.

### Style / conventions specific to this codebase

- `do … end` blocks (not functions) are used pervasively so each `dofile` returns to a clean local scope — `topub`, `btbl`, `tp`, `punow` etc. are intentionally `local` to the block where they're consumed.
- Functions intended for cross-file callbacks (`light`, `moveDetected`, the timer helpers) live at file scope in `main.lua` so other files can reference them after `dofile`.
- Topics are built as `dat.topic..'/'..subkey`, where `dat.topic = 'garage/light'` and `dat.nodetopic = dat.topic..'/'..dat.clntid`. Anything per-device goes under `nodetopic`; anything shared (light state) goes under `topic`.
- Tail-recurse pattern: `mqttpub.lua` uses `punow` as the publish callback so messages are serialized, and `mqttget.lua` uses `dofile` self-call to reconnect — preserve this when modifying either.
</content>
</invoke>