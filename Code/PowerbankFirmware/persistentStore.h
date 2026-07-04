#ifndef PERSISTENT_STORE_H
#define PERSISTENT_STORE_H

#include <Arduino.h>
#include <FlashIAPBlockDevice.h>
#include <LittleFileSystem.h>

#include "packMonitor.h"
#include "register.h"

class PersistentStore {
public:
    PersistentStore();

    bool begin(PackMonitor& monitor);
    bool available() const;
    void update(PackMonitor& monitor, const PackSnapshot& snapshot, uint16_t telemetryFlags, bool chargeComplete);
    void setTime(uint32_t unixTime, uint32_t uptimeSec);
    void resetLearnedBattery(PackMonitor& monitor);

    HealthPayload healthPayload() const;
    uint32_t bootId() const;
    uint32_t oldestHistorySequence() const;
    uint32_t latestHistorySequence() const;
    bool readHistory(uint32_t sequence, HistoryRecordPayload& record);

private:
#pragma pack(push, 1)
    struct State {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint32_t bootId;
        uint8_t gaugeValid;
        uint8_t learnedCapacityValid;
        uint8_t calibrationActive;
        uint8_t reserved;
        uint16_t chargeMahTenths;
        uint16_t learnedCapacityMah;
        uint16_t lastCell1Mv;
        uint16_t lastCell2Mv;
        uint16_t lastCell5Mv;
        uint16_t validCapacityCycles;
        uint32_t calibrationDischargedMahTenths;
        uint64_t totalDischargedMahTenths;
        uint64_t totalEnergyMilliWh;
        uint64_t hotSeconds;
        int16_t maximumTempCentiC;
        uint16_t maximumIdleDeltaMv;
        uint64_t idleDeltaTotalMv;
        uint32_t idleDeltaSamples;
        uint32_t checksum;
    };

    struct StoredHistoryRecord {
        HistoryRecordPayload payload;
        uint16_t checksum;
    };
#pragma pack(pop)

    bool loadState();
    bool saveState(const PackMonitor& monitor, const PackSnapshot& snapshot);
    void scanHistory();
    bool appendHistory(const PackMonitor& monitor, const PackSnapshot& snapshot, uint16_t telemetryFlags);
    bool shouldRecordHistory(const PackSnapshot& snapshot, uint16_t telemetryFlags, uint32_t nowMs) const;
    uint32_t epochForUptime(uint32_t uptimeSec) const;
    static uint32_t checksum(const void* bytes, size_t length);

    static constexpr uint32_t stateMagic = 0x50425733; // "PBW3"
    static constexpr uint16_t stateVersion = 1;
    static constexpr uint32_t flashStart = 0x000E8000;
    static constexpr uint32_t flashSize = 0x00018000;
    static constexpr uint16_t historyCapacity = 2000;
    static constexpr uint32_t flashWriteSpacingMs = 2000;

    FlashIAPBlockDevice blockDevice;
    mbed::LittleFileSystem fileSystem;
    State state = {};
    bool mounted = false;
    uint32_t oldestSequence = 0;
    uint32_t latestSequence = 0;
    uint32_t nextSequence = 1;
    uint32_t lastCheckpointMs = 0;
    uint32_t lastHistoryMs = 0;
    uint32_t lastFlashWriteMs = 0;
    uint32_t lastIdleDeltaSampleMs = 0;
    uint32_t lastHealthUpdateMs = 0;
    uint32_t syncedUnixTime = 0;
    uint32_t syncedUptimeSec = 0;
    uint16_t lastHistoryFlags = 0;
    uint16_t lastHistoryFaults = 0;
    PackState lastHistoryState = PackState::Starting;
    bool hasHistoryBaseline = false;
    bool stateCheckpointPending = false;
    bool lastChargeComplete = false;
    bool lastAtAnchor = false;
    float totalDischargedMah = 0.0f;
    float totalEnergyWh = 0.0f;
    float calibrationDischargedMah = 0.0f;
    uint32_t hotRemainderMs = 0;
};

#endif
