# x16.Garage\_light.v2 — прошивка на C++ (в разработке)

ESP8266 (NodeMCU) на Arduino/PlatformIO. Контроллер света в гараже:
основное освещение (Main, GPIO5), декоративные Edison (GPIO6),
датчик движения (PIR на GPIO7), температура/влажность (AM2320 по I²C).
Управляется через MQTT с интеграцией Home Assistant.

> **Структура MQTT пересмотрена** — старая Lua-плоскость (`dat.light`,
> `dat.lightMoveDetection`, …) больше не применяется как спецификация. Дерево
> проектировалось под Home Assistant: один корень `garage/light/…`, единый
> JSON status под `…/status`, LWT/availability отдельным топиком,
> команды в виде **`<thing>/{set,state}`** — стандартное HA-соглашение.

## Что в репозитории

```
.
├── CLAUDE.md                  # краткое описание проекта
├── README.md                  # этот файл
├── .gitignore                 # CAD-артефакты, .pio/, src/secrets.h
├── platformio.ini             # target env:nodemcu
├── docs/
│   └── lua-original/          # оригинал на Lua — архивный reference
│       └── *.lua
└── src/                       # прошивка на C++ (вся в одном файле)
    ├── main.cpp               # setup()/loop() + все классы
    ├── Config.h               # компиле-тайм флаги, пины, имена топиков
    ├── Secrets.h.sample       # шаблон кредами — лежит в git
    └── Secrets.h              # копия с реальными данными — **НЕ в git**
```

## Железо

- **Плата:** ESP8266 NodeMCU v2 (≈80 КБ ОЗУ, доступных приложению).
- **Распиновка:**

  | Пин | Функция                                          |
  | --- | ------------------------------------------------ |
  | D5  | GPIO5 — `pinLightMain` (основное освещение)      |
  | D6  | GPIO6 — `pinLightEdison` (декоративные Edison)    |
  | D7  | GPIO7 — `pinPIR` (датчик движения, прерывание)   |
  | D1  | GPIO2 — I²C SDA → AM2320                         |
  | D2  | GPIO4 — I²C SCL → AM2320                         |

- **AM2320** подключён к I²C. Драйвер будит датчик и читает регистры
  0x0000/0x0001 по даташиту; ошибки уходят в поле `message` JSON.

## MQTT: дерево топиков

Корень — `garage/light`. Один комплект в гараже — `clntid` в топиках
**не используется**, всё под корнем напрямую. Всё управление идёт через пары
`<leaf>/{set,state}` в стиле Home Assistant: подписаться на `…/<leaf>/set`
для приёма команд, опубликовать состояние в `…/<leaf>/state`.

### Управление

| Топик                            | Тип                    | Описание                                                |
| -------------------------------- | ---------------------- | ------------------------------------------------------- |
| `garage/light/light/set`         | in (sub)               | Команда `ON` / `OFF` — включить/выключить активный источник. Какой именно — определяется текущим `mode`. |
| `garage/light/light/state`       | out (retained)         | Текущее состояние `ON` / `OFF`.                         |
| `garage/light/mode/set`          | in (sub)               | Выбор источника для следующего включения: `MAIN` / `EDISON` / `OFF` (`OFF` — оба выключены и «никакой следующей команды не включает»). |
| `garage/light/mode/state`        | out (retained)         | Текущий режим (тот же набор значений).                  |
| `garage/light/motion/set`        | in (sub)               | `ON` / `OFF` — вкл/выкл режим авто-включения от PIR.   |
| `garage/light/motion/state`      | out (retained)         | Текущее состояние `ON` / `OFF`.                          |
| `garage/light/restart/set`       | in (sub)               | Команда `RESTART` или `PRESS` — перезагрузка МК.        |

### Телеметрия

| Топик                            | Тип               | Описание                                                  |
| -------------------------------- | ----------------- | --------------------------------------------------------- |
| `garage/light/status`            | out (retained, JSON) | Сводка состояния публикуется раз в минуту и при изменениях. Подробный формат ниже. |
| `garage/light/availability`      | out (retained)   | `online` / `offline`. LWT-birth публикует `online` с retain при коннекте; LWT публикует `offline` если брокер потерял связь. |

### Формат `…/status` (JSON)

```json
{
  "available": true,
  "uptime_s":  12345,
  "rssi":     -61,
  "ip":       "10.0.2.123",
  "mode":     "MAIN",
  "temp_c":    22.4,
  "hum_pct":   45.1
}
```

Что **не** включено и почему:

- `ts` (unix-time) — убран, потому что `uptime_s` уже фиксирует порядок событий.
- `heap` — исключён: на ESP8266 после `wifi.begin` диагностика фрагментации
  памяти даёт шумные цифры; пользователю интереснее `uptime` и `rssi`.
- `light_main`/`light_edison` как отдельные поля — убраны: один источник
  света управляется через `…/light/{set,state}`, и `mode` уже говорит, какой.

