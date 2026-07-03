// src/main.cpp
// ─────────────────────────────────────────────────────────────────────────────
// Entry point. Composes the singleton modules in setup() and drives the
// 1-Hz loop. Mirrors the chaining pattern of the original Lua's
//   init.lua → setglobals.lua → mqttset.lua → main.lua
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <Ticker.h>

#include "Config.h"
#include "Secrets.h"
#include "State.h"
#include "Logger.h"
#include "WifiManager.h"
#include "MqttClient.h"
#include "MqttDispatch.h"
#include "MqttPublisher.h"
#include "LightController.h"

// We deliberately keep single ownership inside this file: the hardware
// reference objects live in their own compilation units and are linked
// as `extern` references (State, g_state). The other modules
// (Wifi, Mqtt, Publisher, Dispatch, Light) are composed here so that
// the wiring diagram matches `main.lua`'s boot chain.

namespace {

WifiManager      g_wifi;
MqttPublisher    g_publisher;
MqttClient       g_mqtt;
MqttDispatch     g_dispatch;
LightController  g_light;

// Dispatch table — keep indexed by leaf.
const MqttDispatch::Entry kDispatchTable[] = {
    { "light",        &LightController::onLightCommandStatic },
};
constexpr size_t kDispatchTableSize =
    sizeof(kDispatchTable) / sizeof(kDispatchTable[0]);

Ticker heartbeat;

void heartbeat1Hz() {
    ++g_state.count;

    // 60-tick diagnostics match — see main.lua's `dat.count >= 60` block.
    if (g_state.count % HEARTBEAT_DIAG_PERIOD == 0) {
        g_publisher.enqueue(String(g_state.clntid) + "/heap",
                            String(ESP.getFreeHeap()), 0, false);
        g_publisher.enqueue(String(g_state.clntid) + "/uptime",
                            String(millis() / 1000UL), 0, false);
    }
}

}  // namespace

void setup() {
    Logger::begin();
    Logger::info(F("boot"), F("starting x16 garage light"));

    g_state.begin();

    g_wifi.begin(WIFI_SSID, WIFI_PASSWORD);

    g_publisher.begin(g_mqtt);
    g_light.begin(g_publisher);

    g_dispatch.begin(kDispatchTable, kDispatchTableSize);
    g_mqtt.setup(g_dispatch);

    heartbeat.attach_ms(HEARTBEAT_PERIOD_MS, heartbeat1Hz);

    Logger::info(F("boot"), F("setup complete"));
}

void loop() {
    g_wifi.tick();
    g_mqtt.tick();
    g_publisher.tick();
    g_light.tick();

    delay(LOOP_TICK_BUDGET_MS);
}
