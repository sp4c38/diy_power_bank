/*
  The Last Minute Life Saver Power Bank

  Hobby-grade supervisor firmware for the Arduino Nano 33 BLE + BQ76920 PCB.
  The BQ76920 remains the protection AFE; this firmware owns policy, telemetry,
  idle output control, guarded commands, and BLE.
*/

#include <ArduinoLog.h>
#include <Wire.h>

#include "balancer.h"
#include "bleApp.h"
#include "bq76920Driver.h"
#include "packMonitor.h"
#include "powerManager.h"
#include "safetyPolicy.h"
#include "serialConsole.h"
#include "utils.h"

Bq76920Driver bq;
PackMonitor monitor;
SafetyPolicy safetyPolicy;
Balancer balancer;
PowerManager powerManager;
BLEApp bleApp;
SerialConsole serialConsole;
ControlState controls;
PolicyDecision lastDecision;

bool bqOnline = false;
bool bleOnline = false;

bool handleCommand(CommandId command, bool confirmed, char* result, size_t resultSize);
bool initializeBq();
void refreshAndApply();
void applyDecision(const PolicyDecision& decision);
void setBqOffline(const char* reason);
void logPeriodicStatus();

void setup() {
    Serial.begin(9600);
    unsigned long serialWaitStartMs = millis();
    while (!Serial && millis() - serialWaitStartMs < 3000) {
        delay(10);
    }
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    serialConsole.begin();

    Wire.begin();
    Wire.setClock(100000);
    pinMode(ALERT_PIN, INPUT);

    Log.noticeln("\n************* The Last Minute Life Saver Power Bank *************");
    Log.noticeln("Firmware %s; BLE protocol %d", FIRMWARE_VERSION, BLE_PROTOCOL_VERSION);
    Log.noticeln("Type 'help' for serial commands.");

    bqOnline = initializeBq();
    bleOnline = bleApp.begin();
}

void loop() {
    serialConsole.poll(handleCommand, monitor, controls, bq);
    if (bleOnline) {
        bleApp.poll(handleCommand);
    }

    refreshAndApply();

    if (bleOnline) {
        bleApp.update(monitor.snapshot(), monitor.socPercent(), balancer.active(), bleApp.connected());
    }

    logPeriodicStatus();
    delay(250);
}

bool initializeBq() {
    if (!bq.begin()) {
        setBqOffline("BQ76920 initialization failed.");
        return false;
    }
    controls = ControlState();
    Log.noticeln("BQ76920 startup complete.");
    return true;
}

void refreshAndApply() {
    if (!bq.isOnline()) {
        monitor.markBqOffline();
        bqOnline = false;
        return;
    }

    if (!monitor.update(bq)) {
        setBqOffline("BQ76920 update failed.");
        return;
    }

    powerManager.update(monitor.snapshot(), controls);
    lastDecision = safetyPolicy.evaluate(monitor.snapshot(), controls);
    monitor.applyPolicy(lastDecision.state, lastDecision.faults);
    applyDecision(lastDecision);
}

void applyDecision(const PolicyDecision& decision) {
    PackSnapshot snapshot = monitor.snapshot();

    if (decision.requestShip) {
        Log.warningln("Entering SHIP mode.");
        bq.enterShipMode();
        controls.shipRequested = false;
        return;
    }

    bool chgOn = (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::CHG_ON)) != 0;
    bool dsgOn = (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::DSG_ON)) != 0;

    if (chgOn != decision.allowCharge) {
        if (!bq.setChargeEnabled(decision.allowCharge)) {
            setBqOffline("Failed to update charge FET.");
            return;
        }
    }
    if (dsgOn != decision.allowDischarge) {
        if (!bq.setDischargeEnabled(decision.allowDischarge)) {
            setBqOffline("Failed to update discharge FET.");
            return;
        }
    }

    if (!balancer.update(bq, snapshot, decision.allowBalancing)) {
        setBqOffline("Failed to update balancing.");
        return;
    }
}

