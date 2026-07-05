#include <ArduinoLog.h>
#include <stdio.h>
#include <string.h>

#include "persistentStore.h"

static const char* statePath = "/pbank/state.bin";
static const char* temporaryStatePath = "/pbank/state.tmp";
static const char* historyPath = "/pbank/history.bin";

// State.controlFlags bits.
static const uint8_t controlDischargeManuallyOff = 1 << 0;
static const uint8_t controlChargeManuallyOff = 1 << 1;
static const uint8_t controlIdleOutputOff = 1 << 2;
static const uint8_t controlChargeLatchedOff = 1 << 3;

PersistentStore::PersistentStore():
    blockDevice(flashStart, flashSize),
    fileSystem("pbank", nullptr, 64, 64, 4096, 128) {
}

bool PersistentStore::begin(PackMonitor& monitor) {
    uintptr_t dataSize = (uintptr_t) &__data_end__ - (uintptr_t) &__data_start__;
    uintptr_t imageEnd = (uintptr_t) &__etext + dataSize;
    if (imageEnd > flashStart) {
        Log.errorln("Persistent storage disabled: firmware overlaps reserved flash.");
        return false;
    }

    int result = fileSystem.mount(&blockDevice);
    if (result != 0) {
        result = fileSystem.reformat(&blockDevice);
    }
    if (result != 0) {
        Log.errorln("Persistent storage unavailable: %d", result);
        return false;
    }
    mounted = true;

    bool restored = loadState();
    if (!restored) {
        memset(&state, 0, sizeof(state));
        state.magic = stateMagic;
        state.version = stateVersion;
        state.size = sizeof(State);
        state.learnedCapacityMah = thresholds::usableCapacityMah;
    }

    state.bootId++;
    if (state.gaugeValid) {
        monitor.restoreGauge(
            state.chargeMahTenths,
            state.learnedCapacityMah,
            state.learnedCapacityValid != 0
        );
    }

    totalDischargedMah = (float) state.totalDischargedMahTenths / 10.0f;
    totalEnergyWh = (float) state.totalEnergyMilliWh / 1000.0f;
    calibrationDischargedMah = (float) state.calibrationDischargedMahTenths / 10.0f;
    scanHistory();
    if (saveState(monitor, monitor.snapshot())) {
        lastCheckpointMs = millis();
    }
    Log.noticeln("Persistent storage ready; boot %lu; history %lu-%lu.",
        (unsigned long) state.bootId,
        (unsigned long) oldestSequence,
        (unsigned long) latestSequence);
    return true;
}

bool PersistentStore::available() const {
    return mounted;
}

