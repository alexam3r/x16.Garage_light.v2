// src/main.cpp — прошивка контроллера света в гараже.
// ESP8266 + PubSubClient 2.x + ArduinoJson 7.x.
//
// Структура MQTT — плоское дерево garage/light/<leaf>, без <clntid>.
// Подробное описание и таблица топиков — README.md.
//
// В src/ лежат только Config.h и Secrets.h(.sample). Весь код прошивки
// в одном файле.

#include <Arduino.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "Secrets.h"

// ─────────────────────────────────────────────────────────────────────────────
// Глобальное состояние
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct State {
    // Сетевое.
    String  ip;
    int     rssi = 0;

    // Управление.
    String  mode;            // MAIN | EDISON | OFF
    String  lightNow;        // ON | OFF
    bool    motionArmed;     // true ⇒ PIR триггерит авто-включение

    // Сенсоры.
    float   tempC    = NAN;
    float   humPct   = NAN;

    // Heartbeat.
    uint32_t count     = 0;
    uint32_t errorNo   = 0;   // счётчик ошибок коннекта
    bool     connected = false;

    void applyMode(String newMode);
    void applyLight(String target);
};

State g_state;

void State::applyMode(String newMode) {
    if (newMode != MODE_MAIN && newMode != MODE_EDISON && newMode != MODE_OFF)
        return;
    mode = newMode;
    // Если свет горит и mode изменился — переключаем активный реле.
    if (lightNow == LIGHT_ON) applyLight(LIGHT_ON);
}

void State::applyLight(String target) {
    bool wantOn = (target == LIGHT_ON);
    digitalWrite(PIN_LIGHT_MAIN,
                 (wantOn && mode == MODE_MAIN) ? HIGH : LOW);
    digitalWrite(PIN_LIGHT_EDISON,
                 (wantOn && mode == MODE_EDISON) ? HIGH : LOW);
    lightNow = wantOn ? LIGHT_ON : LIGHT_OFF;
}

// ─────────────────────────────────────────────────────────────────────────────
// Логгер [L][uptime][module] message.
// При включённом CFG_VERBOSE_LOG доступны макросы VLOG_I/W/E для
// подробной диагностики. Иначе они компилируются в no-op (= 0 flash).
// ─────────────────────────────────────────────────────────────────────────────

class Logger {
public:
    static void begin(unsigned long baud = 115200) {
        Serial.begin(baud);
        delay(50);
    }
    static void info (const __FlashStringHelper* m, const __FlashStringHelper* s) { emit('I', m, String(s)); }
    static void warn (const __FlashStringHelper* m, const __FlashStringHelper* s) { emit('W', m, String(s)); }
    static void error(const __FlashStringHelper* m, const __FlashStringHelper* s) { emit('E', m, String(s)); }
    static void info (const __FlashStringHelper* m, const String& s) { emit('I', m, s); }
    static void warn (const __FlashStringHelper* m, const String& s) { emit('W', m, s); }
    static void error(const __FlashStringHelper* m, const String& s) { emit('E', m, s); }

private:
    static void emit(char lvl, const __FlashStringHelper* m, const String& s) {
        Serial.print('['); Serial.print(lvl); Serial.print(F("]["));
        Serial.print(millis()); Serial.print(F("]["));
        Serial.print(m); Serial.print(F("] "));
        Serial.println(s);
    }
};

#if CFG_VERBOSE_LOG
#define VLOG_I(m, s) Logger::info((m), (s))
#define VLOG_W(m, s) Logger::warn((m), (s))
#define VLOG_E(m, s) Logger::error((m), (s))
#else
#define VLOG_I(m, s) do {} while (0)
#define VLOG_W(m, s) do {} while (0)
#define VLOG_E(m, s) do {} while (0)
#endif

// ─────────────────────────────────────────────────────────────────────────────
// I²C-драйвер AM2320. Простая polling-реализация по даташиту:
// WAKE (≥800 µs silence) → read regs 0x0000..0x0003 → CRC.
// ─────────────────────────────────────────────────────────────────────────────

class Am2320 {
public:
    bool begin() {
        // На NodeMCU v2 I²C пины: D1=IO2, D2=IO4 (по документации).
        Wire.begin(/*sda*/ 2, /*scl*/ 4);
        return true;
    }

