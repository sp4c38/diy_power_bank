#include <ArduinoLog.h>
#include <stdlib.h>

#include "packMonitor.h"
#include "utils.h"

// Approximate resting open-circuit-voltage to state-of-charge mapping for the
// NCR18650B, expressed on this pack's usable window (3.10 V = 0 %, 4.15 V = 100 %).
// Only used to seed the coulomb integrator at rest; the integrator and the
// end-of-charge / end-of-discharge anchors carry accuracy from there.
static float ocvSocFraction(uint16_t cellMv) {
    static const struct { uint16_t mv; uint8_t pct; } table[] = {
        {3100, 0}, {3200, 4}, {3300, 8}, {3400, 13}, {3500, 20}, {3600, 30},
        {3700, 42}, {3800, 55}, {3900, 68}, {4000, 80}, {4100, 92}, {4150, 100}
    };
    const uint8_t count = sizeof(table) / sizeof(table[0]);
    if (cellMv <= table[0].mv) {
        return 0.0f;
    }
    if (cellMv >= table[count - 1].mv) {
        return 1.0f;
    }
    for (uint8_t i = 1; i < count; i++) {
        if (cellMv < table[i].mv) {
            float span = table[i].mv - table[i - 1].mv;
            float frac = (cellMv - table[i - 1].mv) / span;
            float pct = table[i - 1].pct + frac * (table[i].pct - table[i - 1].pct);
            return pct / 100.0f;
        }
    }
    return 1.0f;
}

bool PackMonitor::update(Bq76920Driver& driver) {
    BqRawReadings rawReading;
    if (!driver.readRaw(rawReading)) {
        markBqOffline();
        return false;
    }

    lastRaw = rawReading;
    PackSnapshot sample;
    sample.cell1Mv = driver.cellMvFromRaw(rawReading.vc1Raw);
    sample.cell2Mv = driver.cellMvFromRaw(rawReading.vc2Raw);
    sample.cell5Mv = driver.cellMvFromRaw(rawReading.vc5Raw);
    sample.packMv = driver.packMvFromRaw(rawReading.batRaw);
    sample.currentMa = driver.currentMaFromRaw(rawReading.ccRaw);
    sample.dieTempCentiC = driver.dieTempCentiCFromRaw(rawReading.tempRaw);
    sample.balanceMask = rawReading.cellBal;
    sample.sysStat = rawReading.sysStat;
    sample.sysCtrl1 = rawReading.sysCtrl1;
    sample.sysCtrl2 = rawReading.sysCtrl2;
    sample.updatedAtMs = millis();
    sample.stale = false;

    bool trusted = evaluateTrust(sample);
    pushSample(sample);
    current = buildFilteredSnapshot(sample);
    current.trusted = trusted && trustedConsecutiveSamples >= 3;
    current.lowCellWarning = minCellMv() <= thresholds::lowWarnMv;
    current.faultFlags = deriveFaults(current, current.trusted);
    if (current.faultFlags & FAULT_SENSOR) {
        current.state = PackState::SensorFault;
    }
    updateStateOfCharge(current);
    return true;
}

void PackMonitor::updateStateOfCharge(const PackSnapshot& sample) {
    unsigned long now = sample.updatedAtMs;
    uint16_t lowCell = min(sample.cell1Mv, min(sample.cell2Mv, sample.cell5Mv));
    uint16_t highCell = max(sample.cell1Mv, max(sample.cell2Mv, sample.cell5Mv));

    // Seed the integrator once, from resting voltage, after readings are trusted.
    if (!socInitialized) {
        if (!sample.trusted) {
            return;
        }
        chargeMah = ocvSocFraction(lowCell) * capacityMah;
        lastCoulombMs = now;
        socInitialized = true;
        return;
    }

    // Integrate charge: positive current charges, negative discharges. Skip
    // implausibly long gaps (e.g. after a BQ stall) so they don't jump the count.
    unsigned long previousCoulombMs = lastCoulombMs;
    unsigned long dtMs = previousCoulombMs == 0 ? 0 : now - previousCoulombMs;
    lastCoulombMs = now;
    if (dtMs > 0 && dtMs < 5000) {
        chargeMah += (float) sample.currentMa * ((float) dtMs / 3600000.0f);
    }

    if (correctionRemainingMah != 0.0f && dtMs > 0 && dtMs < 5000) {
        float correction = correctionRateMahPerMs * (float) dtMs;
        if (abs(correction) >= abs(correctionRemainingMah)) {
            correction = correctionRemainingMah;
        }
        chargeMah += correction;
        correctionRemainingMah -= correction;
    }

    if (reconciliationPending) {
        bool resting = sample.trusted && abs(sample.currentMa) <= thresholds::idleCurrentMa;
        if (!resting) {
            restoredIdleSinceMs = 0;
        } else if (restoredIdleSinceMs == 0) {
            restoredIdleSinceMs = now;
        } else if (now - restoredIdleSinceMs >= 30000UL) {
            float voltageMah = ocvSocFraction(lowCell) * capacityMah;
            float differencePercent = abs(voltageMah - chargeMah) / capacityMah * 100.0f;
            if (differencePercent > 20.0f) {
                chargeMah = voltageMah;
                correctionRemainingMah = 0.0f;
                largeGaugeReconciliation = true;
            } else if (differencePercent > 8.0f) {
                correctionRemainingMah = (voltageMah - chargeMah) * 0.25f;
                correctionRateMahPerMs = correctionRemainingMah / 60000.0f;
            }
            reconciliationPending = false;
        }
    }

    // Re-anchor at the window edges so integration drift cannot accumulate.
    if (lowCell <= thresholds::outputOffMv) {
        chargeMah = 0.0f;
        reconciliationPending = false;
        correctionRemainingMah = 0.0f;
    } else if (highCell >= thresholds::chargeStopMv) {
        chargeMah = capacityMah;
        reconciliationPending = false;
        correctionRemainingMah = 0.0f;
    }

    chargeMah = constrain(chargeMah, 0.0f, capacityMah);
}