void PersistentStore::update(
    PackMonitor& monitor,
    const PackSnapshot& snapshot,
    uint16_t telemetryFlags,
    bool chargeComplete
) {
    if (!mounted || !snapshot.trusted) {
        return;
    }

    uint32_t nowMs = millis();
    uint32_t dtMs = lastHealthUpdateMs == 0 ? 0 : nowMs - lastHealthUpdateMs;
    lastHealthUpdateMs = nowMs;

    if (dtMs > 0 && dtMs < 5000) {
        if (snapshot.currentMa < 0) {
            float dischargedMah = (float) -snapshot.currentMa * ((float) dtMs / 3600000.0f);
            totalDischargedMah += dischargedMah;
            totalEnergyWh += dischargedMah / 1000.0f * ((float) snapshot.packMv / 1000.0f);
            if (state.calibrationActive) {
                calibrationDischargedMah += dischargedMah;
            }
        }

        if (snapshot.dieTempCentiC >= 4500) {
            hotRemainderMs += dtMs;
            if (hotRemainderMs >= 1000) {
                state.hotSeconds += hotRemainderMs / 1000;
                hotRemainderMs %= 1000;
            }
        } else {
            hotRemainderMs = 0;
        }
    }

    if (snapshot.dieTempCentiC > state.maximumTempCentiC) {
        state.maximumTempCentiC = snapshot.dieTempCentiC;
    }

    if (abs(snapshot.currentMa) <= thresholds::idleCurrentMa &&
        (lastIdleDeltaSampleMs == 0 || nowMs - lastIdleDeltaSampleMs >= 60000UL)) {
        uint16_t delta = monitor.cellDeltaMv();
        state.idleDeltaTotalMv += delta;
        state.idleDeltaSamples++;
        state.maximumIdleDeltaMv = max(state.maximumIdleDeltaMv, delta);
        lastIdleDeltaSampleMs = nowMs;
    }

    bool calibrationStarted = chargeComplete && !lastChargeComplete;
    if (calibrationStarted) {
        state.calibrationActive = 1;
        calibrationDischargedMah = 0.0f;
    }
    lastChargeComplete = chargeComplete;

    bool completedCalibration = state.calibrationActive &&
        monitor.minCellMv() <= thresholds::outputOffMv;
    if (completedCalibration) {
        if (calibrationDischargedMah >= 1600.0f && calibrationDischargedMah <= 4000.0f) {
            uint16_t measured = (uint16_t) lroundf(calibrationDischargedMah);
            if (state.learnedCapacityValid) {
                state.learnedCapacityMah = (uint16_t) lroundf(
                    (float) state.learnedCapacityMah * 0.75f + (float) measured * 0.25f
                );
            } else {
                state.learnedCapacityMah = measured;
            }
            state.learnedCapacityValid = 1;
            state.validCapacityCycles++;
            monitor.setLearnedCapacity(state.learnedCapacityMah, true);
        }
        state.calibrationActive = 0;
        calibrationDischargedMah = 0.0f;
    }

    bool gaugeReconciled = monitor.consumeLargeGaugeReconciliation();
    if (gaugeReconciled) {
        state.learnedCapacityValid = 0;
        state.validCapacityCycles = 0;
        monitor.setLearnedCapacity(thresholds::usableCapacityMah, false);
    }

    bool anchor = monitor.minCellMv() <= thresholds::outputOffMv ||
        monitor.maxCellMv() >= thresholds::chargeStopMv;
    bool anchorReached = anchor && !lastAtAnchor;
    lastAtAnchor = anchor;
    bool checkpointDue = lastCheckpointMs == 0 || nowMs - lastCheckpointMs >= 900000UL;
    if (checkpointDue || anchorReached || calibrationStarted || gaugeReconciled || completedCalibration) {
        stateCheckpointPending = true;
    }

    bool flashWriteReady = lastFlashWriteMs == 0 ||
        nowMs - lastFlashWriteMs >= flashWriteSpacingMs;
    bool historyWritten = false;
    if (flashWriteReady && shouldRecordHistory(snapshot, telemetryFlags, nowMs)) {
        historyWritten = appendHistory(monitor, snapshot, telemetryFlags);
    }

    if (stateCheckpointPending && flashWriteReady && !historyWritten) {
        if (saveState(monitor, snapshot)) {
            stateCheckpointPending = false;
            lastCheckpointMs = nowMs;
        }
    }
}

void PersistentStore::setTime(uint32_t unixTime, uint32_t uptimeSec) {
    syncedUnixTime = unixTime;
    syncedUptimeSec = uptimeSec;
}

void PersistentStore::restoreControls(ControlState& controls) const {
    if (!mounted) {
        return;
    }
    controls.dischargeManuallyDisabled = (state.controlFlags & controlDischargeManuallyOff) != 0;
    controls.chargeManuallyDisabled = (state.controlFlags & controlChargeManuallyOff) != 0;
    controls.idleOutputOff = (state.controlFlags & controlIdleOutputOff) != 0;
    // The charge latch normally re-derives from voltage, but between the stop
    // and resume thresholds it is hysteresis state — restoring it avoids a
    // top-up micro-cycle after every maintenance reboot on the charger.
    controls.chargeLatchedOff = (state.controlFlags & controlChargeLatchedOff) != 0;
}

void PersistentStore::syncControls(const ControlState& controls) {
    uint8_t flags = 0;
    if (controls.dischargeManuallyDisabled) {
        flags |= controlDischargeManuallyOff;
    }
    if (controls.chargeManuallyDisabled) {
        flags |= controlChargeManuallyOff;
    }
    if (controls.idleOutputOff) {
        flags |= controlIdleOutputOff;
    }
    if (controls.chargeLatchedOff) {
        flags |= controlChargeLatchedOff;
    }
    if (flags != state.controlFlags) {
        state.controlFlags = flags;
        stateCheckpointPending = true;
    }
}

