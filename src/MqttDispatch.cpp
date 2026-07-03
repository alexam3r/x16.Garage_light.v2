// src/MqttDispatch.cpp

#include "MqttDispatch.h"
#include "Logger.h"

void MqttDispatch::begin(const Entry* entries, size_t n) {
    _table = entries;
    _count = n;
}

void MqttDispatch::dispatch(const String& topic, const String& payload) {
    if (!_table) return;

    for (size_t i = 0; i < _count; ++i) {
        const Entry& e = _table[i];
        if (!e.fn || !e.leaf) continue;
        if (matchLeaf(topic, e.leaf)) {
            e.fn(payload);
            return;
        }
    }

    // No match — log a hint so the developer can wire a new handler.
    String s = F("unhandled topic leaf, topic=");
    s += topic;
    Logger::warn(F("dispatch"), s);
}

bool MqttDispatch::matchLeaf(const String& topic, const char* leaf) {
    // Equivalent of Lua `string.match(topic, "./(%w+)$")`:
    //   topic ends with "/" + leaf.
    if (!leaf || !*leaf) return false;
    size_t tlen = topic.length();
    size_t llen = strlen(leaf);
    if (tlen < llen + 1) return false;
    if (topic[tlen - llen - 1] != '/') return false;
    // Compare the suffix without allocating intermediates.
    const char* t = topic.c_str();
    return strncmp(t + tlen - llen, leaf, llen) == 0;
}
