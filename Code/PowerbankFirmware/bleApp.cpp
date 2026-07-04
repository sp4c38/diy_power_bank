#include <ArduinoLog.h>
#include <string.h>

#include "bleApp.h"
#include "persistentStore.h"

BLEApp::BLEApp():
    service(bleUuid::service),
    telemetryCharacteristic(bleUuid::telemetry, BLERead | BLENotify, sizeof(TelemetryPayload)),
    commandCharacteristic(bleUuid::command, BLEWrite, sizeof(CommandPayload)),
    commandResultCharacteristic(bleUuid::commandResult, BLERead | BLENotify, 80),
    deviceInfoCharacteristic(bleUuid::deviceInfo, BLERead, 80),
    historyControlCharacteristic(bleUuid::historyControl, BLERead | BLEWrite | BLENotify, sizeof(HistoryStatusPayload)),
    historyDataCharacteristic(bleUuid::historyData, BLENotify, sizeof(HistoryChunkPayload)),
    healthCharacteristic(bleUuid::health, BLERead | BLENotify, sizeof(HealthPayload)),
    timeSyncCharacteristic(bleUuid::timeSync, BLEWrite, sizeof(TimeSyncPayload)) {
}

bool BLEApp::begin() {
    if (!BLE.begin()) {
        Log.errorln("Starting BLE failed.");
        return false;
    }

    // Internal-flash erase/program operations temporarily pause interrupts on
    // the nRF52840. A longer supervision window keeps iOS connected while a
    // LittleFS commit completes. Units are 10 ms.
    BLE.setSupervisionTimeout(1200);

    service.addCharacteristic(telemetryCharacteristic);
    service.addCharacteristic(commandCharacteristic);
    service.addCharacteristic(commandResultCharacteristic);
    service.addCharacteristic(deviceInfoCharacteristic);
    service.addCharacteristic(historyControlCharacteristic);
    service.addCharacteristic(historyDataCharacteristic);
    service.addCharacteristic(healthCharacteristic);
    service.addCharacteristic(timeSyncCharacteristic);
    BLE.addService(service);

    BLE.setLocalName("Powerbank");
    BLE.setDeviceName("Powerbank");
    BLE.setAdvertisedService(service);

    char deviceInfo[80];
    snprintf(deviceInfo, sizeof(deviceInfo), "Powerbank FW %s; BLE protocol %d", FIRMWARE_VERSION, BLE_PROTOCOL_VERSION);
    deviceInfoCharacteristic.writeValue((const uint8_t*) deviceInfo, strlen(deviceInfo));
    setCommandResult("Ready");
    updateHistoryStatus(0);
    HealthPayload emptyHealth = {};
    healthCharacteristic.writeValue((const uint8_t*) &emptyHealth, sizeof(emptyHealth));

    BLE.advertise();
    Log.noticeln("Started BLE advertising.");
    return true;
}

void BLEApp::attachStore(PersistentStore* persistentStore) {
    store = persistentStore;
    updateHistoryStatus(0);
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

    if (historyControlCharacteristic.written() && store != nullptr) {
        HistoryRequestPayload request = {};
        int bytesRead = historyControlCharacteristic.readValue((uint8_t*) &request, sizeof(request));
        if (bytesRead == sizeof(request) && request.operation == 1) {
            uint32_t oldest = store->oldestHistorySequence();
            historyNextSequence = request.afterSequence + 1;
            if (oldest > 0 && historyNextSequence < oldest) {
                historyNextSequence = oldest;
            }
            historyStreaming = historyNextSequence > 0 &&
                historyNextSequence <= store->latestHistorySequence();
            historyRecordLoaded = false;
            historyChunkIndex = 0;
            updateHistoryStatus(historyStreaming ? 1 : 2);
        } else if (bytesRead >= 1 && request.operation == 2) {
            historyStreaming = false;
            historyRecordLoaded = false;
            updateHistoryStatus(0);
        }
    }

    if (timeSyncCharacteristic.written() && store != nullptr) {
        TimeSyncPayload payload = {};
        if (timeSyncCharacteristic.readValue((uint8_t*) &payload, sizeof(payload)) == sizeof(payload)) {
            store->setTime(payload.unixTime, millis() / 1000UL);
        }
    }

    streamHistory();

    bool isConnected = connected();
    if (isConnected != wasConnected) {
        wasConnected = isConnected;
        Log.noticeln(isConnected ? "BLE central connected." : "BLE central disconnected.");
    }
}