void PersistentStore::checkpoint(const PackMonitor& monitor, const PackSnapshot& snapshot) {
    if (!mounted) {
        return;
    }
    if (saveState(monitor, snapshot)) {
        stateCheckpointPending = false;
        lastCheckpointMs = millis();
    }
}

void PersistentStore::setIdleElapsedSec(uint32_t seconds) {
    // Updated silently: the value rides along with the next regular checkpoint
    // instead of forcing flash writes of its own.
    state.idleElapsedSec = seconds;
}

uint32_t PersistentStore::savedIdleElapsedSec() const {
    return state.idleElapsedSec;
}

void PersistentStore::resetLearnedBattery(PackMonitor& monitor) {
    if (!mounted) {
        monitor.resetGauge();
        return;
    }

    uint32_t currentBootId = state.bootId;
    memset(&state, 0, sizeof(state));
    state.magic = stateMagic;
    state.version = stateVersion;
    state.size = sizeof(State);
    state.bootId = currentBootId;
    state.learnedCapacityMah = thresholds::usableCapacityMah;
    totalDischargedMah = 0.0f;
    totalEnergyWh = 0.0f;
    calibrationDischargedMah = 0.0f;
    oldestSequence = 0;
    latestSequence = 0;
    nextSequence = 1;
    hasHistoryBaseline = false;
    remove(historyPath);
    monitor.resetGauge();
    saveState(monitor, monitor.snapshot());
}

HealthPayload PersistentStore::healthPayload() const {
    HealthPayload payload = {};
    payload.version = 1;
    payload.confidence = state.learnedCapacityValid ? 1 : 0;
    payload.learnedCapacityMah = state.learnedCapacityValid
        ? state.learnedCapacityMah
        : thresholds::usableCapacityMah;
    payload.totalDischargedMah = (uint32_t) min(
        (double) totalDischargedMah,
        (double) UINT32_MAX
    );
    payload.totalEnergyWhTenths = (uint32_t) min(
        (double) totalEnergyWh * 10.0,
        (double) UINT32_MAX
    );
    float cycleCapacity = state.learnedCapacityValid
        ? (float) state.learnedCapacityMah
        : (float) thresholds::usableCapacityMah;
    payload.equivalentCyclesTenths = cycleCapacity > 0
        ? (uint32_t) lroundf(totalDischargedMah / cycleCapacity * 10.0f)
        : 0;
    payload.hotMinutes = (uint32_t) min(state.hotSeconds / 60ULL, 0xFFFFFFFFULL);
    payload.maximumTempCentiC = state.maximumTempCentiC;
    payload.averageIdleDeltaMv = state.idleDeltaSamples > 0
        ? (uint16_t) min(state.idleDeltaTotalMv / state.idleDeltaSamples, 65535ULL)
        : 0;
    payload.maximumIdleDeltaMv = state.maximumIdleDeltaMv;
    payload.validCapacityCycles = state.validCapacityCycles;
    return payload;
}

uint32_t PersistentStore::bootId() const {
    return state.bootId;
}

uint32_t PersistentStore::oldestHistorySequence() const {
    return oldestSequence;
}

uint32_t PersistentStore::latestHistorySequence() const {
    return latestSequence;
}

bool PersistentStore::readHistory(uint32_t sequence, HistoryRecordPayload& record) {
    if (!mounted || sequence == 0 || sequence < oldestSequence || sequence > latestSequence) {
        return false;
    }
    FILE* file = fopen(historyPath, "rb");
    if (file == nullptr) {
        return false;
    }
    uint32_t slot = (sequence - 1) % historyCapacity;
    fseek(file, (long) slot * sizeof(StoredHistoryRecord), SEEK_SET);
    StoredHistoryRecord stored = {};
    size_t count = fread(&stored, sizeof(stored), 1, file);
    fclose(file);
    if (count != 1 ||
        stored.payload.sequence != sequence ||
        stored.checksum != (uint16_t) checksum(&stored.payload, sizeof(stored.payload))) {
        return false;
    }
    record = stored.payload;
    return true;
}

