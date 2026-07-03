// src/MqttClient.h
// ─────────────────────────────────────────────────────────────────────────────
// ESP8266 + PubSubClient wrapper. Runs the reconnect loop, holds the LWT,
// exposes `connected()` for the publisher queue, and forwards every
// incoming message to `MqttDispatch` via a function pointer installed at
// `setup()` time.
//
// Mirrors the original Lua broker code:
//   main/mqttset.lua   — LWT, offline reconnect, message handler
//   main/mqttget.lua   — connect loop + subscriptions
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

class MqttDispatch;  // forward — handed in to setup().

class MqttClient {
public:
    void setup(MqttDispatch& dispatch);
    bool connected() const;
    void tick();
    void forceReconnectIn(unsigned long delayMs);

    // ── Direct publish API (used by MqttPublisher) ───────────────────────────
    bool publishConnected(const String& topic, const String& payload,
                          uint8_t qos, bool retain);
    PubSubClient& raw();  // for testing only — keep minimal.

private:
    PubSubClient* _mqtt       = nullptr;
    WiFiClient    _espClient;
    MqttDispatch* _dispatch   = nullptr;

    bool          _wantConnect = false;
    unsigned long _reconnectAtMs = 0;

    bool connectBroker();
    void announceOnline();
    void subscribeAll();
};