bool handleCommand(CommandId command, bool confirmed, char* result, size_t resultSize) {
    if (resultSize > 0) {
        result[0] = '\0';
    }

    switch (command) {
        case CommandId::OutputOn:
            if (!bq.isOnline()) {
                snprintf(result, resultSize, "Output blocked: BQ offline");
                return false;
            }
            if (!monitor.snapshot().trusted) {
                snprintf(result, resultSize, "Output blocked: readings untrusted");
                return false;
            }
            powerManager.noteOutputOn(controls);
            controls.dischargeManuallyDisabled = false;
            snprintf(result, resultSize, "Output on requested");
            return true;

        case CommandId::OutputOff:
            controls.dischargeManuallyDisabled = true;
            powerManager.noteOutputOff(controls);
            bq.setDischargeEnabled(false);
            snprintf(result, resultSize, "Output off");
            return true;

        case CommandId::ClearFaults:
            if (!bq.clearFaults()) {
                snprintf(result, resultSize, "Failed to clear faults");
                return false;
            }
            controls.dischargeLatchedOff = false;
            controls.chargeLatchedOff = false;
            snprintf(result, resultSize, "Fault clear sent");
            return true;

        case CommandId::Ship:
            if (!confirmed) {
                snprintf(result, resultSize, "SHIP requires confirmation");
                return false;
            }
            controls.shipRequested = true;
            snprintf(result, resultSize, "SHIP requested");
            return true;

        case CommandId::BalanceOff:
            if (!balancer.stop(bq)) {
                snprintf(result, resultSize, "Failed to disable balancing");
                return false;
            }
            snprintf(result, resultSize, "Balancing off");
            return true;

        case CommandId::ChargeOn:
            if (!confirmed) {
                snprintf(result, resultSize, "charge_on requires developer confirmation");
                return false;
            }
            controls.chargeManuallyDisabled = false;
            snprintf(result, resultSize, "Charge enabled if safe");
            return true;

        case CommandId::ChargeOff:
            controls.chargeManuallyDisabled = true;
            bq.setChargeEnabled(false);
            snprintf(result, resultSize, "Charge off");
            return true;

        case CommandId::DischargeOn:
            if (!confirmed) {
                snprintf(result, resultSize, "discharge_on requires developer confirmation");
                return false;
            }
            controls.dischargeManuallyDisabled = false;
            controls.idleOutputOff = false;
            snprintf(result, resultSize, "Discharge enabled if safe");
            return true;

        case CommandId::DischargeOff:
            controls.dischargeManuallyDisabled = true;
            bq.setDischargeEnabled(false);
            snprintf(result, resultSize, "Discharge off");
            return true;

        case CommandId::RawDiagnostics:
            snprintf(result, resultSize, "Raw diagnostics available over serial");
            return true;

        case CommandId::None:
        default:
            snprintf(result, resultSize, "Unknown command");
            return false;
    }
}

void setBqOffline(const char* reason) {
    bq.markOffline();
    monitor.markBqOffline();
    controls.chargeManuallyDisabled = true;
    controls.dischargeManuallyDisabled = true;
    controls.idleOutputOff = true;
    bqOnline = false;
    Log.errorln("%s", reason);
}

void logPeriodicStatus() {
    static unsigned long previousLogMs = 0;
    every(5000, &previousLogMs, []() {
        const PackSnapshot& snapshot = monitor.snapshot();
        Log.noticeln("Pack state: %d; trusted: %T; DSG: %T; CHG: %T; current: %d mA; pack: %d mV; Cell 1: %d mV; Cell 2: %d mV; Cell 5: %d mV; faults: 0x%X",
            (uint8_t) snapshot.state,
            snapshot.trusted,
            (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::DSG_ON)) != 0,
            (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::CHG_ON)) != 0,
            snapshot.currentMa,
            snapshot.packMv,
            snapshot.cell1Mv,
            snapshot.cell2Mv,
            snapshot.cell5Mv,
            snapshot.faultFlags);
    });
}
