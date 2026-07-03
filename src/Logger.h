// src/Logger.h
// ─────────────────────────────────────────────────────────────────────────────
// Serial logger with a unified prefix. Replaces the ad-hoc `print()` calls
// scattered through the original Lua firmware.
//
// Usage:
//     Logger::begin();
//
//     Logger::info(F("boot"), F("power on"));
//     Logger::warn(F("wifi"), F("disconnected, retry"));
//     Logger::error(F("mqtt"), F("connect failed"), code);
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <Arduino.h>

class Logger {
public:
    static void begin(unsigned long baud = 115200);
    static bool ready();

    // Helper: emit `[I][<uptimeMs>][<module>] <message>`.
    static void info (const __FlashStringHelper* module, const __FlashStringHelper* msg);
    static void warn (const __FlashStringHelper* module, const __FlashStringHelper* msg);
    static void error(const __FlashStringHelper* module, const __FlashStringHelper* msg);

    // String overloads — accept Arduino String in RAM, no flash wrapping.
    static void info (const __FlashStringHelper* module, const String& msg);
    static void warn (const __FlashStringHelper* module, const String& msg);
    static void error(const __FlashStringHelper* module, const String& msg);

private:
    static bool _ready;
    static void emit(char levelChar,
                     const __FlashStringHelper* module,
                     const String& payload);
};
