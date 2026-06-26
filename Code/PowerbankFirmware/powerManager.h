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

private:
    unsigned long lastActivityMs = 0;
};

#endif