bool PersistentStore::loadState() {
    FILE* file = fopen(statePath, "rb");
    if (file == nullptr) {
        return false;
    }
    uint8_t buffer[sizeof(State)] = {};
    size_t bytesRead = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);

    if (bytesRead == sizeof(State)) {
        State candidate = {};
        memcpy(&candidate, buffer, sizeof(candidate));
        uint32_t storedChecksum = candidate.checksum;
        candidate.checksum = 0;
        bool valid = candidate.magic == stateMagic &&
            candidate.version == stateVersion &&
            candidate.size == sizeof(State) &&
            storedChecksum == checksum(&candidate, sizeof(candidate));
        if (valid) {
            candidate.checksum = storedChecksum;
            state = candidate;
            return true;
        }
    }

    // v1 layout is v2 without idleElapsedSec (which sits right before the
    // checksum), so a valid v1 blob migrates by copying the shared prefix.
    constexpr size_t v1Size = sizeof(State) - sizeof(uint32_t);
    constexpr size_t v1FieldBytes = v1Size - sizeof(uint32_t);
    if (bytesRead >= v1Size) {
        uint32_t v1Magic = 0;
        uint16_t v1Version = 0;
        uint16_t v1StructSize = 0;
        memcpy(&v1Magic, buffer, sizeof(v1Magic));
        memcpy(&v1Version, buffer + 4, sizeof(v1Version));
        memcpy(&v1StructSize, buffer + 6, sizeof(v1StructSize));
        if (v1Magic == stateMagic && v1Version == 1 && v1StructSize == v1Size) {
            uint32_t storedChecksum = 0;
            memcpy(&storedChecksum, buffer + v1FieldBytes, sizeof(storedChecksum));
            uint8_t verify[v1Size] = {};
            memcpy(verify, buffer, v1Size);
            memset(verify + v1FieldBytes, 0, sizeof(uint32_t));
            if (storedChecksum == checksum(verify, v1Size)) {
                memset(&state, 0, sizeof(state));
                memcpy(&state, buffer, v1FieldBytes);
                state.version = stateVersion;
                state.size = sizeof(State);
                state.idleElapsedSec = 0;
                Log.noticeln("Migrated persistent state v1 -> v2.");
                return true;
            }
        }
    }
    return false;
}

bool PersistentStore::saveState(const PackMonitor& monitor, const PackSnapshot& snapshot) {
    if (!mounted) {
        return false;
    }
    state.magic = stateMagic;
    state.version = stateVersion;
    state.size = sizeof(State);
    state.chargeMahTenths = monitor.chargeMahTenths();
    if (snapshot.trusted) {
        state.gaugeValid = 1;
        state.lastCell1Mv = snapshot.cell1Mv;
        state.lastCell2Mv = snapshot.cell2Mv;
        state.lastCell5Mv = snapshot.cell5Mv;
    }
    state.calibrationDischargedMahTenths =
        (uint32_t) min((double) calibrationDischargedMah * 10.0, (double) UINT32_MAX);
    state.totalDischargedMahTenths =
        (uint64_t) max(0.0f, totalDischargedMah * 10.0f);
    state.totalEnergyMilliWh =
        (uint64_t) max(0.0f, totalEnergyWh * 1000.0f);
    state.checksum = 0;
    state.checksum = checksum(&state, sizeof(state));

    FILE* file = fopen(temporaryStatePath, "wb");
    if (file == nullptr) {
        return false;
    }
    bool written = fwrite(&state, sizeof(state), 1, file) == 1;
    fflush(file);
    fclose(file);
    if (!written) {
        remove(temporaryStatePath);
        return false;
    }
    bool saved = rename(temporaryStatePath, statePath) == 0;
    if (!saved) {
        remove(statePath);
        saved = rename(temporaryStatePath, statePath) == 0;
    }
    if (saved) {
        lastFlashWriteMs = millis();
    }
    return saved;
}

