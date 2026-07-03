#include "powerManager.h"

void PowerManager::noteOutputOn(ControlState& controls) {
    controls.idleOutputOff = false;
    controls.dischargeManuallyDisabled = false;
    lastActivityMs = millis();
    automaticShutdown = false;
}

void PowerManager::noteOutputOff(ControlState& controls) {
    controls.idleOutputOff = true;
    lastActivityMs = millis();
    automaticShutdown = false;
}

void PowerManager::update(const PackSnapshot& snapshot, ControlState& controls) {
    if (lastActivityMs == 0) {
        lastActivityMs = millis();
    }

    if (snapshot.currentMa < thresholds::loadCurrentMa || snapshot.currentMa > thresholds::idleCurrentMa) {
        lastActivityMs = millis();
        if (!controls.dischargeManuallyDisabled && !controls.dischargeLatchedOff) {
            controls.idleOutputOff = false;
            automaticShutdown = false;
        }
    }

    bool idle = abs(snapshot.currentMa) <= thresholds::idleCurrentMa;
    if (idle && !controls.idleOutputOff && millis() - lastActivityMs >= thresholds::outputIdleTimeoutMs) {
        controls.idleOutputOff = true;
        automaticShutdown = true;
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

uint16_t PowerManager::idleRemainingSec(const PackSnapshot& snapshot, const ControlState& controls) const {
    bool idle = abs(snapshot.currentMa) <= thresholds::idleCurrentMa;
    if (!idle ||
        controls.idleOutputOff ||
        controls.dischargeManuallyDisabled ||
        controls.dischargeLatchedOff ||
        lastActivityMs == 0) {
        return 0xFFFF;
    }
    unsigned long elapsed = millis() - lastActivityMs;
    if (elapsed >= thresholds::outputIdleTimeoutMs) {
        return 0;
    }
    return (uint16_t) ((thresholds::outputIdleTimeoutMs - elapsed + 999UL) / 1000UL);
}

bool PowerManager::automaticShutdownOccurred() const {
    return automaticShutdown;
}
