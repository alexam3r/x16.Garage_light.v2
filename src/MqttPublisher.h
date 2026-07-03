// src/MqttPublisher.h
// ─────────────────────────────────────────────────────────────────────────────
// FIFO outgoing-publication queue. Mirrors the Lua `topub` table drained
// by `mqttpub.lua`. Used to serialize publish() calls so they keep order
// over the single MQTT socket.
//
// Per publish we attach an optional `isJson` flag — kept for the
// iteration where we publish the state JSON to the per-device state
// topic. Topic is built as `garage/light/<leaf>` (Lua `dat.topic .. '/' ..
// tp[1]`).
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

    MqttClient* _mqtt = nullptr;
    Item        _head;          // holds the packet currently being sent
    bool        _headInFlight = false;

    void releaseHead();
};