### Дерево не содержит `<clntid>`

Оставляем плоское дерево в корне `garage/light/…` — в текущей инсталляции
один МК в гараже. Если в будущем появится второе устройство, имеет смысл
обратно перейти к `garage/light/<id>/…`, но тогда нужно задуматься и про
HA-`unique_id`, и про миграцию retained-топиков — это отдельный этап.

## Поведение

- **Старт.** Подключаемся к Wi-Fi сети из `Secrets.h`, затем к MQTT-брокеру.
  На первом коннекте публикуем retain: discovery-конфиги HA, `…/availability`
  = `online`, `…/light/state`, `…/mode/state`, `…/motion/state` — всё текущее
  состояние.
- **Heartbeat (1 Гц).** Внутри обновляет счётчик. Каждые 60 тиков (≈раз в минуту)
  пересобирает JSON `…/status` и публикует.
- **PIR (HC-SR501 на GPIO7).** Прерывание по фронту `RISING`. PIR триггерит
  **активный** источник: если `mode = MAIN`, поднимает Main; если `mode =
  EDISON`, поднимает Edison. Если `mode = OFF` или motion_disarmed, PIR
  ничего не делает.
  Семантика таймера у PIR:
  - если свет выключен → PIR включает его с таймером `LIGHT_TIMEOUT_MOTION_MS
    = 600 000` (10 минут);
  - если свет уже горит и активный таймер — тоже motion‑источник → PIR
    продлевает таймер до 10 минут от события (стандартное поведение детектора);
  - если свет уже горит и активный таймер — `MQTT`‑источник → PIR event
    **игнорируется**. PIR не имеет права уменьшать таймер, установленный
    вручную через MQTT.
- **MQTT-команда `…/light/set = ON`.** Поднимает текущий источник по `mode`,
  таймер `LIGHT_TIMEOUT_MQTT_MS = 3 600 000` (1 час). PIR затем не меняет
  этот таймер (см. выше).
- **Авто-выключение.** Когда сработал таймер (motion-10 мин или mqtt-1 час),
  оба GPIO выключаются, `…/light/state` = `OFF`.
- **После выключения происходит две вещи:** `mode` всегда сбрасывается на
  `MAIN` (вне зависимости от того, какой источник работал и по какой причине
  произошло выключение — MQTT-OFF, авто-таймаут или `mode=OFF` через селектор);
  внутренний флаг `_fromMotion` сбрасывается — следующий PIR-триггер снова
  назначит10-минутный таймер, как и должно быть.
- **RESTART.** По `…/restart/set` → `ESP.restart()`.

## Home Assistant — discovery

На первом MQTT-коннекте публикуем **retain** следующие config-топики
HA в стандартной форме `homeassistant/<platform>/garage_light_<entity>/config`:

| Топик discovery                                              | Сущность                 | Источник данных                                  |
| ------------------------------------------------------------ | ------------------------ | ------------------------------------------------ |
| `homeassistant/light/garage_light/config`                    | `light.garage_light`     | `…/light/{state,set}`                            |
| `homeassistant/select/garage_light_mode/config`              | `select.garage_light_mode` | `…/mode/{state,set}`, options = `[MAIN, EDISON, OFF]` |
| `homeassistant/binary_sensor/garage_light_motion/config`     | `binary_sensor.garage_light_motion` | `…/motion/state`, `device_class:motion` |
| `homeassistant/button/garage_light_restart/config`           | `button.garage_light_restart`    | `…/restart/set`, payload `RESTART`        |
| `homeassistant/sensor/garage_light_temperature/config`       | `sensor.garage_light_temperature` | `…/status`, `value_template: "{{ value_json.temp_c }}"` |
| `homeassistant/sensor/garage_light_humidity/config`          | `sensor.garage_light_humidity`    | `…/status`, `value_template: "{{ value_json.hum_pct }}"` |

…и три диагностических сенсора с `entity_category: diagnostic`:

| Топик discovery                                              | Сущность                          | value_template                          |
| ------------------------------------------------------------ | --------------------------------- | --------------------------------------- |
| `homeassistant/sensor/garage_light_uptime/config`            | `sensor.garage_light_uptime`      | `{{ value_json.uptime_s }}`            |
| `homeassistant/sensor/garage_light_rssi/config`              | `sensor.garage_light_rssi`        | `{{ value_json.rssi }}` (`device_class: signal_strength`) |
| `homeassistant/sensor/garage_light_ip/config`                | `sensor.garage_light_ip`          | `{{ value_json.ip }}`                  |

Все сущности описаны через один HA-`device`:

```jsonc
"device": {
  "identifiers": ["garage_light_x1_6"],   // стабильный unique-id по MKT + модели
  "name":       "Garage Light x16",
  "model":      "esp8266-nodemcu",
  "sw_version": "<semver проекта>"
}
```

`availability_topic` для всех entity — `garage/light/availability`,
`payload_available = "online"`, `payload_not_available = "offline"`.
Это позволяет HA показывать устройство как «недоступно» при потере
связи с брокером (PubSubClient LWT публикует `offline` по таймауту
keepalive).

