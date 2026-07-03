// src/LightController.h
// ─────────────────────────────────────────────────────────────────────────────
// Light actuator and PIR/state-driven controller. In this first iteration
// only the `light` MQTT command is wired up — equivalent to the slice of
// Lua `main.lua/main.lua` that handles ON/OFF plus auto-off timer.
//
// Mirrors portions of `main/main.lua`'s `light()` and `moveDetected()`.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <Arduino.h>

class MqttPublisher;   // forward

class LightController {
public:
    void begin(MqttPublisher& publisher);
    void tick();

    // Wired into MqttDispatch for leaf="light".
    static void onLightCommandStatic(const String& payload);

private:
    MqttPublisher*   _publisher = nullptr;

    bool             _isOn = false;
    bool             _onManual = false;   // true if turned on via MQTT
    unsigned long    _onSinceMs = 0;
    unsigned long    _timeoutMs = 0;      // current timeout window

    void setOn(bool manual);
    void setOff();

    void applyLevel();
};
