// src/WifiManager.cpp

#include "WifiManager.h"
#include "Logger.h"

void WifiManager::begin(const char* ssid, const char* password) {
    if (!ssid || !*ssid) {
        Logger::warn(F("wifi"), F("SSID empty, skipping"));
        return;
    }
    _haveCreds = true;

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    WiFi.onStationModeGotIP([this](const WiFiEventStationModeGotIP& e) {
        _connectedAnnounced = false;
    });
    WiFi.onStationModeDisconnected([this](const WiFiEventStationModeDisconnected&) {
        _connectedAnnounced = false;
    });

    Logger::info(F("wifi"), String(F("connecting to '")) + ssid + F("'"));
    WiFi.begin(ssid, password);
}

bool WifiManager::isConnected() const {
    return _haveCreds && WiFi.status() == WL_CONNECTED;
}

void WifiManager::tick() {
    if (!isConnected() || _connectedAnnounced) return;

    // One-shot log on the rising edge of "got IP".
    IPAddress ip = WiFi.localIP();
    String    s  = F("connected, ip=");
    s += ip.toString();
    Logger::info(F("wifi"), s);
    _connectedAnnounced = true;
}

// Inline placeholders (we capture `this` via lambdas, but keep member
// signatures for clarity/tests).
void WifiManager::onGotIp   (const WiFiEventStationModeGotIP&)        {}
void WifiManager::onDisconn (const WiFiEventStationModeDisconnected&)  {}