void PackMonitor::markBqOffline() {
    current.stale = true;
    current.trusted = false;
    current.state = PackState::BqOffline;
    current.faultFlags |= FAULT_BQ_OFFLINE;
}

void PackMonitor::applyPolicy(PackState state, uint16_t faults) {
    current.state = state;
    current.faultFlags = faults;
    current.lowCellWarning = minCellMv() <= thresholds::lowWarnMv;
}

const PackSnapshot& PackMonitor::snapshot() const {
    return current;
}

const BqRawReadings& PackMonitor::raw() const {
    return lastRaw;
}

void PackMonitor::pushSample(const PackSnapshot& sample) {
    cell1Samples[sampleIndex] = sample.cell1Mv;
    cell2Samples[sampleIndex] = sample.cell2Mv;
    cell5Samples[sampleIndex] = sample.cell5Mv;
    packSamples[sampleIndex] = sample.packMv;
    currentSamples[sampleIndex] = sample.currentMa;
    tempSamples[sampleIndex] = sample.dieTempCentiC;
    sampleIndex = (sampleIndex + 1) % 5;
    if (sampleCount < 5) {
        sampleCount++;
    }
}

PackSnapshot PackMonitor::buildFilteredSnapshot(const PackSnapshot& sample) {
    PackSnapshot filtered = sample;
    filtered.cell1Mv = median5(cell1Samples, sampleCount);
    filtered.cell2Mv = median5(cell2Samples, sampleCount);
    filtered.cell5Mv = median5(cell5Samples, sampleCount);
    filtered.packMv = median5(packSamples, sampleCount);
    filtered.currentMa = median5Int(currentSamples, sampleCount);
    filtered.dieTempCentiC = median5Int(tempSamples, sampleCount);
    return filtered;
}

bool PackMonitor::evaluateTrust(const PackSnapshot& sample) {
    uint16_t minCell = min(sample.cell1Mv, min(sample.cell2Mv, sample.cell5Mv));
    uint16_t maxCell = max(sample.cell1Mv, max(sample.cell2Mv, sample.cell5Mv));
    uint32_t summedCells = (uint32_t) sample.cell1Mv + sample.cell2Mv + sample.cell5Mv;
    uint16_t packMismatch = summedCells > sample.packMv ? summedCells - sample.packMv : sample.packMv - summedCells;
    bool plausible = minCell >= 2400 && maxCell <= 4300 && packMismatch <= thresholds::maxPackMismatchMv;
    bool hugeCellDelta = (maxCell - minCell) > thresholds::maxTrustedCellDeltaMv && abs(sample.currentMa) <= thresholds::idleCurrentMa;
    bool idleJump = false;

    if (hasPreviousSample && abs(sample.currentMa) <= thresholds::idleCurrentMa) {
        uint16_t jump1 = abs((int) sample.cell1Mv - (int) previousSample.cell1Mv);
        uint16_t jump2 = abs((int) sample.cell2Mv - (int) previousSample.cell2Mv);
        uint16_t jump5 = abs((int) sample.cell5Mv - (int) previousSample.cell5Mv);
        uint16_t jumpPack = abs((int) sample.packMv - (int) previousSample.packMv);
        idleJump = jump1 > thresholds::maxIdleSampleJumpMv ||
            jump2 > thresholds::maxIdleSampleJumpMv ||
            jump5 > thresholds::maxIdleSampleJumpMv ||
            jumpPack > (thresholds::maxIdleSampleJumpMv * 2);
    }

    previousSample = sample;
    hasPreviousSample = true;

    if (!plausible || hugeCellDelta || idleJump) {
        if (instabilityScore < 10) {
            instabilityScore += 2;
        }
    } else if (instabilityScore > 0) {
        instabilityScore--;
    }

    bool trusted = plausible && !hugeCellDelta && !idleJump && instabilityScore < 3;
    if (trusted) {
        if (trustedConsecutiveSamples < 10) {
            trustedConsecutiveSamples++;
        }
    } else {
        trustedConsecutiveSamples = 0;
    }
    return trusted;
}