    bool read(float& tempC, float& humPct) {
        // 1) «Разбудить» — короткий START-STOP (>800 µs), затем пауза.
        Wire.beginTransmission(AM2320_ADDR);
        Wire.endTransmission();
        delayMicroseconds(1500);

        // 2) Команда чтения регистров 0x0000..0x0003 (hum MSB|LSB, temp MSB|LSB).
        Wire.beginTransmission(AM2320_ADDR);
        Wire.write(0x03);                       // function code
        Wire.write(0x00); Wire.write(0x00);     // start = 0x0000
        Wire.write(0x00); Wire.write(0x04);     // length = 4 regs
        if (Wire.endTransmission() != 0) return false;
        delayMicroseconds(1500);

        // 3) Получаем 8 байт: [func, len, humMSB, humLSB, tempMSB, tempLSB, crclow, crchigh].
        if (Wire.requestFrom(AM2320_ADDR, (uint8_t)8) != 8) return false;
        uint8_t buf[8];
        for (uint8_t i = 0; i < 8; ++i) {
            if (!Wire.available()) return false;
            buf[i] = Wire.read();
        }

        // 4) CRC по даташиту (poly 0xA001).
        uint16_t crc = 0xFFFF;
        for (uint8_t i = 0; i < 6; ++i) {
            crc ^= buf[i];
            for (uint8_t j = 0; j < 8; ++j) {
                crc = (crc & 0x01) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
            }
        }
        if ((uint8_t)(crc & 0xFF) != buf[7]) return false;
        if ((uint8_t)(crc >> 8)   != buf[6]) return false;

        uint16_t humRaw  = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t tempRaw = ((uint16_t)buf[4] << 8) | buf[5];
        humPct = humRaw  / 10.0f;
        tempC  = tempRaw / 10.0f;
        return true;
    }

private:
    static constexpr uint8_t AM2320_ADDR = 0x5C;
};

Am2320 g_am2320;

// ─────────────────────────────────────────────────────────────────────────────
// Wi-Fi
// ─────────────────────────────────────────────────────────────────────────────

class WifiManager {
public:
    void begin(const char* ssid, const char* password) {
        if (!ssid || !*ssid) { Logger::warn(F("wifi"), F("SSID пустой")); return; }
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
        g_state.ip   = WiFi.localIP().toString();
        g_state.rssi = WiFi.RSSI();
        Logger::info(F("wifi"), String(F("connected, ip=")) + g_state.ip);
        _announced = true;
    }

private:
    bool _announced = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// MQTT-клиент. Тело методов вынесено после полного объявления MqttDispatcher
// и MqttPublisher, чтобы они виделись как полные типы.
// ─────────────────────────────────────────────────────────────────────────────

class MqttDispatcher;

class MqttClient {
public:
    bool connected() const { return _mqtt && _mqtt->connected(); }
    void begin(MqttDispatcher& dispatch);
    void tick();

    // PubSubClient 2.x: (topic, const uint8_t*, len, retain) — наша единственная
    // сигнатура с payload-as-string.
    bool publishRaw(const String& topic, const String& payload, bool retain) {
        if (!connected()) return false;
        return _mqtt->publish(topic.c_str(),
                              (const uint8_t*)payload.c_str(),
                              payload.length(),
                              retain);
    }

private:
    WiFiClient       _espClient;
    PubSubClient*    _mqtt       = nullptr;
    MqttDispatcher*  _dispatch   = nullptr;
    bool             _want       = false;
    unsigned long    _retryAtMs  = 0;
    bool             _haDiscoveryPublished = false;

    bool doConnect();
};

// ─────────────────────────────────────────────────────────────────────────────
// Out-очередь. Одна публикация в полёте.
// ─────────────────────────────────────────────────────────────────────────────

class MqttPublisher {
public:
    void begin(MqttClient& mqtt) { _mqtt = &mqtt; reset(); }

    void enqueueFullTopic(const String& topic, const String& payload, bool retain = false) {
        if (_size >= QUEUE_CAPACITY) {
            _head = (_head + 1) % QUEUE_CAPACITY;
            --_size;
            Logger::warn(F("pub"), F("queue full, dropped oldest"));
        }
        _q[_tail].topic   = topic;
        _q[_tail].payload = payload;
        _q[_tail].retain  = retain;
        _tail = (_tail + 1) % QUEUE_CAPACITY;
        ++_size;
    }

    void enqueueLeaf(const char* leaf, const String& payload, bool retain = false) {
        enqueueFullTopic(String(TOPIC_BASE) + "/" + leaf, payload, retain);
    }

