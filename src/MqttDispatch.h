// src/MqttDispatch.h
// ─────────────────────────────────────────────────────────────────────────────
// Topic → handler dispatch. Mirrors the Lua `mqttanalise.lua` "last
// segment" parser (`string.match(topic, './(%%w+)$')`). Handlers are
// registered at setup() time; in this iteration only `light` is wired.
//
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <Arduino.h>

class MqttDispatch {
public:
    typedef void (*HandlerFn)(const String& payload);

    struct Entry {
        const char* leaf;
        HandlerFn   fn;
    };

    void begin(const Entry* entries, size_t n);
    void dispatch(const String& topic, const String& payload);

private:
    const Entry* _table = nullptr;
    size_t       _count = 0;

    static bool matchLeaf(const String& topic, const char* leaf);
};
