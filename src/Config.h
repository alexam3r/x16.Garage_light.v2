// src/Config.h
// ─────────────────────────────────────────────────────────────────────────────
// Compile-time конфигурация проекта. Без секретов: пароли в src/Secrets.h.
// Все define-флаги здесь — это Compile-Time-выключатели. Поведение по
// умолчанию отражает согласованную v2-структуру MQTT (см. README.md):
//
//   - плоское дерево garage/light/<leaf> без <clntid>;
//   - JSON status под garage/light/status раз в минуту и при изменениях;
//   - retain availability под garage/light/availability;
//   - HA discovery через homeassistant/.../config retain-конфиги;
//   - управление в виде стандартных пар <leaf>/{set,state}.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time switches
// ─────────────────────────────────────────────────────────────────────────────

// Включить рекламу HA discovery при первом MQTT-коннекте (6 основных
// сущностей + 3 diagnostic сенсора).
#ifndef CFG_ENABLE_HA_DISCOVERY
#define CFG_ENABLE_HA_DISCOVERY       1
#endif

// Подключить AM2320 (I²C) для температуры и влажности. Если 1 —
// добавляем зависимость и опрашиваем датчик перед публикацией JSON.
#ifndef CFG_ENABLE_AM2320
#define CFG_ENABLE_AM2320             1
#endif

// Публиковать единый JSON в garage/light/status (раз в минуту + по
// изменениям состояния).
#ifndef CFG_PUBLISH_STATUS_JSON
#define CFG_PUBLISH_STATUS_JSON       1
#endif

// HTTP-recovery и OTA. По умолчанию выключено — следующая итерация.
#ifndef CFG_ENABLE_HTTP_RECOVERY
#define CFG_ENABLE_HTTP_RECOVERY      0
#endif

// Legacy Lua-топики (airTemp/hum/light/lightNow/lightSelected/lightMoveDetection)
// больше НЕ публикуются — отказались от них при переходе на новый layout.
#ifndef CFG_PUBLISH_LEGACY_TOPICS
#define CFG_PUBLISH_LEGACY_TOPICS     0
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Пины
// ─────────────────────────────────────────────────────────────────────────────
#define PIN_LIGHT_MAIN       5   // D5 — основное освещение
#define PIN_LIGHT_EDISON     6   // D6 — Edison лампы
#define PIN_PIR              7   // D7 — датчик движения (прерывание)

// ─────────────────────────────────────────────────────────────────────────────
// Тайминги
// ─────────────────────────────────────────────────────────────────────────────

// Таймаут авто-выключения после детектирования движения.
#define LIGHT_TIMEOUT_MOTION_MS       600000UL

// Таймаут авто-выключения после команды MQTT ON.
#define LIGHT_TIMEOUT_MQTT_MS         3600000UL

// Длинный grace-таймер для PIR. Когда motion был выключен с MQTT, через
// столько мс авто-возвращаемся в armed. Значение из оригинальной
// Lua-прошивки (moveDetectionTimout = 6870947).
#define MOTION_REARM_MS              6870947UL

// 1 Гц heartbeat. Период в мс.
#define HEARTBEAT_PERIOD_MS           1000UL

// Что планируется в heartbeat:
//   - каждые 60 тиков публикуем garage/light/status.
#define HEARTBEAT_STATUS_PERIOD       60UL
//   - по изменениям mode/light/motion публикуем targeted state-топики
//     сразу, не дожидаясь heartbeat.

// Tick budget в loop — отдаём время Wi-Fi.
#define LOOP_TICK_BUDGET_MS           2UL

// ─────────────────────────────────────────────────────────────────────────────
// MQTT: корень и leafы (плоское дерево)
// ─────────────────────────────────────────────────────────────────────────────
// Topic base (без clntid).
static const char TOPIC_BASE[]        = "garage/light";

// Leafs обмена (всё в плоском дереве; пары /<leaf>/{set,state} для HA).
static const char LEAF_LIGHT[]        = "light";
static const char LEAF_MODE[]         = "mode";
static const char LEAF_MOTION[]       = "motion";
static const char LEAF_RESTART[]      = "restart";

// Топики телеметрии и LWT.
static const char LEAF_STATUS[]       = "status";        // JSON
static const char LEAF_AVAILABILITY[] = "availability";  // LWT-birth + retain

// ─────────────────────────────────────────────────────────────────────────────
// MQTT connection parameters (не-секреты)
// ─────────────────────────────────────────────────────────────────────────────
#define MQTT_KEEPALIVE_S              60
#define MQTT_DEFAULT_PORT             1883

// LWT (Last Will and Testament) — что публикует брокер, если МК
// не отвечает на keepalive. Используется как availability в HA:
// пара значений online/offline строго одна.
static const char LWT_PAYLOAD_ONLINE[]  = "online";
static const char LWT_PAYLOAD_OFFLINE[] = "offline";

// Состояния источников света и режимов.
static const char LIGHT_ON[]   = "ON";
static const char LIGHT_OFF[]  = "OFF";

static const char MODE_MAIN[]   = "MAIN";
static const char MODE_EDISON[] = "EDISON";
static const char MODE_OFF[]    = "OFF";   // оба выключены, никакой не активен

static const char MOTION_ON[]   = "ON";
static const char MOTION_OFF[]  = "OFF";

static const char RESTART_CMD[] = "RESTART";

// ─────────────────────────────────────────────────────────────────────────────
// HA discovery configuration
// ─────────────────────────────────────────────────────────────────────────────
static const char HA_DISCOVERY_PREFIX[]   = "homeassistant";

// Идентификатор устройства в HA — стабильный, чтобы HA не создавал новые
// entity при каждом реконнекте. Если у вас несколько МК такого типа —
// переключаемся на ESP.getChipId().
static const char DEVICE_ID[]             = "garage_light_x1_6";
static const char DEVICE_NAME[]           = "Garage Light x16";
static const char DEVICE_MODEL[]          = "esp8266-nodemcu";
static const char DEVICE_MANUFACTURER[]    = "DIY";
static const char DEVICE_SW_VERSION[]     = "2.0.0";

// Префикс для конфиг-топика discovery —
//   <HA_DISCOVERY_PREFIX>/<platform>/<unique_id_with_device_id>/config
#define HA_ENTITY_ID(id) (String(HA_DISCOVERY_PREFIX) + "/" + (id))
