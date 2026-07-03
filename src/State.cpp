// src/State.cpp

#include "State.h"
#include "Config.h"

void State::begin() {
    clntid     = String(ESP.getChipId());
    nodetopic  = String(TOPIC_BASE) + "/" + clntid;
    topic      = String(TOPIC_BASE);

    light          = "OFF";
    lightNow       = "OFF";
    lightSelected  = "MAIN";   // first iteration has only MAIN source.

    count      = 0;
    error_no   = 0;
    broker     = false;
    message    = "";
}

// Singleton instance — defined here, declared `extern` in State.h.
State g_state;
