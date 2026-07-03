// src/LightController.cpp

#include "LightController.h"
#include "MqttPublisher.h"
#include "Config.h"
#include "State.h"
#include "Logger.h"

namespace {
// One global instance — wired into the dispatch table at setup().
LightController* g_lightInstance = nullptr;
}

// Trampoline because the dispatch table needs a static / free function.
void LightController::onLightCommandStatic(const String& payload) {
    if (!g_lightInstance) return;

    // Mirror Lua `if itm[2] ~= dat.light then dat.light = itm[2]; …`.
    if (payload == g_state.light) return;

    g_state.light = payload;        // remember last input

    if (payload == "ON") {
        g_lightInstance->setOn(/*manual*/ true);
    } else {
        g_lightInstance->setOff();
    }
}

void LightController::begin(MqttPublisher& publisher) {
    _publisher    = &publisher;
    g_lightInstance = this;

    pinMode(PIN_LIGHT_MAIN,   OUTPUT);
    pinMode(PIN_LIGHT_EDISON, OUTPUT);
    digitalWrite(PIN_LIGHT_MAIN,   LOW);
    digitalWrite(PIN_LIGHT_EDISON, LOW);

    g_state.light         = "OFF";
    g_state.lightNow      = "OFF";
    g_state.lightSelected = "MAIN";

    Logger::info(F("light"), F("controller ready, GPIO init"));
}

void LightController::tick() {
    if (!_isOn || _timeoutMs == 0) return;

    unsigned long now = millis();
    if (now - _onSinceMs >= _timeoutMs) {
        Logger::info(F("light"), F("auto-off timer expired"));
        setOff();
    }
}

void LightController::setOn(bool manual) {
    _isOn        = true;
    _onManual    = manual;
    _onSinceMs   = millis();
    _timeoutMs   = manual ? LIGHT_TIMEOUT_MQTT_MS
                          : LIGHT_TIMEOUT_DEFAULT_MS;
    applyLevel();

    // Publish legacy siblings.
    if (_publisher) {
        _publisher->enqueue("light",     "ON", 0, true);
        _publisher->enqueue("lightNow",  "ON", 0, false);
    }
    Logger::info(F("light"), manual ? F("manual ON") : F("motion ON"));
}

void LightController::setOff() {
    _isOn      = false;
    _onManual  = false;
    _timeoutMs = 0;

    digitalWrite(PIN_LIGHT_MAIN,   LOW);
    if (g_state.lightSelected == "MAIN")
        digitalWrite(PIN_LIGHT_EDISON, LOW);
    else
        digitalWrite(PIN_LIGHT_EDISON, LOW);

    g_state.lightNow = "OFF";

    if (_publisher) {
        // "light" mirrors the input — original Lua sets `dat.light = 'OFF'`
        // explicitly when turning OFF.
        _publisher->enqueue("light",    "OFF", 0, true);
        _publisher->enqueue("lightNow", "OFF", 0, false);
    }
    Logger::info(F("light"), F("OFF"));
}

void LightController::applyLevel() {
    if (g_state.lightSelected == "MAIN") {
        digitalWrite(PIN_LIGHT_MAIN,   HIGH);
        digitalWrite(PIN_LIGHT_EDISON, LOW);
    } else {
        digitalWrite(PIN_LIGHT_MAIN,   LOW);
        digitalWrite(PIN_LIGHT_EDISON, HIGH);
    }
    g_state.lightNow = "ON";
}
