#ifndef BLE_APP_H
#define BLE_APP_H

#include <ArduinoBLE.h>

#include "packMonitor.h"
#include "register.h"
#include "safetyPolicy.h"

class PersistentStore;

class BLEApp {
public:
    BLEApp();
    bool begin();
    void attachStore(PersistentStore* persistentStore);
    void update(
        const PackSnapshot& snapshot,
        uint8_t socPercent,
        uint16_t chargeMahTenths,
        bool balancing,
        bool bleConnectedFlag,
        const ControlState& controls,
        uint16_t idleRemainingSec,
        bool automaticIdleShutdown,
        bool balanceTimedOut
    );
    void poll(CommandHandler handler);
    bool connected() const;
    void setCommandResult(const char* message);

private:
    TelemetryPayload buildPayload(
        const PackSnapshot& snapshot,
        uint8_t socPercent,
        uint16_t chargeMahTenths,
        bool balancing,
        bool bleConnectedFlag,
        const ControlState& controls,
        uint16_t idleRemainingSec,
        bool automaticIdleShutdown,
        bool balanceTimedOut
    );
    void updateHistoryStatus(uint8_t state);
    void streamHistory();

    BLEService service;
    BLECharacteristic telemetryCharacteristic;
    BLECharacteristic commandCharacteristic;
    BLECharacteristic commandResultCharacteristic;
    BLECharacteristic deviceInfoCharacteristic;
    BLECharacteristic historyControlCharacteristic;
    BLECharacteristic historyDataCharacteristic;
    BLECharacteristic healthCharacteristic;
    BLECharacteristic timeSyncCharacteristic;
    PersistentStore* store = nullptr;
    bool wasConnected = false;
    bool historyStreaming = false;
    uint32_t historyNextSequence = 0;
    HistoryRecordPayload historyRecord = {};
    uint8_t historyChunkIndex = 0;
    bool historyRecordLoaded = false;
    unsigned long lastNotifyMs = 0;
};

#endif