void PersistentStore::scanHistory() {
    oldestSequence = 0;
    latestSequence = 0;
    uint32_t maximumStoredBootId = 0;
    FILE* file = fopen(historyPath, "rb");
    if (file == nullptr) {
        nextSequence = 1;
        return;
    }
    for (uint16_t slot = 0; slot < historyCapacity; slot++) {
        StoredHistoryRecord stored = {};
        if (fread(&stored, sizeof(stored), 1, file) != 1) {
            break;
        }
        if (stored.payload.sequence != 0 &&
            stored.checksum == (uint16_t) checksum(&stored.payload, sizeof(stored.payload))) {
            latestSequence = max(latestSequence, stored.payload.sequence);
            maximumStoredBootId = max(maximumStoredBootId, stored.payload.bootId);
        }
    }
    fclose(file);
    if (latestSequence > 0) {
        oldestSequence = latestSequence >= historyCapacity
            ? latestSequence - historyCapacity + 1
            : 1;
        nextSequence = latestSequence + 1;
    } else {
        nextSequence = 1;
    }
    if (state.bootId <= maximumStoredBootId) {
        state.bootId = maximumStoredBootId + 1;
    }
}

bool PersistentStore::appendHistory(
    const PackMonitor& monitor,
    const PackSnapshot& snapshot,
    uint16_t telemetryFlags
) {
    HistoryRecordPayload payload = {};
    payload.sequence = nextSequence;
    payload.bootId = state.bootId;
    payload.uptimeSec = snapshot.updatedAtMs / 1000UL;
    payload.epochSec = epochForUptime(payload.uptimeSec);
    payload.flags = telemetryFlags;
    payload.faults = snapshot.faultFlags;
    payload.cell1Mv = snapshot.cell1Mv;
    payload.cell2Mv = snapshot.cell2Mv;
    payload.cell5Mv = snapshot.cell5Mv;
    payload.packMv = snapshot.packMv;
    payload.currentMa = snapshot.currentMa;
    payload.dieTempCentiC = snapshot.dieTempCentiC;
    payload.socPercent = monitor.socPercent();
    payload.state = (uint8_t) snapshot.state;

    StoredHistoryRecord stored = {};
    stored.payload = payload;
    stored.checksum = (uint16_t) checksum(&stored.payload, sizeof(stored.payload));

    FILE* file = fopen(historyPath, "r+b");
    if (file == nullptr) {
        file = fopen(historyPath, "w+b");
    }
    if (file == nullptr) {
        return false;
    }
    uint32_t slot = (payload.sequence - 1) % historyCapacity;
    fseek(file, (long) slot * sizeof(StoredHistoryRecord), SEEK_SET);
    bool written = fwrite(&stored, sizeof(stored), 1, file) == 1;
    fflush(file);
    fclose(file);
    if (!written) {
        return false;
    }

    latestSequence = payload.sequence;
    oldestSequence = latestSequence >= historyCapacity
        ? latestSequence - historyCapacity + 1
        : 1;
    nextSequence++;
    lastHistoryMs = millis();
    lastHistoryFlags = telemetryFlags;
    lastHistoryFaults = snapshot.faultFlags;
    lastHistoryState = snapshot.state;
    hasHistoryBaseline = true;
    lastFlashWriteMs = millis();
    return true;
}

bool PersistentStore::shouldRecordHistory(
    const PackSnapshot& snapshot,
    uint16_t telemetryFlags,
    uint32_t nowMs
) const {
    if (!hasHistoryBaseline || lastHistoryMs == 0 || nowMs - lastHistoryMs >= 600000UL) {
        return true;
    }
    const uint16_t eventFlags = FLAG_LOW_CELL_WARN |
        FLAG_CHARGE_COMPLETE |
        FLAG_BALANCE_TIMEOUT |
        FLAG_IDLE_OUTPUT_OFF;
    return snapshot.state != lastHistoryState ||
        snapshot.faultFlags != lastHistoryFaults ||
        (telemetryFlags & eventFlags) != (lastHistoryFlags & eventFlags);
}

uint32_t PersistentStore::epochForUptime(uint32_t uptimeSec) const {
    if (syncedUnixTime == 0 || uptimeSec < syncedUptimeSec) {
        return 0;
    }
    return syncedUnixTime + (uptimeSec - syncedUptimeSec);
}

uint32_t PersistentStore::checksum(const void* bytes, size_t length) {
    const uint8_t* data = (const uint8_t*) bytes;
    uint32_t value = 2166136261UL;
    for (size_t index = 0; index < length; index++) {
        value ^= data[index];
        value *= 16777619UL;
    }
    return value;
}
