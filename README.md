# x16.Garage\_light.v2 — прошивка на C++ (в разработке)

Прошивка ESP8266 (NodeMCU) на Arduino/PlatformIO для контроллера света в гараже —
переписывание оригинальной прошивки на Lua (`main/*.lua`) на C++.

> **Статус: в разработке.** Целевая структура описана, файлы ещё не созданы.
> Это README — контракт того, что появится в `src/`.

## Что сейчас лежит в репозитории

```
.
├── CLAUDE.md                  # краткое описание проекта для будущих сессий Claude Code
├── README.md                  # этот файл
├── .gitignore                 # исключает CAD-артефакты, secrets.h, сборку PIO
├── platformio.ini              # (пока нет — будет добавлен)
├── docs/
│   └── lua-original/           # Lua-версия, сохраняется как reference
│       ├── init.lua
│       ├── setglobals.lua
│       ├── mqttset.lua
│       ├── mqttget.lua
│       ├── mqttanalise.lua
│       ├── mqttpub.lua
│       ├── main.lua
│       └── check\_air\_temp.lua
└── src/                       # новая прошивка на C++ (будет создана)
```

`docs/lua-original/` — оригинальная NodeMCU-прошивка на Lua, оставляется как
референс на время перехода на C++. **Файлы `.lua` там редактировать не надо** —
они только для сверки при переводе на C++.

## Железо

- **Плата:** ESP8266 NodeMCU v2 (≈80 КБ ОЗУ, доступных приложению).
- **Распиновка** (совпадает с оригинальной Lua-прошивкой):

  | Пин | Функция                                              |
  | --- | ---------------------------------------------------- |
  | D5  | GPIO5 — `pinLightMain` (основное освещение)          |
  | D6  | GPIO6 — `pinLightEdison` (декоративные лампы)        |
  | D7  | GPIO7 — `pinPIR` (датчик движения, прерывание)       |
  | D1  | GPIO2 — I²C SDA → AM2320                             |
  | D2  | GPIO4 — I²C SCL → AM2320                             |

- **AM2320** на шине I²C (SDA=`D1`, SCL=`D2`). Драйвер будит датчик и читает
  регистры 0x0000/0x0001 по даташиту; ошибки уходят в поле `message` JSON,
  а наружу — топик `garage/light/<clntid>/message`.

## Поведение

Прошивка повторяет логику оригинала на Lua:

- На старте: подключение к Wi-Fi (последняя сохранённая сеть), затем к MQTT-брокеру.
- Подписка на управляющие топики, публикация availability (`online`) и
  discovery-конфигов Home Assistant при первом успешном коннекте.
- 1‑Гц таймер публикует диагностику (uptime, heap, RSSI, IP) и раз в минуту
  опрашивает `Am2320::read()`.

### Что реализовано в текущей итерации

В этой первой портирующей итерации сделано:

- ✅ Подключение к Wi-Fi и MQTT с LWT (`…/<clntid>/state`, retain=true).
- ✅ Подписки на `garage/light/light`, `…/lightNow`, `…/lightSelected`,
   `…/lightMoveDetection`, `…/<clntid>/ide`, `…/<clntid>/restart`.
- ✅ Обработка команды `garage/light/light` = `ON|OFF` — через
   `MqttDispatch` → `LightController::onLightCommandStatic` →
   `gpio.write(GPIO5, …)`.
- ✅ Авто-выключение через 600 с (после движения) или 3600 с (после команды
   MQTT).
- ✅ Публикация `<clntid>/heap` и `<clntid>/uptime` каждые 60 тиков
   (диагностика).
- ✅ Legacy plain-топики `garage/light/light`, `…/lightNow` сохраняются.

Что **НЕ** реализовано:

- ❌ Home Assistant discovery — пока legacy plaintext tree.
- ❌ AM2320 (температура/влажность).
- ❌ PIR-прерывание (motion → turn light ON).
- ❌ Select MAIN/EDISON (сейчас только MAIN, GPIO5).
- ❌ HTTP-recovery / OTA-updates.
- ❌ Watchdog.
- ❌ Единый JSON state-topic.

Все эти пункты включаются флагами `CFG_ENABLE_*` в `src/Config.h` — оставляем
их в 0 до следующих итераций.

### Управление светом

- В любой момент активен ровно один источник: **MAIN** (GPIO5) или **EDISON** (GPIO6).
- Переключение режима — через select в Home Assistant
  `select.garage_light_x16_mode` (подписка на тот же логический топик
  `garage/light/lightSelected`, что и в Lua-оригинале).
