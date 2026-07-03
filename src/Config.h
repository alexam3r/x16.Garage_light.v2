// src/Config.h
// Compile-time project configuration. **Doesn't contain any secrets** —
// all credentials live in src/Secrets.h.
//
// Mirrors the constants used by the original Lua firmware:
//   main/setglobals.lua   (pins, defaults)
//   main/main.lua         (timing)
//   main/mqttset.lua      (topic strings, MQTT keepalive)

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Module-level compile-time switches.
//
// CFG_ENABLE_HA_DISCOVERY       — Home Assistant discovery payloads
//                                  (not yet implemented; 0 in this iteration).
// CFG_ENABLE_AM2320             — I²C AM2320 driver (not yet implemented).
// CFG_PUBLISH_LEGACY_TOPICS     — publish the original plain-string topic
//                                  tree under garage/light/* for non-HA
//                                  consumers. **Enabled by default in this
//                                  iteration** — that is the entire surface
//                                  area we cover before adding HA.
// CFG_PUBLISH_TELEMETRY_JSON    — single JSON state topic (not in this
//                                  iteration — only the legacy tree).
// CFG_ENABLE_HTTP_RECOVERY      — HTTP-recovery boot mode (not in this
//                                  iteration).
// ---------------------------------------------------------------------------
#define CFG_ENABLE_HA_DISCOVERY      0
#define CFG_ENABLE_AM2320            0
#define CFG_PUBLISH_LEGACY_TOPICS    1
#define CFG_PUBLISH_TELEMETRY_JSON   0
#define CFG_ENABLE_HTTP_RECOVERY     0

// ---------------------------------------------------------------------------
// GPIO pin assignments — match the original Lua firmware (setglobals.lua).
// ---------------------------------------------------------------------------
#define PIN_LIGHT_MAIN       5   // D5 — main lighting relay
#define PIN_LIGHT_EDISON     6   // D6 — decorative Edison lamps
#define PIN_PIR              7   // D7 — PIR motion detector (interrupt)

// ---------------------------------------------------------------------------
// Timing constants, in milliseconds.
//   dat.lightTimeout = 600 s         (motion-driven auto-off)
//   dat.lightTimeout = 3600 s        (MQTT override)
//   dat.moveDetectionTimout = 6870947 ms (~1 h 54 m 30 s)
// ---------------------------------------------------------------------------
#define LIGHT_TIMEOUT_DEFAULT_MS   600000UL
#define LIGHT_TIMEOUT_MQTT_MS      3600000UL
#define MOVE_DETECTION_TIMEOUT_MS  6870947UL

// 1 Hz heartbeat cadence. Period in ms.
#define HEARTBEAT_PERIOD_MS        1000UL
// Every Nth heartbeat, publish heap/uptime. Matches original Lua behaviour.
#define HEARTBEAT_DIAG_PERIOD      60UL
// Tick budget for `delay()` in loop() — lets the WiFi stack run.
#define LOOP_TICK_BUDGET_MS        2UL

// ---------------------------------------------------------------------------
// Topic tree. The original Lua firmware used a string `dat.topic =
// 'garage/light'` and `dat.nodetopic = dat.topic .. '/' .. dat.clntid`.
// We keep the same layout so old tooling and dashboards still see the
// same tree.
// ---------------------------------------------------------------------------
static const char TOPIC_BASE[]      PROGMEM = "garage/light";

// Shared leaf-name publisher (publishing under `garage/light/<leaf>`,
// build the topic at call site: `String(TOPIC_BASE) + "/" + leaf`).
//
// Per-device (`<clntid>/<leaf>`) topics are built in code via State.cpp.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MQTT connection parameters (non-secret).
// ---------------------------------------------------------------------------
#define MQTT_KEEPALIVE_S            60
#define MQTT_DEFAULT_PORT           1883

// ---------------------------------------------------------------------------
// LWT payloads.
//   Lua dat.brok-offline sets `…/<clntid>/state` with value "OFF".
//   Birth (online) = "ON" with retain=1.
//   See main/mqttset.lua and main/mqttget.lua.
// ---------------------------------------------------------------------------
static const char LWT_PAYLOAD_OFFLINE[] PROGMEM = "OFF";
static const char LWT_PAYLOAD_ONLINE[]  PROGMEM = "ON";