uint16_t PackMonitor::deriveFaults(const PackSnapshot& sample, bool trusted) const {
    uint16_t faults = FAULT_NONE;
    if (sample.sysStat & (1 << (uint8_t) SysStatusOpt::UV)) {
        faults |= FAULT_BQ_UV;
    }
    if (sample.sysStat & (1 << (uint8_t) SysStatusOpt::OV)) {
        faults |= FAULT_BQ_OV;
    }
    if (sample.sysStat & (1 << (uint8_t) SysStatusOpt::SCD)) {
        faults |= FAULT_BQ_SCD;
    }
    if (sample.sysStat & (1 << (uint8_t) SysStatusOpt::OCD)) {
        faults |= FAULT_BQ_OCD;
    }
    if (sample.sysStat & (1 << (uint8_t) SysStatusOpt::DEVICE_XREADY)) {
        faults |= FAULT_BQ_XREADY;
    }
    if (!trusted) {
        faults |= FAULT_SENSOR;
    }
    if (sample.dieTempCentiC < thresholds::minDieTempCentiC || sample.dieTempCentiC > thresholds::maxDieTempCentiC) {
        faults |= FAULT_TEMP;
    }
    uint16_t minCell = min(sample.cell1Mv, min(sample.cell2Mv, sample.cell5Mv));
    if (minCell <= thresholds::outputOffMv) {
        faults |= FAULT_OUTPUT_LOW_CELL;
    }
    return faults;
}

uint8_t PackMonitor::socPercent() const {
    // Before the coulomb integrator is seeded, fall back to the resting-voltage estimate.
    if (!socInitialized) {
        return (uint8_t) lroundf(ocvSocFraction(minCellMv()) * 100.0f);
    }
    float pct = chargeMah / capacityMah * 100.0f;
    return (uint8_t) constrain((long) lroundf(pct), 0L, 100L);
}

uint16_t PackMonitor::chargeMahTenths() const {
    // Full-resolution charge for the app's live readout. Mirrors socPercent()'s
    // pre-seed fallback so the two stay consistent before the integrator is seeded.
    float mah = socInitialized ? chargeMah
        : ocvSocFraction(minCellMv()) * (float) thresholds::usableCapacityMah;
    return (uint16_t) constrain((long) lroundf(mah * 10.0f), 0L, 65535L);
}

uint16_t PackMonitor::minCellMv() const {
    return min(current.cell1Mv, min(current.cell2Mv, current.cell5Mv));
}

uint16_t PackMonitor::maxCellMv() const {
    return max(current.cell1Mv, max(current.cell2Mv, current.cell5Mv));
}

uint16_t PackMonitor::cellDeltaMv() const {
    return maxCellMv() - minCellMv();
}

void PackMonitor::restoreGauge(uint16_t savedChargeMahTenths, uint16_t learnedCapacityMah, bool learnedCapacityValid) {
    setLearnedCapacity(learnedCapacityMah, learnedCapacityValid);
    chargeMah = constrain((float) savedChargeMahTenths / 10.0f, 0.0f, capacityMah);
    socInitialized = true;
    lastCoulombMs = 0;
    restoredIdleSinceMs = 0;
    reconciliationPending = true;
    correctionRemainingMah = 0.0f;
    largeGaugeReconciliation = false;
}

void PackMonitor::resetGauge() {
    capacityMah = (float) thresholds::usableCapacityMah;
    chargeMah = 0.0f;
    socInitialized = false;
    lastCoulombMs = 0;
    restoredIdleSinceMs = 0;
    reconciliationPending = false;
    correctionRemainingMah = 0.0f;
    largeGaugeReconciliation = false;
}

void PackMonitor::setLearnedCapacity(uint16_t learnedCapacityMah, bool valid) {
    float previousCapacityMah = capacityMah;
    float previousFraction = previousCapacityMah > 0.0f
        ? constrain(chargeMah / previousCapacityMah, 0.0f, 1.0f)
        : 0.0f;
    capacityMah = valid
        ? constrain((float) learnedCapacityMah, 1600.0f, 4000.0f)
        : (float) thresholds::usableCapacityMah;
    if (socInitialized) {
        chargeMah = previousFraction * capacityMah;
    }
}

bool PackMonitor::consumeLargeGaugeReconciliation() {
    bool value = largeGaugeReconciliation;
    largeGaugeReconciliation = false;
    return value;
}

bool PackMonitor::gaugeIsProvisional() const {
    return reconciliationPending;
}

float PackMonitor::chargeMahValue() const {
    return socInitialized ? chargeMah : ocvSocFraction(minCellMv()) * capacityMah;
}

uint16_t PackMonitor::effectiveCapacityMah() const {
    return (uint16_t) lroundf(capacityMah);
}
