#ifndef SAFETY_POLICY_H
#define SAFETY_POLICY_H

#include <Arduino.h>

#include "register.h"

struct ControlState {
    bool chargeManuallyDisabled = false;
    bool dischargeManuallyDisabled = false;
    bool idleOutputOff = false;
    bool chargeLatchedOff = false;
    bool dischargeLatchedOff = false;
    bool shipRequested = false;
};

struct PolicyDecision {
    bool allowCharge = false;
    bool allowDischarge = false;
    bool allowBalancing = false;
    bool requestShip = false;
    PackState state = PackState::Starting;
    uint16_t faults = FAULT_NONE;
};

class SafetyPolicy {
public:
    PolicyDecision evaluate(const PackSnapshot& snapshot, ControlState& controls);
};

#endif