- Датчик движения включает свет по текущему активному источнику:
  - в режиме MAIN поднимает GPIO5;
  - в режиме EDISON оригинальный код всё равно поднимает GPIO5 («вспышка на движение»).
    C++-порт сохраняет это поведение и публикует событие через поле
    `motion_event` в JSON state-topic.
- Таймер авто-выключения: при включении от датчика движения `lightTimeout = 600 с`;
  при включении по MQTT таймер перекрывается на `3600 с`.
- Длинный grace-таймер (`moveDetectionTimout = 6 870 947 мс ≈ 1 ч 54 м`):
  когда принудительно выключили датчик (`lightMoveDetection=OFF`), PIR снова
  включается через этот интервал.

### Телеметрия

**Один retained JSON state-topic** публикуется на каждом heartbeat, в котором
что-то поменялось:

```
garage/light/<clntid>/state
```

Схема (имена полей совпадают с ключами `value_template` в HA discovery):

```jsonc
{
  "ip":          "10.0.2.123",
  "heap":        12345,
  "uptime":      12345,
  "rssi":        -61,
  "air\_temp":   22.4,        // °C
  "hum":         45.1,        // %
  "light":       "ON",        // зеркало оригинального garage/light/light
  "light\_now":  "ON",        // зеркало garage/light/lightNow
  "mode":        "MAIN",      // MAIN | EDISON
  "motion":      "ON",        // зеркало garage/light/lightMoveDetection (ON = охрана включена)
  "manual\_on":  false,       // true если свет включён по MQTT, не движением
  "message":     ""           // последняя ошибка (очищается на успешной операции)
}
```

`<clntid>` — это `node.chipid()` (как в оригинале).

### Интеграция с Home Assistant

На первом успешном коннекте прошивка публикует retain'ом discovery-конфиги
в следующие топики:

| Топик | Сущность |
| --- | --- |
| `homeassistant/light/garage_light_x16_*/config` | Свет (управление + состояние) |
| `homeassistant/select/garage_light_x16_*/config` | Mode select MAIN/EDISON |
| `homeassistant/binary_sensor/garage_light_x16_*/config` | Состояние датчика движения |
| `homeassistant/sensor/garage_light_x16_*/config` | Температура воздуха |
| `homeassistant/sensor/garage_light_x16_*/config` | Влажность |
| `homeassistant/sensor/garage_light_x16_*/config` | Heap, uptime, RSSI (`entity_category:"diagnostic"`) |

Все сущности принадлежат одному HA-устройству:
`identifiers = [<clntid>]` (chip ID), `name = "Garage Light x16"`,
`sw = "<version проекта>"`.

Availability — отдельный топик
`garage/light/<clntid>/availability` (`online`/`offline`),
LWT — `…/state` со значением `OFFLINE`, чтобы HA показывал устройство
недоступным при потере связи.

## Структура проекта

```
src/
├── main.cpp                    # setup() + loop()
├── Config.h                    # пины, дерево топиков, рантайм-дефолты (без секретов)
├── Secrets.h                   # креды Wi-Fi + MQTT — **НЕ В GIT**
├── Secrets.h\.sample           # шаблон, который коммитим
├── State.{h,cpp}               # структура глобального состояния (бывший `dat` в Lua)
├── WifiManager.{h,cpp}         # авто-коннект / реконнект к Wi-Fi
├── MqttClient.{h,cpp}          # connect/reconnect MQTT, LWT, availability
├── MqttDispatch.{h,cpp}        # входящий топик → таблица хендлеров
├── MqttPublisher.{h,cpp}       # FIFO исходящих публикаций + punow-сериализация
├── HaDiscovery.{h,cpp}         # публикация HA discovery JSON
├── Telemetry.{h,cpp}           # сборка JSON-документа состояния
├── LightController.{h,cpp}     # light(), moveDetected(), таймеры
├── Am2320.{h,cpp}              # I²C-драйвер AM2320 (WAKE + read)
├── HttpRecovery.{h,cpp}        # OTA + HTTP-recovery (флаг rtcmem из Lua сохранён)
└── Logger.{h,cpp}              # обёртка serial-логирования
```

## Конфигурация

### Секреты (`Secrets.h`)

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

`Secrets.h` добавляется в `.gitignore` — в репозитории лежит **только**
`Secrets.h.sample`.

