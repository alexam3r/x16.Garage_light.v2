// src/State.h
// ─────────────────────────────────────────────────────────────────────────────
// Global state singleton (≈ Lua `dat`).
// Single object `g_state` of type `State` is the single source of truth for
// runtime values across all modules.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <Arduino.h>

struct State {
    String clntid;       // chip-id in hex → MQTT client ID
    String nodetopic;    // "garage/light/<clntid>"
    String topic;        // "garage/light"

    // Light control — mirrors `dat.light*`
    String light;        // "ON"|"OFF" — last input value via MQTT
    String lightNow;     // "ON"|"OFF" — effective state after tmr callbacks
    String lightSelected;// "MAIN"|"EDISON" — mode of active source

    // Heartbeat / diagnostics
    uint32_t count;
    uint32_t error_no;

    // MQTT reachability
    bool broker;

    // Last error message → "garage/light/message"
    String message;

    void begin();
};

extern State g_state;
