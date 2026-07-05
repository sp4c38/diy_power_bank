#include <ArduinoLog.h>

#include "balancer.h"

bool Balancer::update(Bq76920Driver& driver, const PackSnapshot& snapshot, bool allowed) {
    uint16_t minCell = min(snapshot.cell1Mv, min(snapshot.cell2Mv, snapshot.cell5Mv));
    uint16_t maxCell = max(snapshot.cell1Mv, max(snapshot.cell2Mv, snapshot.cell5Mv));
    uint16_t delta = maxCell - minCell;
    if (delta <= thresholds::balanceStopDeltaMv) {
        timeoutLatched = false;
    }

    if (!allowed) {
        return stop(driver);
    }

    if (timeoutLatched) {
        return stop(driver);
    }

    if (activeMask != 0) {
        if (delta <= thresholds::balanceStopDeltaMv) {
            return stop(driver);
        }
        if (millis() - startedAtMs >= thresholds::balanceMaxDurationMs) {
            timeoutLatched = true;
            return stop(driver);
        }
        return true;
    }

    if (delta < thresholds::balanceStartDeltaMv) {
        return true;
    }

    activeMask = chooseCellToBalance(snapshot);
    if (activeMask == 0) {
        return true;
    }
    startedAtMs = millis();
    timeoutLatched = false;
    Log.noticeln("Starting balancing mask %X.", activeMask);
    return driver.setBalancingMask(activeMask);
}

bool Balancer::stop(Bq76920Driver& driver) {
    if (activeMask == 0 && driver.cellBalancing() == 0) {
        return true;
    }
    activeMask = 0;
    startedAtMs = 0;
    Log.noticeln("Balancing off.");
    return driver.disableBalancing();
}

bool Balancer::active() const {
    return activeMask != 0;
}

uint8_t Balancer::mask() const {
    return activeMask;
}

bool Balancer::timedOut() const {
    return timeoutLatched;
}

void Balancer::clearTimeout() {
    timeoutLatched = false;
}

uint8_t Balancer::chooseCellToBalance(const PackSnapshot& snapshot) const {
    if (snapshot.cell1Mv >= snapshot.cell2Mv && snapshot.cell1Mv >= snapshot.cell5Mv) {
        return BALANCE_CELL_1;
    }
    if (snapshot.cell2Mv >= snapshot.cell1Mv && snapshot.cell2Mv >= snapshot.cell5Mv) {
        return BALANCE_CELL_2;
    }
    return BALANCE_CELL_5;
}
