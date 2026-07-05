#include "powerManager.h"

void PowerManager::noteOutputOn(ControlState& controls) {
    controls.idleOutputOff = false;
    controls.dischargeManuallyDisabled = false;
    lastActivityMs = millis();
    idleCarryMs = 0;
    automaticShutdown = false;
}

void PowerManager::noteOutputOff(ControlState& controls) {
    controls.idleOutputOff = true;
    lastActivityMs = millis();
    idleCarryMs = 0;
    automaticShutdown = false;
}

void PowerManager::update(const PackSnapshot& snapshot, ControlState& controls) {
    if (lastActivityMs == 0) {
        lastActivityMs = millis();
    }

    if (snapshot.currentMa < thresholds::loadCurrentMa || snapshot.currentMa > thresholds::idleCurrentMa) {
        lastActivityMs = millis();
        idleCarryMs = 0;
        if (!controls.dischargeManuallyDisabled && !controls.dischargeLatchedOff) {
            controls.idleOutputOff = false;
            automaticShutdown = false;
        }
    }

    bool idle = abs(snapshot.currentMa) <= thresholds::idleCurrentMa;
    if (idle && !controls.idleOutputOff && idleDurationMs() >= thresholds::outputIdleTimeoutMs) {
        controls.idleOutputOff = true;
        automaticShutdown = true;
    }

    if (controls.idleOutputOff && idleDurationMs() >= thresholds::veryLongIdleShipMs) {
        controls.shipRequested = true;
    }
}

unsigned long PowerManager::idleDurationMs() const {
    if (lastActivityMs == 0) {
        return idleCarryMs;
    }
    return millis() - lastActivityMs + idleCarryMs;
}

void PowerManager::restoreIdleElapsed(uint32_t seconds) {
    idleCarryMs = (unsigned long) seconds * 1000UL;
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
    unsigned long elapsed = idleDurationMs();
    if (elapsed >= thresholds::outputIdleTimeoutMs) {
        return 0;
    }
    return (uint16_t) ((thresholds::outputIdleTimeoutMs - elapsed + 999UL) / 1000UL);
}

bool PowerManager::automaticShutdownOccurred() const {
    return automaticShutdown;
}
