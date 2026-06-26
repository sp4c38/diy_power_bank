#include <ArduinoLog.h>

#include "bleApp.h"

BLEApp::BLEApp():
    service(bleUuid::service),
    telemetryCharacteristic(bleUuid::telemetry, BLERead | BLENotify, sizeof(TelemetryPayload)),
    commandCharacteristic(bleUuid::command, BLEWrite, sizeof(CommandPayload)),
    commandResultCharacteristic(bleUuid::commandResult, BLERead | BLENotify, 80),
    deviceInfoCharacteristic(bleUuid::deviceInfo, BLERead, 80) {
}

bool BLEApp::begin() {
    if (!BLE.begin()) {
        Log.errorln("Starting BLE failed.");
        return false;
    }

    service.addCharacteristic(telemetryCharacteristic);
    service.addCharacteristic(commandCharacteristic);
    service.addCharacteristic(commandResultCharacteristic);
    service.addCharacteristic(deviceInfoCharacteristic);
    BLE.addService(service);

    BLE.setLocalName("Powerbank");
    BLE.setDeviceName("Powerbank");
    BLE.setAdvertisedService(service);

    char deviceInfo[80];
    snprintf(deviceInfo, sizeof(deviceInfo), "Powerbank FW %s; BLE protocol %d", FIRMWARE_VERSION, BLE_PROTOCOL_VERSION);
    deviceInfoCharacteristic.writeValue((const uint8_t*) deviceInfo, strlen(deviceInfo));
    setCommandResult("Ready");

    BLE.advertise();
    Log.noticeln("Started BLE advertising.");
    return true;
}

void BLEApp::poll(CommandHandler handler) {
    BLE.poll();

    if (commandCharacteristic.written()) {
        CommandPayload command = {0, 0};
        int bytesRead = commandCharacteristic.readValue((uint8_t*) &command, sizeof(command));
        char result[80] = "";
        if (bytesRead < 1) {
            snprintf(result, sizeof(result), "Empty command");
        } else if (handler != nullptr && handler((CommandId) command.command, command.confirmation == 0xA5, result, sizeof(result))) {
            // Handler filled result.
        } else if (result[0] == '\0') {
            snprintf(result, sizeof(result), "Command failed");
        }
        setCommandResult(result);
    }

    bool isConnected = connected();
    if (isConnected != wasConnected) {
        wasConnected = isConnected;
        Log.noticeln(isConnected ? "BLE central connected." : "BLE central disconnected.");
    }
}

void BLEApp::update(const PackSnapshot& snapshot, uint8_t socPercent, bool balancing, bool bleConnectedFlag) {
    unsigned long interval = connected() ? 1000UL : 5000UL;
    if (millis() - lastNotifyMs < interval && lastNotifyMs != 0) {
        return;
    }
    lastNotifyMs = millis();
    TelemetryPayload payload = buildPayload(snapshot, socPercent, balancing, bleConnectedFlag);
    telemetryCharacteristic.writeValue((uint8_t*) &payload, sizeof(payload));
}

TelemetryPayload BLEApp::buildPayload(const PackSnapshot& snapshot, uint8_t socPercent, bool balancing, bool bleConnectedFlag) {
    TelemetryPayload payload;
    payload.protocolVersion = BLE_PROTOCOL_VERSION;
    payload.state = (uint8_t) snapshot.state;
    payload.flags = 0;
    if (snapshot.trusted) {
        payload.flags |= FLAG_MEASUREMENTS_TRUSTED;
    }
    if (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::CHG_ON)) {
        payload.flags |= FLAG_CHG_ON;
    }
    if (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::DSG_ON)) {
        payload.flags |= FLAG_DSG_ON;
    }
    if (snapshot.lowCellWarning) {
        payload.flags |= FLAG_LOW_CELL_WARN;
    }
    if (snapshot.stale) {
        payload.flags |= FLAG_STALE;
    }
    if (balancing) {
        payload.flags |= FLAG_BALANCING;
    }
    if (bleConnectedFlag) {
        payload.flags |= FLAG_BLE_CONNECTED;
    }
    payload.faults = snapshot.faultFlags;
    payload.balanceMask = snapshot.balanceMask;
    payload.cell1Mv = snapshot.cell1Mv;
    payload.cell2Mv = snapshot.cell2Mv;
    payload.cell5Mv = snapshot.cell5Mv;
    payload.packMv = snapshot.packMv;
    payload.currentMa = snapshot.currentMa;
    payload.dieTempCentiC = snapshot.dieTempCentiC;
    payload.socPercent = socPercent;
    payload.uptimeSec = millis() / 1000UL;
    return payload;
}

bool BLEApp::connected() const {
    return BLE.connected();
}

void BLEApp::setCommandResult(const char* message) {
    commandResultCharacteristic.writeValue((const uint8_t*) message, strlen(message));
}
