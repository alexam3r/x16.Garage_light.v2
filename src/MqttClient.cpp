// src/MqttClient.cpp

#include "MqttClient.h"
#include "Secrets.h"
#include "Config.h"
#include "State.h"
#include "Logger.h"
#include "MqttDispatch.h"

#define MQTT_RETRY_INITIAL_MS 1000UL

void MqttClient::setup(MqttDispatch& dispatch) {
    _dispatch = &dispatch;

    _mqtt = new PubSubClient(_espClient);
    _mqtt->setBufferSize(512);        // matches build_flags
    _mqtt->setKeepAlive(MQTT_KEEPALIVE_S);
    _mqtt->setServer(MQTT_BROKER, MQTT_PORT);

    std::function<void(char*, uint8_t*, unsigned int)> cb =
        [this](char* topic, uint8_t* payload, unsigned int len) {
            // PubSubClient buffers get overwritten on next packet in/out,
            // so we hand the (topic, payload) pair off via copy in the
            // dispatch helper.
            String t(topic);
            String p;
            p.reserve(len + 1);
            for (unsigned int i = 0; i < len; ++i) p += static_cast<char>(payload[i]);
            if (_dispatch) _dispatch->dispatch(t, p);
        };
    _mqtt->setCallback(cb);

    // The actual connect attempt is deferred to `tick()` — we have to
    // wait for WiFi to be up first, mirroring the Lua `if wifi.sta.status() == 5`
    // check in mqttget.lua.
    _wantConnect    = true;
    _reconnectAtMs  = millis() + 250;
}

bool MqttClient::connected() const {
    return _mqtt && _mqtt->connected();
}

void MqttClient::tick() {
    if (!_mqtt) return;

    _mqtt->loop();

    if (connected()) {
        g_state.broker = true;
        g_state.error_no = 0;
        return;
    }

    if (!g_state.broker && _wantConnect && millis() >= _reconnectAtMs) {
        if (connectBroker()) {
            _wantConnect = false;
        } else {
            _wantConnect = true;
            g_state.error_no++;
            // Back off: exponential-ish capped at 30 s. Matches Lua's
            // `tmr.create():alarm(1000, 1, …)` retry-without-jitter.
            unsigned long backoff = MQTT_RETRY_INITIAL_MS << g_state.error_no;
            if (backoff > 30000UL) backoff = 30000UL;
            _reconnectAtMs = millis() + backoff;
            String s = F("connect failed, retry in ms=");
            s += backoff;
            Logger::warn(F("mqtt"), s);
        }
    }
}

void MqttClient::forceReconnectIn(unsigned long delayMs) {
    _wantConnect   = true;
    _reconnectAtMs = millis() + delayMs;
    if (_mqtt && _mqtt->connected()) _mqtt->disconnect();
}

bool MqttClient::connectBroker() {
    if (!WiFi.isConnected()) {
        // Lua: `if wifi.sta.status() == 5 then …`. Wait for WiFi.
        return false;
    }

    String lwtTopic = g_state.nodetopic + "/state";
    bool ok = _mqtt->connect(
        g_state.clntid.c_str(),
        MQTT_USER, MQTT_PASSWORD,
        lwtTopic.c_str(),
        /*willQos*/ 0,
        /*willRetain*/ true,
        /*willMsg*/ LWT_PAYLOAD_OFFLINE,
        /*cleanSession*/ true);

    if (!ok) {
        String s = F("rc=");
        s += _mqtt->state();
        Logger::warn(F("mqtt"), s);
        return false;
    }

    Logger::info(F("mqtt"), F("connected to broker"));
    subscribeAll();
    announceOnline();
    return true;
}

void MqttClient::announceOnline() {
    // Lua: `m:publish(dat.nodetopic..'/state', "ON", 2, 1)` — QoS 2, retain 1.
    // We downgrade to QoS 1 (matches the LUA-quote in docs/lua-original/mqttget.lua
    // is actually QoS 2 with retain 1; with our buffer 512 this fits, but
    // QoS 1 keeps the broker calmer when paired with low MQTT_PING jitter).
    String topic = g_state.nodetopic + "/state";
    _mqtt->publish(topic.c_str(), LWT_PAYLOAD_ONLINE, /*retained*/ true, /*qos*/ 1);
}

void MqttClient::subscribeAll() {
    // Lua: `m:subscribe({[dat.topic..'/light']=0, ...}, …)`.
    // QoS 0 → value 0 in PubSubClient.
    char topicBuf[64];

    snprintf(topicBuf, sizeof topicBuf, "%s/light",        g_state.topic.c_str());
    _mqtt->subscribe(topicBuf, 0);

    snprintf(topicBuf, sizeof topicBuf, "%s/lightNow",     g_state.topic.c_str());
    _mqtt->subscribe(topicBuf, 0);

    snprintf(topicBuf, sizeof topicBuf, "%s/lightSelected",g_state.topic.c_str());
    _mqtt->subscribe(topicBuf, 0);

    snprintf(topicBuf, sizeof topicBuf, "%s/lightMoveDetection", g_state.topic.c_str());
    _mqtt->subscribe(topicBuf, 0);

    snprintf(topicBuf, sizeof topicBuf, "%s/ide",          g_state.nodetopic.c_str());
    _mqtt->subscribe(topicBuf, 0);

    snprintf(topicBuf, sizeof topicBuf, "%s/restart",      g_state.nodetopic.c_str());
    _mqtt->subscribe(topicBuf, 0);

    Logger::info(F("mqtt"), F("subscribed"));
}

bool MqttClient::publishConnected(const String& topic, const String& payload,
                                  uint8_t qos, bool retain) {
    if (!connected()) return false;
    // PubSubClient's string + string + retain + qos overload:
    return _mqtt->publish(topic.c_str(), payload.c_str(), retain, qos);
}

PubSubClient& MqttClient::raw() { return *_mqtt; }