### Как это ведёт себя в Lovelace

- Карточка `light.garage_light` — главная лампа. Кнопка вкл/выкл, иконка
  меняется в зависимости от `…/light/state`.
- Рядом select для режима — пользователь переключает между MAIN/EDISON/OFF.
- В карточке настроек — binary_sensor `motion` (включена ли PIR-охрана),
  диагностические сенсоры (uptime/RSSI/IP) разворачиваются в development‑секции.
- Кнопка `button.garage_light_restart` — в development‑секции.

## Конфигурация

### Secrets (`src/Secrets.h`)

Шаблон коммитится как `src/Secrets.h.sample`. Копируем в `src/Secrets.h`
и заполняем:

```cpp
#define WIFI_SSID      "your-ssid"
#define WIFI_PASSWORD  "your-wifi-password"
#define MQTT_USER      "your-mqtt-user"
#define MQTT_PASSWORD  "your-mqtt-password"
#define MQTT_BROKER    "10.0.2.1"   // можно оставить как дефолт
#define MQTT_PORT      1883
```

`Secrets.h` в `.gitignore` — в репозитории только sample.

### Дефайны времени компиляции (`src/Config.h`)

Дефолты рассчитаны на новую структуру:

- `CFG_ENABLE_HA_DISCOVERY`    → по умолчанию `1` — discovery публикуется при первом коннекте.
- `CFG_ENABLE_AM2320`           → по умолчанию `1` — сенсор температуры/влажности.
- `CFG_PUBLISH_STATUS_JSON`     → по умолчанию `1` — JSON `…/status` раз в минуту.
- `CFG_VERBOSE_LOG`             → по умолчанию `1` — подробные логи в serial: PIR-триггер,
   путь MQTT-команд, таймер авто-выключения, этапы коннекта к брокеру. Поставить
   `0` — останутся только warn/error и сэкономим ~3 КБ flash.
- `CFG_PUBLISH_LEGACY_TOPICS`   → по умолчанию `0` — старые Lua-топики (`airTemp`,
   `hum`, `lightSelected`, …) НЕ публикуются. Эта ситуация — отказ от
   legacy-формата.

## Как собирать / прошивать

```bash
# 0. Сконфигурировать креды.
cp src/Secrets.h.sample src/Secrets.h
# отредактировать src/Secrets.h: WIFI_SSID / WIFI_PASSWORD / MQTT_USER / …

# 1. Сборка (артефакты в .pio/).
pio run

# 2. Прошивка по USB (NodeMCU serial).
pio run -t upload

# 3. Serial-монитор.
pio device monitor
#   должны появиться:
#      [I][…][boot] starting x16 garage light
#      [I][…][wifi] connecting to '<SSID>'
#      [I][…][wifi] connected, ip=10.0.x.y
#      [I][…][mqtt] connected to broker
#      [I][…][mqtt] subscribed
```

После прошивки проверить взаимодействие вручную:

```bash
# Включаемся с режимом MAIN, ON.
mosquitto_pub -h <broker> -t 'garage/light/mode/set'  -m MAIN
mosquitto_pub -h <broker> -t 'garage/light/light/set' -m ON

# Видим retained:
# garage/light/light/state ON
# garage/light/mode/state MAIN
# garage/light/status {"available":true, ...}

# Переключаемся на Edison и оставим ON.
mosquitto_pub -h <broker> -t 'garage/light/mode/set'  -m EDISON
mosquitto_pub -h <broker> -t 'garage/light/light/set' -m ON
# → GPIO6 (Edison) поднимается, GPIO5 (Main) опускается.

# Выключаем всё.
mosquitto_pub -h <broker> -t 'garage/light/light/set' -m OFF

# В Home Assistant: добавляем MQTT integration, он найдёт
# все entity по discovery config-топикам автоматически.
```

## Что осталось за рамками этой итерации

- **Аппаратный watchdog** — перезагрузка ESP при зависании (`ESP.wdtEnable`).
  Сейчас дефолт без него: при обнаружении зависания — отладка вручную.
  Внутри `src/main.cpp` место для интеграции подготовлено — функция-триггер
  в `setup()`.

## Открытые вопросы / TODO

1. **Имя устройства и `manufacturer` в HA discovery** — оставлено
   «Garage Light x16». Если вы используете другой вендор — расскажите,
   заполню.
2. **PIR→active source** vs **PIR→Main always**. Принято второе
   (то есть PIR теперь поднимает тот источник, который сейчас в `mode`).
   Это упрощает UX. Если в вашей инсталляции PIR должен поднимать только
   Main — скажите, переключим.
3. **Идентификация** — `device.identifiers = ["garage_light_x1_6"]`
   жёстко выкован. Если у вас несколько гаражей или несколько таких плат —
   переключаемся на `chipid` динамически и добавляем миграцию retained.
