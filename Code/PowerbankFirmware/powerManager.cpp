#include "powerManager.h"

void PowerManager::noteOutputOn(ControlState& controls) {
    controls.idleOutputOff = false;
    controls.dischargeManuallyDisabled = false;
    lastActivityMs = millis();
}

void PowerManager::noteOutputOff(ControlState& controls) {
    controls.idleOutputOff = true;
    lastActivityMs = millis();
}

void PowerManager::update(const PackSnapshot& snapshot, ControlState& controls) {
    if (lastActivityMs == 0) {
        lastActivityMs = millis();
    }

    if (snapshot.currentMa < thresholds::loadCurrentMa || snapshot.currentMa > thresholds::idleCurrentMa) {
        lastActivityMs = millis();
        if (!controls.dischargeManuallyDisabled && !controls.dischargeLatchedOff) {
            controls.idleOutputOff = false;
        }
    }

    bool idle = abs(snapshot.currentMa) <= thresholds::idleCurrentMa;
    if (idle && !controls.idleOutputOff && millis() - lastActivityMs >= thresholds::outputIdleTimeoutMs) {
        controls.idleOutputOff = true;
    }

    if (controls.idleOutputOff && millis() - lastActivityMs >= thresholds::veryLongIdleShipMs) {
        controls.shipRequested = true;
    }
}

unsigned long PowerManager::idleDurationMs() const {
    if (lastActivityMs == 0) {
        return 0;
    }
    return millis() - lastActivityMs;
}