### Дефайны времени компиляции (`Config.h`)

Переключатели поведения без правки логики:

- `CFG_ENABLE_HA_DISCOVERY`     → по умолчанию `1`
- `CFG_ENABLE_AM2320`            → по умолчанию `1`
- `CFG_PUBLISH_LEGACY_TOPICS`    → по умолчанию `1` (публикует оригинальные
  plain-string топики `garage/light/airTemp`, `…/hum`, `…/light`,
  `…/lightNow`, `…/lightSelected`, `…/lightMoveDetection` — для пользователей
  вне Home Assistant).
- `CFG_PUBLISH_TELEMETRY_JSON`   → по умолчанию `1` (один JSON state-topic).
- `CFG_ENABLE_HTTP_RECOVERY`     → по умолчанию `1` (сохраняет поведение Lua:
  rtcmem-флаг срабатывает и из MQTT-сообщения `…/ide`).

## Как собирать / прошивать

Таргет PlatformIO объявлен в `platformio.ini`:

```bash
# 0. Сконфигурировать креды.
cp src/Secrets.h.sample src/Secrets.h
# отредактировать src/Secrets.h, заполнив WIFI_SSID/WIFI_PASSWORD/...

# 1. Сборка (артефакты в .pio/).
pio run

# 2. Прошивка по USB (NodeMCU serial).
pio run -t upload

# 3. Serial-монитор.
pio device monitor
#   → должны появиться:
#      [I][…][boot] starting x16 garage light
#      [I][…][wifi] connecting to '<SSID>'
#      [I][…][wifi] connected, ip=10.0.x.y
#      [I][…][mqtt] connected to broker
#      [I][…][mqtt] subscribed
```

После прошивки проверить, что MQTT-команды работают:

```bash
# Выключено по умолчанию.
mosquitto_sub -h <broker> -t 'garage/light/#' -v &
# Потянуть свет:
mosquitto_pub -h <broker> -t 'garage/light/light' -m ON
# Опустить свет:
mosquitto_pub -h <broker> -t 'garage/light/light' -m OFF
```

После команды ON должны придти retained-сообщения `…/light ON` и
`…/lightNow ON`; реле на GPIO5 — включиться; через 60 минут
(после MQTT-ON) или 10 минут (после движения, сейчас не срабатывает —
PIR ещё не подключён) свет выключится автоматически.

## Что сохраняется из оригинала на Lua

- Распиновка, дерево топиков (`garage/light`), MQTT-клиент на `chipid()`,
  таймеры авто-выключения (600 с / 3600 с override / `moveDetectionTimout = 6870947` мс).
- HA-friendly JSON state **дополняет** оригинальные топики — оба могут сосуществовать.

## Что меняется

- Код на C++17, собирается через PlatformIO / ESP8266 Arduino Core.
- Doxygen-совместимое разделение на модули вместо одного `_G` namespace.
- Все чувствительные константы — в `Secrets.h` (в `.gitignore`),
  `Secrets.h.sample` — единственный шаблон креда в git.
- Логирование — через единый `Logger` вместо ad-hoc `print()`.
- Lua recovery-режим (`rtcmem.write32(0,501)`) сохранён: срабатывает по
  MQTT-команде `…/ide` и поднимает `HttpRecovery`.
- В этой первой итерации актуальный путь к актуальной прошивке — `src/`.
  Lua-исходники переехали в `docs/lua-original/` и не редактируются
  (это reference, не активный код).

## Вне рамок задачи (сейчас)

- Проверка целостности прошивки на первом старте (планируется в `HttpRecovery::loop`).
- TLS для MQTT (в Lua брокер в доверенной LAN).

## Открытые вопросы / TODO перед началом кода

Это не вошло в ответы выше и остаётся открытым — задокументировано, чтобы
вернуться на этапе реализации:

1. **Поведение датчика движения в режиме EDISON:** оставляем как в Lua
   (PIR поднимает MAIN даже когда режим EDISON — «вспышка на движение»),
   или меняем на «PIR триггерит только активный источник»? Дефолт в README —
   сохраняем Lua-поведение.
2. **Watchdog:** включать ли аппаратный watchdog ESP и перезагружать на зависании.
   Дефолт — да, после `k` секундный таймаут.
3. **Размер discovery payload:** если payload для light-entity превысит
   512 Б (не должен, ожидается ~480), публиковать облегчённую версию
   (без `effect_list` и т. п.).
