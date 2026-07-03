// src/MqttPublisher.cpp

#include "MqttPublisher.h"
#include "MqttClient.h"
#include "State.h"
#include "Config.h"
#include "Logger.h"

void MqttPublisher::begin(MqttClient& mqtt) {
    _mqtt = &mqtt;
    _qHead = _qTail = _qSize = 0;
    _headInFlight = false;
    _head = Item{};
}

void MqttPublisher::enqueue(const String& leaf, const String& payload,
                            uint8_t qos, bool retain) {
    // Drop oldest on overflow — equivalent to a rolling log; recent
    // telemetry beats stale state when buffer pressure is high.
    if (_qSize >= QUEUE_CAPACITY) {
        _qHead = (_qHead + 1) % QUEUE_CAPACITY;
        --_qSize;
        Logger::warn(F("pub"), F("queue full, dropping oldest"));
    }
    Item it;
    it.leaf    = leaf;
    it.payload = payload;
    it.qos     = qos;
    it.retain  = retain;
    _q[_qTail] = it;
    _qTail = (_qTail + 1) % QUEUE_CAPACITY;
    ++_qSize;
}

void MqttPublisher::tick() {
    if (!_mqtt) return;

    // In PubSubClient world a publish call is queued into TCP buffers
    // synchronously; only one write is "in flight" per tick. The flag is
    // dropped on the next tick, after PubSubClient::loop() (driven by
    // MqttClient::tick() one step earlier) has had its chance to drain.
    if (_headInFlight) {
        _headInFlight = false;
    }

    if (_qSize == 0) return;

    // Take the next item.
    Item next = _q[_qHead];
    _qHead = (_qHead + 1) % QUEUE_CAPACITY;
    --_qSize;

    if (!_mqtt->connected()) {
        // No broker — put it back at the head and wait.
        _qHead = (_qHead == 0) ? QUEUE_CAPACITY - 1 : _qHead - 1;
        ++_qSize;
        return;
    }

    String topic = String(g_state.topic) + "/" + next.leaf;
    bool ok = _mqtt->publishConnected(
        topic, next.payload, next.qos, next.retain);

    if (ok) {
        _head = next;
        _headInFlight = true;
    } else {
        // Publish failed (likely TCP TX buffer full) — requeue at the head.
        _qHead = (_qHead == 0) ? QUEUE_CAPACITY - 1 : _qHead - 1;
        ++_qSize;
    }
}

void MqttPublisher::releaseHead() {
    _headInFlight = false;
}
