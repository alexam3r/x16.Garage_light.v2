// src/MqttPublisher.h
// ─────────────────────────────────────────────────────────────────────────────
// FIFO outgoing-publication queue. Mirrors the Lua `topub` table drained
// by `mqttpub.lua`. Used to serialize publish() calls so they keep order
// over the single MQTT socket.
//
// Topic is built as `garage/light/<leaf>` (Lua `dat.topic .. '/' .. tp[1]`).
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <Arduino.h>

class MqttClient;  // forward

class MqttPublisher {
public:
    void begin(MqttClient& mqtt);

    // Enqueue an outgoing publish. Topic = `garage/light/<leaf>`.
    void enqueue(const String& leaf, const String& payload,
                 uint8_t qos = 0, bool retain = false);

    void tick();

private:
    struct Item {
        String leaf;
        String payload;
        uint8_t qos;
        bool   retain;
    };

    // Ring-buffer is owned by the class instead of a TU-static so the
    // declaration order between `Item` and `s_queue[]` is unambiguous.
    static constexpr size_t QUEUE_CAPACITY = 16;
    Item        _q[QUEUE_CAPACITY];
    size_t      _qHead = 0;     // index of oldest
    size_t      _qTail = 0;     // index of next-slot
    size_t      _qSize = 0;

    MqttClient* _mqtt = nullptr;
    bool        _headInFlight = false;
    Item        _head;

    void releaseHead();
};
