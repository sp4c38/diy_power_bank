#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>

#include "register.h"
#include "safetyPolicy.h"

class PowerManager {
public:
    void noteOutputOn(ControlState& controls);
    void noteOutputOff(ControlState& controls);
    void update(const PackSnapshot& snapshot, ControlState& controls);
    unsigned long idleDurationMs() const;
    uint16_t idleRemainingSec(const PackSnapshot& snapshot, const ControlState& controls) const;
    bool automaticShutdownOccurred() const;

private:
    unsigned long lastActivityMs = 0;
    bool automaticShutdown = false;
};

#endif
