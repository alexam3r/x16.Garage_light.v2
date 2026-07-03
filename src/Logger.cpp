// src/Logger.cpp

#include "Logger.h"

bool Logger::_ready = false;

bool Logger::ready() { return _ready; }

void Logger::begin(unsigned long baud) {
    Serial.begin(baud);
    // Give the boot ROM a few ms to attach the USB-CDC driver before we
    // declare ourselves "ready".
    delay(50);
    _ready = true;
}

void Logger::emit(char levelChar,
                  const __FlashStringHelper* module,
                  const String& payload) {
    if (!_ready) return;
    unsigned long ms = millis();
    Serial.print('[');
    Serial.print(levelChar);
    Serial.print(F("]["));
    Serial.print(ms);
    Serial.print(F("]["));
#if defined(ARDUINO_ESP8266_RELEASE_2_x) || defined(ESP8266)
    // The ESP8266 Arduino core ships `Serial.print()` overloads that
    // accept a `const __FlashStringHelper*` directly and read from flash
    // (PROGMEM). This avoids copying "module" into RAM.
    Serial.print(module);
#else
    Serial.print((const char*)module);
#endif
    Serial.print(F("] "));
    Serial.println(payload);
}

void Logger::info (const __FlashStringHelper* m, const __FlashStringHelper* s) {
    String payload(s);
    emit('I', m, payload);
}
void Logger::warn (const __FlashStringHelper* m, const __FlashStringHelper* s) {
    String payload(s);
    emit('W', m, payload);
}
void Logger::error(const __FlashStringHelper* m, const __FlashStringHelper* s) {
    String payload(s);
    emit('E', m, payload);
}

void Logger::info (const __FlashStringHelper* m, const String& s)        { emit('I', m, s); }
void Logger::warn (const __FlashStringHelper* m, const String& s)        { emit('W', m, s); }
void Logger::error(const __FlashStringHelper* m, const String& s)        { emit('E', m, s); }
