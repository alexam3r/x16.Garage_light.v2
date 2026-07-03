// src/main.cpp — прошивка контроллера света в гараже.
// ESP8266 + PubSubClient. Весь код проекта в одном файле, кроме
// Config.h и Secrets.h. Логика старается повторять исходную Lua-версию
// из docs/lua-original/main.lua и соседних файлов.
//
// Дерево топиков MQTT:
//   garage/light/<leaf>                ← общее (light, lightMoveDetection и т. п.)
//   garage/light/<clntid>/<leaf>       ← per-device (state, heap, uptime)

#include <Arduino.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "Config.h"
#include "Secrets.h"

// ─────────────────────────────────────────────────────────────────────────────
// Глобальное состояние — аналог Lua `dat`.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct State {
    String clntid;
    String nodetopic;
    String topic;

    // Состояние света.
    String  light;          // вход MQTT (ON|OFF)
    String  lightNow;       // фактическое состояние
    String  lightSelected;  // MAIN|EDISON

    uint32_t count;
    uint32_t error_no;
    bool     broker;
    String   message;
};

State g_state;

void state_begin() {
    g_state.clntid    = String(ESP.getChipId());
    g_state.nodetopic = String(TOPIC_BASE) + "/" + g_state.clntid;
    g_state.topic     = String(TOPIC_BASE);

    g_state.light         = "OFF";
    g_state.lightNow      = "OFF";
    g_state.lightSelected = "MAIN";

    g_state.count    = 0;
    g_state.error_no = 0;
    g_state.broker   = false;
    g_state.message  = "";
}

// ─────────────────────────────────────────────────────────────────────────────
// Логгер — единый префикс [L][uptime][module] message. Заменяет ad-hoc
// `print()` из оригинала на Lua.
// ─────────────────────────────────────────────────────────────────────────────

class Logger {
public:
    static void begin(unsigned long baud = 115200) {
        Serial.begin(baud);
        delay(50);
    }

    // Модуль — строкой из flash (через F(...)), сообщение — flash или RAM.
    static void info (const __FlashStringHelper* m, const __FlashStringHelper* s) { emit('I', m, String(s)); }
    static void warn (const __FlashStringHelper* m, const __FlashStringHelper* s) { emit('W', m, String(s)); }
    static void error(const __FlashStringHelper* m, const __FlashStringHelper* s) { emit('E', m, String(s)); }

