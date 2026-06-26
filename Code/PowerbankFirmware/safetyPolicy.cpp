#include "safetyPolicy.h"

PolicyDecision SafetyPolicy::evaluate(const PackSnapshot& snapshot, ControlState& controls) {
    PolicyDecision decision;
    decision.faults = snapshot.faultFlags;

    bool bqFault = decision.faults & (FAULT_BQ_UV | FAULT_BQ_OV | FAULT_BQ_SCD | FAULT_BQ_OCD | FAULT_BQ_OFFLINE | FAULT_BQ_XREADY);
    bool sensorFault = decision.faults & FAULT_SENSOR;
    bool tempFault = decision.faults & FAULT_TEMP;
    uint16_t minCell = min(snapshot.cell1Mv, min(snapshot.cell2Mv, snapshot.cell5Mv));
    uint16_t maxCell = max(snapshot.cell1Mv, max(snapshot.cell2Mv, snapshot.cell5Mv));

    if (controls.shipRequested || minCell <= thresholds::criticalShipMv) {
        decision.requestShip = true;
        decision.state = PackState::Ship;
        return decision;
    }

    if (bqFault || sensorFault || tempFault) {
        decision.state = sensorFault ? PackState::SensorFault : PackState::Fault;
        return decision;
    }

    if (maxCell >= thresholds::chargeStopMv) {
        controls.chargeLatchedOff = true;
    } else if (maxCell <= thresholds::chargeResumeMv) {
        controls.chargeLatchedOff = false;
    }

    if (minCell <= thresholds::outputOffMv) {
        controls.dischargeLatchedOff = true;
        controls.idleOutputOff = true;
        decision.faults |= FAULT_OUTPUT_LOW_CELL;
    } else if (minCell >= thresholds::lowWarnMv) {
        controls.dischargeLatchedOff = false;
    }

    decision.allowCharge = !controls.chargeManuallyDisabled && !controls.chargeLatchedOff;
    decision.allowDischarge = !controls.dischargeManuallyDisabled && !controls.dischargeLatchedOff && !controls.idleOutputOff;
    decision.allowBalancing = snapshot.trusted &&
        !controls.chargeManuallyDisabled &&
        minCell >= thresholds::balanceMinCellMv &&
        abs(snapshot.currentMa) <= thresholds::idleCurrentMa;

    if (snapshot.balanceMask != 0) {
        decision.state = PackState::Balancing;
    } else if (controls.idleOutputOff) {
        decision.state = PackState::OutputOffIdle;
    } else if (snapshot.currentMa > thresholds::idleCurrentMa) {
        decision.state = PackState::Charging;
    } else if (snapshot.currentMa < thresholds::loadCurrentMa) {
        decision.state = PackState::Discharging;
    } else {
        decision.state = PackState::Idle;
    }

    return decision;
}