void BLEApp::update(
    const PackSnapshot& snapshot,
    uint8_t socPercent,
    uint16_t chargeMahTenths,
    bool balancing,
    bool bleConnectedFlag,
    const ControlState& controls,
    uint16_t idleRemainingSec,
    bool automaticIdleShutdown,
    bool balanceTimedOut
) {
    unsigned long interval = connected() ? 1000UL : 5000UL;
    if (millis() - lastNotifyMs < interval && lastNotifyMs != 0) {
        return;
    }
    lastNotifyMs = millis();
    TelemetryPayload payload = buildPayload(
        snapshot,
        socPercent,
        chargeMahTenths,
        balancing,
        bleConnectedFlag,
        controls,
        idleRemainingSec,
        automaticIdleShutdown,
        balanceTimedOut
    );
    telemetryCharacteristic.writeValue((uint8_t*) &payload, sizeof(payload));
    if (store != nullptr && store->available()) {
        HealthPayload health = store->healthPayload();
        healthCharacteristic.writeValue((const uint8_t*) &health, sizeof(health));
    }
}

TelemetryPayload BLEApp::buildPayload(
    const PackSnapshot& snapshot,
    uint8_t socPercent,
    uint16_t chargeMahTenths,
    bool balancing,
    bool bleConnectedFlag,
    const ControlState& controls,
    uint16_t idleRemainingSec,
    bool automaticIdleShutdown,
    bool balanceTimedOut
) {
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
    if (controls.chargeManuallyDisabled) {
        payload.flags |= FLAG_MANUAL_CHARGE_OFF;
    }
    if (controls.dischargeManuallyDisabled) {
        payload.flags |= FLAG_MANUAL_DISCHARGE_OFF;
    }
    if (automaticIdleShutdown) {
        payload.flags |= FLAG_IDLE_OUTPUT_OFF;
    }
    bool chargeComplete = controls.chargeLatchedOff &&
        max(snapshot.cell1Mv, max(snapshot.cell2Mv, snapshot.cell5Mv)) >= thresholds::chargeStopMv;
    if (chargeComplete) {
        payload.flags |= FLAG_CHARGE_COMPLETE;
    }
    if (balanceTimedOut) {
        payload.flags |= FLAG_BALANCE_TIMEOUT;
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
    payload.chargeMahTenths = chargeMahTenths;
    payload.idleRemainingSec = idleRemainingSec;
    return payload;
}

bool BLEApp::connected() const {
    return BLE.connected();
}

void BLEApp::setCommandResult(const char* message) {
    commandResultCharacteristic.writeValue((const uint8_t*) message, strlen(message));
}

void BLEApp::updateHistoryStatus(uint8_t stateValue) {
    HistoryStatusPayload status = {};
    status.state = stateValue;
    if (store != nullptr && store->available()) {
        status.oldestSequence = store->oldestHistorySequence();
        status.latestSequence = store->latestHistorySequence();
        status.bootId = store->bootId();
    }
    historyControlCharacteristic.writeValue((const uint8_t*) &status, sizeof(status));
}

void BLEApp::streamHistory() {
    if (!historyStreaming || store == nullptr || !historyDataCharacteristic.subscribed()) {
        return;
    }
    uint32_t latest = store->latestHistorySequence();
    const size_t chunkDataSize = 13;
    const uint8_t chunkCount =
        (sizeof(HistoryRecordPayload) + chunkDataSize - 1) / chunkDataSize;

    for (uint8_t sent = 0; sent < 4 && historyNextSequence <= latest; sent++) {
        if (!historyRecordLoaded) {
            if (!store->readHistory(historyNextSequence, historyRecord)) {
                historyNextSequence++;
                continue;
            }
            historyRecordLoaded = true;
            historyChunkIndex = 0;
        }

        HistoryChunkPayload chunk = {};
        chunk.sequence = historyRecord.sequence;
        chunk.chunkIndex = historyChunkIndex;
        chunk.chunkCount = chunkCount;
        size_t offset = (size_t) historyChunkIndex * chunkDataSize;
        size_t remaining = sizeof(HistoryRecordPayload) - offset;
        chunk.payloadLength = (uint8_t) min((size_t) chunkDataSize, remaining);
        memcpy(
            chunk.payload,
            ((const uint8_t*) &historyRecord) + offset,
            chunk.payloadLength
        );
        historyDataCharacteristic.writeValue(
            (const uint8_t*) &chunk,
            7 + chunk.payloadLength
        );
        historyChunkIndex++;
        if (historyChunkIndex >= chunkCount) {
            historyRecordLoaded = false;
            historyChunkIndex = 0;
            historyNextSequence++;
        }
    }
    if (historyNextSequence > latest && !historyRecordLoaded) {
        historyStreaming = false;
        updateHistoryStatus(2);
    }
}
