// src/MqttPublisher.cpp

#include "MqttPublisher.h"
#include "MqttClient.h"
#include "State.h"
#include "Config.h"

namespace {
// Static queue — we hold at most one item in flight plus any pending
// arrivals. Sized tight on purpose: ESP8266 RAM budget.
constexpr size_t QUEUE_CAPACITY = 16;
Item   s_queue[QUEUE_CAPACITY];
size_t s_qHead = 0;     // index of oldest
size_t s_qTail = 0;     // index of next-slot
size_t s_qSize = 0;

bool enqueueSlot(const Item& item) {
    if (s_qSize >= QUEUE_CAPACITY) {
        // Drop oldest — equivalent to Lua's table.remove rolling
        // behaviour. Counterpart: we want recent telemetry to win
        // diagnostics over stale state when buffer pressure is high.
        s_qHead = (s_qHead + 1) % QUEUE_CAPACITY;
        s_qSize--;
    }
    s_queue[s_qTail] = item;
    s_qTail = (s_qTail + 1) % QUEUE_CAPACITY;
    ++s_qSize;
    return true;
}

bool dequeueSlot(Item& out) {
    if (s_qSize == 0) return false;
    out = s_queue[s_qHead];
    s_qHead = (s_qHead + 1) % QUEUE_CAPACITY;
    --s_qSize;
    return true;
}
}  // namespace

void MqttPublisher::begin(MqttClient& mqtt) {
    _mqtt = &mqtt;
    s_qHead = s_qTail = s_qSize = 0;
    _headInFlight = false;
}

void MqttPublisher::enqueue(const String& leaf, const String& payload,
                            uint8_t qos, bool retain) {
    Item it;
    it.leaf    = leaf;
    it.payload = payload;
    it.qos     = qos;
    it.retain  = retain;
    enqueueSlot(it);
}

void MqttPublisher::tick() {
    if (!_mqtt) return;

    // In PubSubClient world, a publish call is queued into TCP buffers
    // synchronously; only one write should be outstanding per tick.
    if (_headInFlight) {
        _headInFlight = false;
        // Allow PubSubClient's loop() (driven by MqttClient::tick()) to
        // drain any ack before we send the next packet. We simply clear
        // the flag here. (Equivalent to Lua `punow()` callback tail.)
    }

    // Take the next item.
    Item next;
    if (!dequeueSlot(next)) return;
    if (!_mqtt->connected()) {
        // No broker — put it back at the head and wait.
        enqueueSlot(next);
        return;
    }

    String topic = String(g_state.topic) + "/" + next.leaf;
    bool ok = _mqtt->publishConnected(
        topic, next.payload, next.qos, next.retain);

    if (ok) {
        _head = next;
        _headInFlight = true;
    } else {
        // Publish failed (likely overflow of ESP8266 TX buffer) — requeue.
        enqueueSlot(next);
    }
}

void MqttPublisher::releaseHead() {
    _headInFlight = false;
}