    static void info (const __FlashStringHelper* m, const String& s) { emit('I', m, s); }
    static void warn (const __FlashStringHelper* m, const String& s) { emit('W', m, s); }
    static void error(const __FlashStringHelper* m, const String& s) { emit('E', m, s); }

private:
    static void emit(char lvl, const __FlashStringHelper* m, const String& s) {
        Serial.print('[');
        Serial.print(lvl);
        Serial.print(F("]["));
        Serial.print(millis());
        Serial.print(F("]["));
        Serial.print(m);              // На ESP8266 Serial.print умеет PROGMEM-строки.
        Serial.print(F("] "));
        Serial.println(s);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Wi-Fi. Сильно напоминает mqttset.lua/mqttget.lua: «не звоним брокеру
// пока Wi-Fi не поднят».
// ─────────────────────────────────────────────────────────────────────────────

class WifiManager {
public:
    void begin(const char* ssid, const char* password) {
        if (!ssid || !*ssid) {
            Logger::warn(F("wifi"), F("SSID пустой, пропускаем"));
            return;
        }
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.persistent(true);
        Logger::info(F("wifi"), String(F("connecting to '")) + ssid + "'");
        WiFi.begin(ssid, password);
    }

    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

    void tick() {
        if (_announced) return;
        if (!isConnected()) return;
        Logger::info(F("wifi"), String(F("connected, ip=")) + WiFi.localIP().toString());
        _announced = true;
    }

private:
    bool _announced = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// MQTT-клиент. Тело методов вынесено после полного объявления MqttDispatcher
// и MqttPublisher, чтобы они виделись как полные типы.
// ─────────────────────────────────────────────────────────────────────────────

class MqttDispatcher;  // forward

class MqttClient {
public:
    bool connected() const { return _mqtt && _mqtt->connected(); }
    void begin(MqttDispatcher& dispatch);
    void tick();

    // Publisher стучится сюда. Универсальная сигнатура работает на
    // PubSubClient 2.6/2.7/2.8: (topic, const uint8_t*, len, retain).
    bool publishRaw(const String& topic, const String& payload, bool retain) {
        if (!connected()) return false;
        return _mqtt->publish(topic.c_str(),
                              (const uint8_t*)payload.c_str(),
                              payload.length(),
                              retain);
    }

private:
    WiFiClient       _espClient;
    PubSubClient*    _mqtt      = nullptr;
    MqttDispatcher*  _dispatch  = nullptr;
    bool             _want     = false;
    unsigned long    _retryAtMs = 0;

    bool doConnect();
};

// ─────────────────────────────────────────────────────────────────────────────
// Out-очередь (бывший topub). Одна публикация в полёте, чтобы стек TCP
// не захлёбывался.
// ─────────────────────────────────────────────────────────────────────────────

class MqttPublisher {
public:
    void begin(MqttClient& mqtt) { _mqtt = &mqtt; reset(); }

    // QoS всегда 0 — для телеметрии в LAN этого хватает.
    void enqueue(const String& leaf, const String& payload, bool retain = false) {
        if (_size >= QUEUE_CAPACITY) {
            _head = (_head + 1) % QUEUE_CAPACITY;
            --_size;
            Logger::warn(F("pub"), F("queue full, dropped oldest"));
        }
        _q[_tail].leaf    = leaf;
        _q[_tail].payload = payload;
        _q[_tail].retain  = retain;
        _tail = (_tail + 1) % QUEUE_CAPACITY;
        ++_size;
    }

    void tick() {
        if (!_mqtt) return;
        if (_inFlight) { _inFlight = false; return; }
        if (_size == 0) return;
        if (!_mqtt->connected()) return;

        Item it = _q[_head];
        if (!_mqtt->publishRaw(String(g_state.topic) + "/" + it.leaf, it.payload, it.retain)) {
            Logger::warn(F("pub"), F("publish failed"));
            return;
        }
        _inFlight = true;
        _head = (_head + 1) % QUEUE_CAPACITY;
        --_size;
    }

private:
    static constexpr size_t QUEUE_CAPACITY = 16;

    struct Item {
        String leaf;
        String payload;
        bool   retain;
    };

    Item        _q[QUEUE_CAPACITY];
    size_t      _head    = 0;
    size_t      _tail    = 0;
    size_t      _size    = 0;
    bool        _inFlight = false;
    MqttClient* _mqtt     = nullptr;

    void reset() { _head = _tail = _size = 0; _inFlight = false; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Dispatcher — таблица обработчиков по последнему сегменту топика (как
// string.match(topic, './(%w+)$') в mqttanalise.lua).
// ─────────────────────────────────────────────────────────────────────────────

class MqttDispatcher {
public:
    typedef void (*HandlerFn)(const String& payload);

    struct Entry { const char* leaf; HandlerFn fn; };

    void begin(const Entry* table, size_t n) { _table = table; _count = n; }

    // Топик и payload всегда копии, потому что буфер PubSubClient
    // перезаписывается между приёмами.
    void dispatch(const String& topic, const String& payload) {
        for (size_t i = 0; i < _count; ++i) {
            const Entry& e = _table[i];
            if (!e.fn || !e.leaf) continue;
            if (matchLeaf(topic, e.leaf)) { e.fn(payload); return; }
        }
        Logger::warn(F("dispatch"), String(F("unhandled: ")) + topic);
    }

private:
    const Entry* _table = nullptr;
    size_t       _count = 0;

    static bool matchLeaf(const String& topic, const char* leaf) {
        size_t tlen = topic.length();
        size_t llen = leaf ? strlen(leaf) : 0;
        if (tlen < llen + 1) return false;
        if (topic[tlen - llen - 1] != '/') return false;
        return strncmp(topic.c_str() + tlen - llen, leaf, llen) == 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Контроллер света — аналог light()/moveDetected() в Lua main.lua.
// В этой версии обрабатывает только MQTT-команду ON/OFF; PIR и выбор
// MAIN/EDISON — следующая итерация.
// ─────────────────────────────────────────────────────────────────────────────

class LightController {
public:
    static LightController* _instance;     // trampoline для Dispatcher.

    void begin(MqttPublisher& pub) {
        _pub = &pub;
        _instance = this;
        setupPins();
        applyLight("OFF");
        Logger::info(F("light"), F("controller ready"));
    }

    void tick() {
        if (_isOn && _timeoutMs && (millis() - _onSinceMs >= _timeoutMs)) {
            Logger::info(F("light"), F("auto-off"));
            cmd("OFF", /*manual*/ false);
        }
    }

    static void onLightCommandStatic(const String& payload) {
        if (!_instance) return;
        // Lua mqttanalise.lua: `if itm[2] ~= dat.light then dat.light = itm[2]; …`.
        if (payload == g_state.light) return;
        g_state.light = payload;
        _instance->cmd(payload, /*manual*/ true);
    }

private:
    MqttPublisher*  _pub        = nullptr;
    bool            _isOn       = false;
    unsigned long   _onSinceMs  = 0;
    unsigned long   _timeoutMs  = 0;

    void setupPins() {
        pinMode(PIN_LIGHT_MAIN,   OUTPUT);
        pinMode(PIN_LIGHT_EDISON, OUTPUT);
        digitalWrite(PIN_LIGHT_MAIN,   LOW);
        digitalWrite(PIN_LIGHT_EDISON, LOW);
    }

    // manual=true  ⇒  таймаут 3600s (mqttget.lua при удалённом включении).
    // manual=false ⇒  таймаут 600s  (после детектирования движения).
    void cmd(const String& target, bool manual) {
        _isOn      = (target == "ON");
        _onSinceMs = millis();
        _timeoutMs = manual ? LIGHT_TIMEOUT_MQTT_MS : LIGHT_TIMEOUT_DEFAULT_MS;
        applyLight(target);
        if (_pub) {
            _pub->enqueue("light",    target, /*retain*/ true);
            _pub->enqueue("lightNow", target, /*retain*/ false);
        }
        Logger::info(F("light"), manual ? F("manual") : F("motion"));
    }

    void applyLight(const String& target) {
        if (target == "ON") {
            if (g_state.lightSelected == "MAIN") {
                digitalWrite(PIN_LIGHT_MAIN,   HIGH);
                digitalWrite(PIN_LIGHT_EDISON, LOW);
            } else {
                digitalWrite(PIN_LIGHT_MAIN,   LOW);
                digitalWrite(PIN_LIGHT_EDISON, HIGH);
            }
            g_state.lightNow = "ON";
        } else {
            digitalWrite(PIN_LIGHT_MAIN,   LOW);
            digitalWrite(PIN_LIGHT_EDISON, LOW);
            g_state.lightNow = "OFF";
        }
    }
};

LightController* LightController::_instance = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Глобальный композит — синглтоны, видимые и в setup()/loop().
// ─────────────────────────────────────────────────────────────────────────────

WifiManager       g_wifi;
MqttPublisher     g_publisher;
MqttClient        g_mqtt;
MqttDispatcher    g_dispatch;
LightController   g_light;

const MqttDispatcher::Entry kDispatchTable[] = {
    { "light", &LightController::onLightCommandStatic },
};
constexpr size_t kDispatchTableSize = sizeof(kDispatchTable) / sizeof(kDispatchTable[0]);

Ticker heartbeat;

void heartbeat1Hz() {
    ++g_state.count;
    // Lua main.lua: раз в 60 тиков публикуем heap/uptime через g_publisher.
    if (g_state.count % HEARTBEAT_DIAG_PERIOD == 0) {
        g_publisher.enqueue(String(g_state.clntid) + "/heap",   String(ESP.getFreeHeap()),     /*retain*/ false);
        g_publisher.enqueue(String(g_state.clntid) + "/uptime", String(millis() / 1000UL),    /*retain*/ false);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Тело MqttClient. Сейчас видны MqttDispatcher и Loger как полные типы.
// ─────────────────────────────────────────────────────────────────────────────

static MqttDispatcher* g_dispatcher = nullptr;

void MqttClient::begin(MqttDispatcher& dispatch) {
    _dispatch = &dispatch;
    g_dispatcher = &dispatch;

    _mqtt = new PubSubClient(_espClient);
    _mqtt->setBufferSize(512);
    _mqtt->setKeepAlive(MQTT_KEEPALIVE_S);
    _mqtt->setServer(MQTT_BROKER, MQTT_PORT);

    _mqtt->setCallback([](char* topic, uint8_t* payload, unsigned int len) {
        if (!g_dispatcher) return;
        String t(topic);
        String p;
        p.reserve(len + 1);
        for (unsigned int i = 0; i < len; ++i) p += (char)payload[i];
        g_dispatcher->dispatch(t, p);
    });

    _want     = true;
    _retryAtMs = millis() + 250;
}

bool MqttClient::doConnect() {
    if (!WiFi.isConnected()) return false;

    String lwt = g_state.nodetopic + "/state";
    bool ok = _mqtt->connect(g_state.clntid.c_str(),
                             MQTT_USER, MQTT_PASSWORD,
                             lwt.c_str(),
                             /*willQos*/ 0,
                             /*willRetain*/ true,
                             LWT_PAYLOAD_OFFLINE,
                             /*cleanSession*/ true);
    if (!ok) {
        String s = F("rc="); s += _mqtt->state();
        Logger::warn(F("mqtt"), s);
        return false;
    }

    Logger::info(F("mqtt"), F("connected to broker"));

    // Подписки — те же 6 топиков, что в mqttget.lua.
    char buf[64];
    snprintf(buf, sizeof buf, "%s/light",              g_state.topic.c_str());     _mqtt->subscribe(buf, 0);
    snprintf(buf, sizeof buf, "%s/lightNow",           g_state.topic.c_str());     _mqtt->subscribe(buf, 0);
    snprintf(buf, sizeof buf, "%s/lightSelected",      g_state.topic.c_str());     _mqtt->subscribe(buf, 0);
    snprintf(buf, sizeof buf, "%s/lightMoveDetection", g_state.topic.c_str());     _mqtt->subscribe(buf, 0);
    snprintf(buf, sizeof buf, "%s/ide",                g_state.nodetopic.c_str()); _mqtt->subscribe(buf, 0);
    snprintf(buf, sizeof buf, "%s/restart",            g_state.nodetopic.c_str()); _mqtt->subscribe(buf, 0);
    Logger::info(F("mqtt"), F("subscribed"));

    // LWT-birth (mqttget.lua: `m:publish(…, "ON", 2, 1)`).
    String stateTopic = g_state.nodetopic + "/state";
    _mqtt->publish(stateTopic.c_str(),
                   (const uint8_t*)LWT_PAYLOAD_ONLINE,
                   strlen(LWT_PAYLOAD_ONLINE),
                   /*retain*/ true);
    return true;
}

void MqttClient::tick() {
    if (!_mqtt) return;
    _mqtt->loop();

    if (connected()) {
        if (!g_state.broker) {
            g_state.broker   = true;
            g_state.error_no = 0;
        }
        return;
    }

    if (g_state.broker) g_state.broker = false;

    if (!_want) return;
    if (millis() < _retryAtMs) return;

    if (doConnect()) {
        _want = false;
    } else {
        ++g_state.error_no;
        unsigned long backoff = 1000UL << g_state.error_no;
        if (backoff > 30000UL) backoff = 30000UL;
        _retryAtMs = millis() + backoff;
        String m = F("retry in ms="); m += backoff;
        Logger::warn(F("mqtt"), m);
    }
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// setup()/loop() — снаружи анонимного namespace, чтобы платформа видела
// символы как глобальные.
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Logger::begin();
    Logger::info(F("boot"), F("starting x16 garage light"));

    state_begin();

    g_wifi.begin(WIFI_SSID, WIFI_PASSWORD);
    g_publisher.begin(g_mqtt);
    g_light.begin(g_publisher);   // внутри ставит LightController::_instance = &g_light

    g_dispatch.begin(kDispatchTable, kDispatchTableSize);
    g_mqtt.begin(g_dispatch);

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
