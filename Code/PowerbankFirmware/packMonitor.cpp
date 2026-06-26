#include <ArduinoLog.h>
#include <stdlib.h>

#include "packMonitor.h"
#include "utils.h"

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
    return true;
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
    uint16_t minCell = minCellMv();
    if (minCell <= thresholds::outputOffMv) {
        return 0;
    }
    if (minCell >= thresholds::chargeStopMv) {
        return 100;
    }
    return (uint8_t) (((uint32_t) (minCell - thresholds::outputOffMv) * 100UL) / (thresholds::chargeStopMv - thresholds::outputOffMv));
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
