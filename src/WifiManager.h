// src/WifiManager.h
// ─────────────────────────────────────────────────────────────────────────────
// Thin wrapper around `WiFi`. Owns the connect-on-boot attempt and lets
// `MqttClient` know whether to attempt/persist its broker connection.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>

class WifiManager {
public:
    void begin(const char* ssid, const char* password);
    bool isConnected() const;

    // Re-check internal state; called each tick (~1 Hz).
    void tick();

private:
    bool        _haveCreds = false;
    bool        _connectedAnnounced = false;

    void onGotIp   (const WiFiEventStationModeGotIP&       event);
    void onDisconn (const WiFiEventStationModeDisconnected& event);
};
