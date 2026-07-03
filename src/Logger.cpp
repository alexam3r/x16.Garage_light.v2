// src/Logger.cpp

#include "Logger.h"

bool Logger::_ready = false;

bool Logger::ready() { return _ready; }

void Logger::begin(unsigned long baud) {
    Serial.begin(baud);
    // Serial may take a moment — give the ESP8266 boot ROM a few ms to
    // attach the USB-CDC driver before we declare ourselves "ready".
    delay(50);
    _ready = true;
}

void Logger::emit(char levelChar, const char* module, const String& payload) {
    if (!_ready) return;
    unsigned long ms = millis();
    Serial.print('[');
    Serial.print(levelChar);
    Serial.print(F("]["));
    Serial.print(ms);
    Serial.print(F("]["));
    Serial.print(module);
    Serial.print(F("] "));
    Serial.println(payload);
}

void Logger::info (const __FlashStringHelper* m, const __FlashStringHelper* s) { emit('I', FPSTR(m), String(s)); }
void Logger::warn (const __FlashStringHelper* m, const __FlashStringHelper* s) { emit('W', FPSTR(m), String(s)); }
void Logger::error(const __FlashStringHelper* m, const __FlashStringHelper* s) { emit('E', FPSTR(m), String(s)); }

void Logger::info (const __FlashStringHelper* m, const String& s) { emit('I', FPSTR(m), s); }
void Logger::warn (const __FlashStringHelper* m, const String& s) { emit('W', FPSTR(m), s); }
void Logger::error(const __FlashStringHelper* m, const String& s) { emit('E', FPSTR(m), s); }