    void tick() {
        if (!_mqtt) return;
        if (_inFlight) { _inFlight = false; return; }
        if (_size == 0) return;
        if (!_mqtt->connected()) return;

        Item it = _q[_head];
        if (!_mqtt->publishRaw(it.topic, it.payload, it.retain)) {
            Logger::warn(F("pub"), F("publish failed"));
            return;
        }
        _inFlight = true;
        _head = (_head + 1) % QUEUE_CAPACITY;
        --_size;
    }

    bool isEmpty() const { return _size == 0; }

private:
    static constexpr size_t QUEUE_CAPACITY = 16;

    struct Item {
        String topic;
        String payload;
        bool   retain;
    };

    Item        _q[QUEUE_CAPACITY];
    size_t      _head      = 0;
    size_t      _tail      = 0;
    size_t      _size      = 0;
    bool        _inFlight  = false;
    MqttClient* _mqtt      = nullptr;

    void reset() { _head = _tail = _size = 0; _inFlight = false; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Dispatcher
// ─────────────────────────────────────────────────────────────────────────────

class MqttDispatcher {
public:
    typedef void (*HandlerFn)(const String& payload);

    struct Entry { const char* leaf; HandlerFn fn; };

    void begin(const Entry* table, size_t n) { _table = table; _count = n; }

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
// HA discovery publisher — набор конфигов под homeassistant/<plat>/<uid>/config
// ─────────────────────────────────────────────────────────────────────────────

namespace ha {

// Хелпер: положить в JsonObject device общий шаблон.
void fillDevice(JsonObject dev) {
    JsonArray ids = dev["identifiers"].to<JsonArray>();
    ids.add(DEVICE_ID);
    dev["name"]        = DEVICE_NAME;
    dev["model"]       = DEVICE_MODEL;
    dev["manufacturer"]= DEVICE_MANUFACTURER;
    dev["sw_version"]  = DEVICE_SW_VERSION;
}

String serializeTo(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    return out;
}

void publishLightCore(MqttPublisher& publisher) {
    JsonDocument doc;
    doc["name"]              = "Garage Light";
    doc["unique_id"]         = String(DEVICE_ID) + "_light";
    doc["state_topic"]       = String(TOPIC_BASE) + "/" + LEAF_LIGHT + "/state";
    doc["command_topic"]     = String(TOPIC_BASE) + "/" + LEAF_LIGHT + "/set";
    doc["payload_on"]        = LIGHT_ON;
    doc["payload_off"]       = LIGHT_OFF;
    doc["availability_topic"]= String(TOPIC_BASE) + "/" + LEAF_AVAILABILITY;
    doc["payload_available"] = LWT_PAYLOAD_ONLINE;
    doc["payload_not_available"] = LWT_PAYLOAD_OFFLINE;
    fillDevice(doc["device"].to<JsonObject>());
    publisher.enqueueFullTopic(
        String(HA_DISCOVERY_PREFIX) + "/light/" + String(DEVICE_ID) + "_light/config",
        serializeTo(doc), /*retain*/ true);
}

void publishSelectMode(MqttPublisher& publisher) {
    JsonDocument doc;
    doc["name"]              = "Mode";
    doc["unique_id"]         = String(DEVICE_ID) + "_mode";
    doc["state_topic"]       = String(TOPIC_BASE) + "/" + LEAF_MODE + "/state";
    doc["command_topic"]     = String(TOPIC_BASE) + "/" + LEAF_MODE + "/set";
    JsonArray opts = doc["options"].to<JsonArray>();
    opts.add(MODE_MAIN);
    opts.add(MODE_EDISON);
    opts.add(MODE_OFF);
    doc["availability_topic"]= String(TOPIC_BASE) + "/" + LEAF_AVAILABILITY;
    doc["payload_available"] = LWT_PAYLOAD_ONLINE;
    doc["payload_not_available"] = LWT_PAYLOAD_OFFLINE;
    fillDevice(doc["device"].to<JsonObject>());
    publisher.enqueueFullTopic(
        String(HA_DISCOVERY_PREFIX) + "/select/" + String(DEVICE_ID) + "_mode/config",
        serializeTo(doc), /*retain*/ true);
}

void publishMotion(MqttPublisher& publisher) {
    JsonDocument doc;
    doc["name"]              = "Motion";
    doc["unique_id"]         = String(DEVICE_ID) + "_motion";
    doc["state_topic"]       = String(TOPIC_BASE) + "/" + LEAF_MOTION + "/state";
    doc["payload_on"]        = MOTION_ON;
    doc["payload_off"]       = MOTION_OFF;
    doc["device_class"]      = "motion";
    doc["availability_topic"]= String(TOPIC_BASE) + "/" + LEAF_AVAILABILITY;
    doc["payload_available"] = LWT_PAYLOAD_ONLINE;
    doc["payload_not_available"] = LWT_PAYLOAD_OFFLINE;
    fillDevice(doc["device"].to<JsonObject>());
    publisher.enqueueFullTopic(
        String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + String(DEVICE_ID) + "_motion/config",
        serializeTo(doc), /*retain*/ true);
}

void publishRestartButton(MqttPublisher& publisher) {
    JsonDocument doc;
    doc["name"]              = "Restart";
    doc["unique_id"]         = String(DEVICE_ID) + "_restart";
    doc["command_topic"]     = String(TOPIC_BASE) + "/" + LEAF_RESTART + "/set";
    doc["payload_press"]     = RESTART_CMD;
    doc["availability_topic"]= String(TOPIC_BASE) + "/" + LEAF_AVAILABILITY;
    doc["payload_available"] = LWT_PAYLOAD_ONLINE;
    doc["payload_not_available"] = LWT_PAYLOAD_OFFLINE;
    fillDevice(doc["device"].to<JsonObject>());
    publisher.enqueueFullTopic(
        String(HA_DISCOVERY_PREFIX) + "/button/" + String(DEVICE_ID) + "_restart/config",
        serializeTo(doc), /*retain*/ true);
}

void publishSensorJson(MqttPublisher& publisher,
                       const char* entityKey, const char* name,
                       const char* unit, const char* deviceClass) {
    JsonDocument doc;
    doc["name"]              = name;
    doc["unique_id"]         = String(DEVICE_ID) + "_" + entityKey;
    doc["state_topic"]       = String(TOPIC_BASE) + "/" + LEAF_STATUS;
    doc["value_template"]    = String("{{ value_json.") + entityKey + " }}";
    if (unit)        doc["unit_of_measurement"] = unit;
    if (deviceClass) doc["device_class"]        = deviceClass;
    doc["entity_category"]     = "diagnostic";
    doc["availability_topic"]  = String(TOPIC_BASE) + "/" + LEAF_AVAILABILITY;
    doc["payload_available"]   = LWT_PAYLOAD_ONLINE;
    doc["payload_not_available"] = LWT_PAYLOAD_OFFLINE;
    fillDevice(doc["device"].to<JsonObject>());
    publisher.enqueueFullTopic(
        String(HA_DISCOVERY_PREFIX) + "/sensor/" + String(DEVICE_ID) + "_" + entityKey + "/config",
        serializeTo(doc), /*retain*/ true);
}

void publishSensorPrimary(MqttPublisher& publisher,
                          const char* entityKey, const char* name,
                          const char* unit, const char* deviceClass) {
    JsonDocument doc;
    doc["name"]              = name;
    doc["unique_id"]         = String(DEVICE_ID) + "_" + entityKey;
    doc["state_topic"]       = String(TOPIC_BASE) + "/" + LEAF_STATUS;
    doc["value_template"]    = String("{{ value_json.") + entityKey + " }}";
    if (unit)        doc["unit_of_measurement"] = unit;
    if (deviceClass) doc["device_class"]        = deviceClass;
    doc["availability_topic"]  = String(TOPIC_BASE) + "/" + LEAF_AVAILABILITY;
    doc["payload_available"]   = LWT_PAYLOAD_ONLINE;
    doc["payload_not_available"] = LWT_PAYLOAD_OFFLINE;
    fillDevice(doc["device"].to<JsonObject>());
    publisher.enqueueFullTopic(
        String(HA_DISCOVERY_PREFIX) + "/sensor/" + String(DEVICE_ID) + "_" + entityKey + "/config",
        serializeTo(doc), /*retain*/ true);
}

void publishAll(MqttPublisher& publisher) {
    publishLightCore(publisher);
    publishSelectMode(publisher);
    publishMotion(publisher);
    publishRestartButton(publisher);
    publishSensorPrimary(publisher, "temp_c",  "Temperature", "°C",  "temperature");
    publishSensorPrimary(publisher, "hum_pct", "Humidity",    "%",   "humidity");
    publishSensorJson  (publisher, "uptime_s", "Uptime",       "s",   NULL);
    publishSensorJson  (publisher, "rssi",     "WiFi Signal",  "dBm", "signal_strength");
    publishSensorJson  (publisher, "ip",       "IP Address",   NULL,  NULL);
}

}  // namespace ha

// ─────────────────────────────────────────────────────────────────────────────
// Контроллер света
// ─────────────────────────────────────────────────────────────────────────────

class LightController {
public:
    static LightController* _instance;

    void begin(MqttPublisher& pub) {
        _pub = &pub;
        _instance = this;

        pinMode(PIN_LIGHT_MAIN,   OUTPUT);
        pinMode(PIN_LIGHT_EDISON, OUTPUT);
        pinMode(PIN_PIR, INPUT);
        digitalWrite(PIN_LIGHT_MAIN,   LOW);
        digitalWrite(PIN_LIGHT_EDISON, LOW);

        g_state.mode         = MODE_MAIN;
        g_state.lightNow     = LIGHT_OFF;
        g_state.motionArmed  = true;
        s_rearmMs     = 0;

        attachInterrupt(digitalPinToInterrupt(PIN_PIR), onPirStatic, RISING);
        Logger::info(F("light"), F("controller ready"));
    }

    // ──── heartbeat tick ────
    void tick() {
        // Диагностика: состояние таймера и сервиса.
        if (_isOn) {
            unsigned long left = (_timeoutMs == 0) ? 0 : (_timeoutMs - (millis() - _onSinceMs));
            VLOG_I(F("tick"), String(F("isOn=on; fromMotion=")) + (_fromMotion ? F("y") : F("n")) + F("; timeleftMs=") + left);
        }

        // Авто-выключение света.
        if (_isOn && _timeoutMs && (millis() - _onSinceMs >= _timeoutMs)) {
            unsigned long delta = millis() - _onSinceMs;
            VLOG_I(F("tick"), String(F("autooff: elapsedMs=")) + delta + F(" budget=") + _timeoutMs);
            Logger::info(F("light"), F("auto-off"));
            applyAndPublish(LIGHT_OFF);
        }
        // Auto re-arm motion.
        if (!g_state.motionArmed && s_rearmMs && millis() >= s_rearmMs) {
            unsigned long delta = millis() - s_rearmMs;
            VLOG_I(F("tick"), String(F("motionRearm: elapsedMs=")) + delta);
            g_state.motionArmed = true;
            attachInterrupt(digitalPinToInterrupt(PIN_PIR), onPirStatic, RISING);
            publishMotionState();
            Logger::info(F("motion"), F("auto re-armed"));
            s_rearmMs = 0;
        }
    }

    // ──── Публикация текущих значений (через _pub) ────
    void publishLightState()  {
        if (!_pub) return;
        _pub->enqueueLeaf(LEAF_LIGHT, g_state.lightNow, /*retain*/ true);
    }
    void publishModeState() {
        if (!_pub) return;
        _pub->enqueueLeaf(LEAF_MODE, g_state.mode, /*retain*/ true);
    }
    void publishMotionState() {
        if (!_pub) return;
        _pub->enqueueLeaf(LEAF_MOTION,
                          g_state.motionArmed ? String(MOTION_ON) : String(MOTION_OFF),
                          /*retain*/ true);
    }

    // ──── Команды от MQTT ────
    static void onLightSetStatic(const String& payload) {
        VLOG_I(F("mqtt"), String(F("light/set=")) + payload);
        if (!_instance) return;
        if (payload == LIGHT_ON)        _instance->onTurnOn(/*fromMotion*/ false);
        else if (payload == LIGHT_OFF)  _instance->onTurnOff();
        else                            Logger::warn(F("light"), String(F("bad payload: ")) + payload);
    }
    static void onModeSetStatic(const String& payload) {
        VLOG_I(F("mqtt"), String(F("mode/set=")) + payload);
        if (!_instance) return;
        if (payload != MODE_MAIN && payload != MODE_EDISON && payload != MODE_OFF) {
            Logger::warn(F("mode"), String(F("bad payload: ")) + payload);
            return;
        }
        String prev = g_state.mode;
        g_state.applyMode(payload);
        _instance->publishModeState();
        VLOG_I(F("mode"), String(F("apply: ")) + prev + F(" -> ") + g_state.mode);
        if (g_state.mode == MODE_OFF && g_state.lightNow == LIGHT_ON) {
            _instance->onTurnOff();
        }
        Logger::info(F("mode"), String(F("now ")) + g_state.mode);
    }
    static void onMotionSetStatic(const String& payload) {
        VLOG_I(F("mqtt"), String(F("motion/set=")) + payload);
        if (!_instance) return;
        if (payload == MOTION_OFF) {
            if (g_state.motionArmed) {
                g_state.motionArmed = false;
                detachInterrupt(digitalPinToInterrupt(PIN_PIR));
                s_rearmMs = millis() + MOTION_REARM_MS;
                _instance->publishMotionState();
                Logger::info(F("motion"), F("disarmed"));
            }
        } else if (payload == MOTION_ON) {
            if (!g_state.motionArmed) {
                g_state.motionArmed = true;
                attachInterrupt(digitalPinToInterrupt(PIN_PIR), onPirStatic, RISING);
                s_rearmMs = 0;
                _instance->publishMotionState();
                Logger::info(F("motion"), F("armed"));
            }
        } else {
            Logger::warn(F("motion"), String(F("bad payload: ")) + payload);
        }
    }
    static void onRestartSetStatic(const String& payload) {
        VLOG_I(F("mqtt"), String(F("restart/set=")) + payload);
        if (!_instance) return;
        if (payload == RESTART_CMD) {
            Logger::info(F("reset"), F("by mqtt, restarting…"));
            delay(100);
            ESP.restart();
        }
    }

    // ──── PIR (call из основного loop через флаг) ────
    static void IRAM_ATTR onPirStatic() {
        if (_instance) _instance->s_pirFlag = true;
    }

    void servicePir() {
        if (!s_pirFlag) {
            // Не печатаем ничего на каждом тике — пусто — это шум.
            return;
        }
        s_pirFlag = false;
        VLOG_I(F("pir"), String(F("flagged: armed=")) + (g_state.motionArmed ? 'y' : 'n') +
                       F(" mode=") + g_state.mode +
                       F(" isOn=") + (g_state.lightNow == LIGHT_ON ? 'y' : 'n') +
                       F(" fromMotion=") + (_fromMotion ? 'y' : 'n'));

        if (!g_state.motionArmed)        return;
        if (g_state.mode == MODE_OFF)    { VLOG_I(F("pir"), F("ignored (mode=OFF)")); return; }

        // Свет уже горит: если таймер от MQTT — НЕ трогаем, PIR не имеет
        // права уменьшать уже установленный MQTT-таймаут.
        if (_isOn && !_fromMotion) {
            VLOG_I(F("pir"), F("ignored (mqtt-timer held)"));
            Logger::info(F("motion"), F("ignored (mqtt-timer)"));
            return;
        }

        // Либо свет выключен (включаем с motion-таймером), либо свет
        // горит по motion (продлеваем таймер до 10 минут от сейчас).
        _isOn       = true;
        _fromMotion = true;
        _onSinceMs  = millis();
        _timeoutMs  = LIGHT_TIMEOUT_MOTION_MS;
        applyAndPublish(LIGHT_ON);
        VLOG_I(F("pir"), F("on (motion timer 10min)"));
        Logger::info(F("motion"), F("triggered"));
    }

private:
    static unsigned long  s_rearmMs;
    static volatile bool  s_pirFlag;

    MqttPublisher* _pub = nullptr;
    bool           _isOn = false;
    bool           _fromMotion = false;    // true ⇒ таймер motion-side, PIR может продлевать
    unsigned long  _onSinceMs = 0;
    unsigned long  _timeoutMs = 0;

    void onTurnOn(bool fromMotion) {
        _isOn       = true;
        _fromMotion = fromMotion;
        _onSinceMs  = millis();
        _timeoutMs  = fromMotion ? LIGHT_TIMEOUT_MOTION_MS : LIGHT_TIMEOUT_MQTT_MS;
        VLOG_I(F("light"), String(F("onTurnOn: from=")) + (fromMotion ? F("motion") : F("mqtt")) +
                       F(" timeoutMs=") + _timeoutMs);
        applyAndPublish(LIGHT_ON);
    }
    void onTurnOff() {
        VLOG_I(F("light"), String(F("onTurnOff: was-fromMotion=")) + (_fromMotion ? 'y' : 'n'));
        _isOn       = false;
        _fromMotion = false;
        _timeoutMs  = 0;
        applyAndPublish(LIGHT_OFF);
        // Независимо от того, какой источник работал до этого —
        // после выключения mode всегда переходит в MAIN.
        if (g_state.mode != MODE_MAIN) {
            String prev = g_state.mode;
            g_state.applyMode(MODE_MAIN);
            publishModeState();
            VLOG_I(F("mode"), String(F("auto-reset: ")) + prev + F(" -> MAIN"));
        }
    }

    // Общий путь: пины + state + republish.
    void applyAndPublish(const String& target) {
        VLOG_I(F("light"), String(F("apply: ")) + target + F(" mode=") + g_state.mode +
                       F(" main=") + (digitalRead(PIN_LIGHT_MAIN) ? "1" : "0") +
                       F(" edison=") + (digitalRead(PIN_LIGHT_EDISON) ? "1" : "0"));
        g_state.applyLight(target);
        publishLightState();
    }
};

LightController* LightController::_instance = nullptr;
unsigned long  LightController::s_rearmMs   = 0;
volatile bool  LightController::s_pirFlag  = false;

// ─────────────────────────────────────────────────────────────────────────────
// singleton-объекты
// ─────────────────────────────────────────────────────────────────────────────

WifiManager      g_wifi;
MqttPublisher    g_publisher;
MqttClient       g_mqtt;
MqttDispatcher   g_dispatch;
LightController  g_light;

const MqttDispatcher::Entry kDispatchTable[] = {
    { "light/set",       &LightController::onLightSetStatic },
    { "mode/set",        &LightController::onModeSetStatic },
    { "motion/set",      &LightController::onMotionSetStatic },
    { "restart/set",     &LightController::onRestartSetStatic },
};
constexpr size_t kDispatchTableSize = sizeof(kDispatchTable) / sizeof(kDispatchTable[0]);

Ticker tick_;

// ─────────────────────────────────────────────────────────────────────────────
// Тело MqttClient (после полного определения)
// ─────────────────────────────────────────────────────────────────────────────

static MqttDispatcher* g_dispatcher = nullptr;

void MqttClient::begin(MqttDispatcher& dispatch) {
    _dispatch    = &dispatch;
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
    Logger::info(F("mqtt"), F("client ready"));
}

bool MqttClient::doConnect() {
    if (!WiFi.isConnected()) {
        VLOG_I(F("mqtt"), F("try: WiFi not up yet"));
        return false;
    }

    String lwtTopic = String(TOPIC_BASE) + "/" + LEAF_AVAILABILITY;
    VLOG_I(F("mqtt"), String(F("connect: broker=")) + String(MQTT_BROKER) +
                   F(":") + MQTT_PORT + F(" clientId=garage_light"));
    bool ok = _mqtt->connect(
        /*clientId*/  "garage_light",
        /*user*/      MQTT_USER,
        /*pass*/      MQTT_PASSWORD,
        /*willTopic*/ lwtTopic.c_str(),
        /*willQos*/   0,
        /*willRetain*/true,
        /*willMsg*/   LWT_PAYLOAD_OFFLINE,
        /*clean*/     true);
    if (!ok) {
        String s = F("connect FAIL rc="); s += _mqtt->state();
        Logger::warn(F("mqtt"), s);
        VLOG_I(F("mqtt"), s);
        return false;
    }
    Logger::info(F("mqtt"), F("connected"));
    VLOG_I(F("mqtt"), F("connected OK, subscribing…"));

    char buf[64];
    snprintf(buf, sizeof buf, "%s/%s/set", TOPIC_BASE, LEAF_LIGHT);
    VLOG_I(F("mqtt"), String(F("subscribe: ")) + buf);
    _mqtt->subscribe(buf, 0);
    snprintf(buf, sizeof buf, "%s/%s/set", TOPIC_BASE, LEAF_MODE);
    VLOG_I(F("mqtt"), String(F("subscribe: ")) + buf);
    _mqtt->subscribe(buf, 0);
    snprintf(buf, sizeof buf, "%s/%s/set", TOPIC_BASE, LEAF_MOTION);
    VLOG_I(F("mqtt"), String(F("subscribe: ")) + buf);
    _mqtt->subscribe(buf, 0);
    snprintf(buf, sizeof buf, "%s/%s/set", TOPIC_BASE, LEAF_RESTART);
    VLOG_I(F("mqtt"), String(F("subscribe: ")) + buf);
    _mqtt->subscribe(buf, 0);
    Logger::info(F("mqtt"), F("subscribed"));

    // LWT-birth (retain 'online').
    VLOG_I(F("mqtt"), String(F("publish LWT-birth: ")) + lwtTopic + F(" = online (retain)"));
    publishRaw(lwtTopic, LWT_PAYLOAD_ONLINE, true);

    // Retain текущие state-значения.
    g_light.publishLightState();
    g_light.publishModeState();
    g_light.publishMotionState();

    // HA discovery.
    if (CFG_ENABLE_HA_DISCOVERY && !_haDiscoveryPublished) {
        VLOG_I(F("mqtt"), F("publishing HA discovery configs…"));
        ha::publishAll(g_publisher);
        _haDiscoveryPublished = true;
    }

    g_state.connected = true;
    g_state.errorNo   = 0;
    return true;
}

void MqttClient::tick() {
    if (!_mqtt) return;
    _mqtt->loop();

    if (connected()) {
        if (!g_state.connected) {
            g_state.connected = true;
            g_state.errorNo   = 0;
            VLOG_I(F("mqtt"), F("state: online"));
        }
        return;
    }
    if (g_state.connected) {
        g_state.connected = false;
        VLOG_I(F("mqtt"), F("state: offline"));
        Logger::warn(F("mqtt"), F("disconnected"));
    }

    if (!_want)                  return;
    if (millis() < _retryAtMs)   return;

    VLOG_I(F("mqtt"), String(F("retry: errorNo=")) + g_state.errorNo);
    if (doConnect()) { _want = false; return; }

    if (g_state.errorNo < UINT32_MAX) ++g_state.errorNo;
    unsigned long backoff = 1000UL << g_state.errorNo;
    if (backoff > 30000UL) backoff = 30000UL;
    _retryAtMs = millis() + backoff;
    String m = F("retry in ms="); m += backoff;
    Logger::warn(F("mqtt"), m);
}

// Формирует JSON status.
static void publishStatusJson() {
    JsonDocument doc;
    doc["available"]    = g_state.connected;
    doc["uptime_s"]     = (uint32_t)(millis() / 1000UL);
    doc["rssi"]         = g_state.rssi;
    doc["ip"]           = g_state.ip;
    doc["mode"]         = g_state.mode;
    if (CFG_ENABLE_AM2320 && !isnan(g_state.tempC))  doc["temp_c"]  = g_state.tempC;
    if (CFG_ENABLE_AM2320 && !isnan(g_state.humPct)) doc["hum_pct"] = g_state.humPct;

    String out;
    serializeJson(doc, out);
    g_publisher.enqueueLeaf(LEAF_STATUS, out, /*retain*/ true);
}

}  // namespace  // anom

// ─────────────────────────────────────────────────────────────────────────────
// Heartbeat-функция вне anom (для Ticker нужен без захвата).
// ─────────────────────────────────────────────────────────────────────────────

static void heartbeat() {
    ++g_state.count;
    g_light.servicePir();

    if (g_state.count % HEARTBEAT_STATUS_PERIOD == 0) {
        if (CFG_ENABLE_AM2320) {
            float t = NAN, h = NAN;
            if (g_am2320.read(t, h)) {
                g_state.tempC  = t;
                g_state.humPct = h;
            } else {
                Logger::warn(F("am2320"), F("read failed"));
            }
        }
        publishStatusJson();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// setup()/loop()
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Logger::begin();
    Logger::info(F("boot"), F("starting x16 garage light"));

    g_state.ip            = "";
    g_state.rssi          = 0;
    g_state.mode          = MODE_MAIN;
    g_state.lightNow      = LIGHT_OFF;
    g_state.motionArmed   = true;
    g_state.tempC         = NAN;
    g_state.humPct        = NAN;
    g_state.count         = 0;
    g_state.errorNo       = 0;
    g_state.connected     = false;

    if (CFG_ENABLE_AM2320) g_am2320.begin();

    g_wifi.begin(WIFI_SSID, WIFI_PASSWORD);
    g_publisher.begin(g_mqtt);
    g_light.begin(g_publisher);

    g_dispatch.begin(kDispatchTable, kDispatchTableSize);
    g_mqtt.begin(g_dispatch);

    tick_.attach_ms(HEARTBEAT_PERIOD_MS, ::heartbeat);
    Logger::info(F("boot"), F("setup complete"));
}

void loop() {
    g_wifi.tick();
    g_mqtt.tick();
    g_publisher.tick();
    g_light.tick();
    delay(LOOP_TICK_BUDGET_MS);
}
