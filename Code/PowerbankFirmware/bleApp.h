#ifndef BLE_APP_H
#define BLE_APP_H

#include <ArduinoBLE.h>

#include "packMonitor.h"
#include "register.h"
#include "safetyPolicy.h"

class BLEApp {
public:
    BLEApp();
    bool begin();
    void update(const PackSnapshot& snapshot, uint8_t socPercent, bool balancing, bool bleConnectedFlag);
    void poll(CommandHandler handler);
    bool connected() const;
    void setCommandResult(const char* message);

private:
    TelemetryPayload buildPayload(const PackSnapshot& snapshot, uint8_t socPercent, bool balancing, bool bleConnectedFlag);

    BLEService service;
    BLECharacteristic telemetryCharacteristic;
    BLECharacteristic commandCharacteristic;
    BLECharacteristic commandResultCharacteristic;
    BLECharacteristic deviceInfoCharacteristic;
    bool wasConnected = false;
    unsigned long lastNotifyMs = 0;
};

#endif
