/*
  The Last Minute Life Saver Power Bank

  Hobby-grade supervisor firmware for the Arduino Nano 33 BLE + BQ76920 PCB.
  The BQ76920 remains the protection AFE; this firmware owns policy, telemetry,
  idle output control, guarded commands, and BLE.
*/

#include <ArduinoLog.h>
#include <Wire.h>
#include <mbed.h>

#include "balancer.h"
#include "bleApp.h"
#include "bq76920Driver.h"
#include "packMonitor.h"
#include "persistentStore.h"
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
PersistentStore persistentStore;
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
uint16_t telemetryFlags();
void maybeMaintenanceReboot();
void enterSystemOff();

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

    // The Nano 33 BLE power LED hangs on a GPIO and the core lights it at boot;
    // it costs about 1 mA around the clock, which the pack pays for. The RGB
    // LED is active-low, so park those pins high to keep it dark.
    pinMode(LED_PWR, OUTPUT);
    digitalWrite(LED_PWR, LOW);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    pinMode(LEDR, OUTPUT);
    digitalWrite(LEDR, HIGH);
    pinMode(LEDG, OUTPUT);
    digitalWrite(LEDG, HIGH);
    pinMode(LEDB, OUTPUT);
    digitalWrite(LEDB, HIGH);

    Log.noticeln("\n************* The Last Minute Life Saver Power Bank *************");
    Log.noticeln("Firmware %s; BLE protocol %d", FIRMWARE_VERSION, BLE_PROTOCOL_VERSION);
    Log.noticeln("Type 'help' for serial commands.");

    bqOnline = initializeBq();
    persistentStore.begin(monitor);
    persistentStore.restoreControls(controls);
    if (controls.idleOutputOff) {
        powerManager.restoreIdleElapsed(persistentStore.savedIdleElapsedSec());
    }
    bleOnline = bleApp.begin();
    bleApp.attachStore(&persistentStore);

    // Last line of defense against a hung loop or a wedged BLE stack. Started
    // after the slow init work (flash mount can reformat) so it cannot bite
    // during setup.
    mbed::Watchdog::get_instance().start(thresholds::watchdogTimeoutMs);
}

void loop() {
    mbed::Watchdog::get_instance().kick();
    serialConsole.poll(handleCommand, monitor, controls, bq);
    if (bleOnline) {
        bleApp.poll(handleCommand);
    }

    refreshAndApply();

    uint16_t idleRemainingSec = powerManager.idleRemainingSec(monitor.snapshot(), controls);
    bool chargeComplete = controls.chargeLatchedOff &&
        monitor.maxCellMv() >= thresholds::chargeStopMv;
    // Don't persist while the BQ is offline: setBqOffline() force-disables
    // everything as an in-session fail-safe, and freezing that into flash
    // would leave the output disabled across a recovery reboot.
    if (bq.isOnline()) {
        persistentStore.syncControls(controls);
    }
    persistentStore.setIdleElapsedSec(
        controls.idleOutputOff ? powerManager.idleDurationMs() / 1000UL : 0
    );
    persistentStore.update(monitor, monitor.snapshot(), telemetryFlags(), chargeComplete);

    if (bleOnline) {
        // Once the output has idled off nobody is nearby; a 2.5 s advertising
        // interval saves radio power and still connects within a few seconds.
        bleApp.setLowPowerAdvertising(controls.idleOutputOff);
        bleApp.update(
            monitor.snapshot(),
            monitor.socPercent(),
            monitor.chargeMahTenths(),
            balancer.active(),
            bleApp.connected(),
            controls,
            idleRemainingSec,
            powerManager.automaticShutdownOccurred(),
            balancer.timedOut()
        );
    }

    maybeMaintenanceReboot();
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
        persistentStore.checkpoint(monitor, snapshot);
        bq.enterShipMode();
        controls.shipRequested = false;
        // The MCU is wired to the battery ahead of the BQ FETs, so SHIP alone
        // would leave it draining the cells at milliamp level until they are
        // deep-discharged. Take the MCU down with the BQ.
        if (bleOnline) {
            bleApp.shutdown(thresholds::bleShutdownDrainMs);
        }
        enterSystemOff();
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
        Log.noticeln(
            "Discharge FET -> %s; state=%d faults=0x%X trusted=%T current=%d mA pack=%d mV",
            decision.allowDischarge ? "ON" : "OFF",
            (uint8_t) decision.state,
            decision.faults,
            snapshot.trusted,
            snapshot.currentMa,
            snapshot.packMv
        );
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
            balancer.clearTimeout();
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

        case CommandId::ResetLearnedBattery:
            if (!confirmed) {
                snprintf(result, resultSize, "Battery reset requires confirmation");
                return false;
            }
            balancer.clearTimeout();
            persistentStore.resetLearnedBattery(monitor);
            snprintf(result, resultSize, "Learned battery data reset");
            return true;

        case CommandId::None:
        default:
            snprintf(result, resultSize, "Unknown command");
            return false;
    }
}

uint16_t telemetryFlags() {
    const PackSnapshot& snapshot = monitor.snapshot();
    uint16_t flags = 0;
    if (snapshot.trusted) flags |= FLAG_MEASUREMENTS_TRUSTED;
    if (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::CHG_ON)) flags |= FLAG_CHG_ON;
    if (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::DSG_ON)) flags |= FLAG_DSG_ON;
    if (controls.chargeManuallyDisabled) flags |= FLAG_MANUAL_CHARGE_OFF;
    if (controls.dischargeManuallyDisabled) flags |= FLAG_MANUAL_DISCHARGE_OFF;
    if (powerManager.automaticShutdownOccurred()) flags |= FLAG_IDLE_OUTPUT_OFF;
    if (balancer.active()) flags |= FLAG_BALANCING;
    if (snapshot.lowCellWarning) flags |= FLAG_LOW_CELL_WARN;
    if (snapshot.stale) flags |= FLAG_STALE;
    if (bleApp.connected()) flags |= FLAG_BLE_CONNECTED;
    if (controls.chargeLatchedOff && monitor.maxCellMv() >= thresholds::chargeStopMv) {
        flags |= FLAG_CHARGE_COMPLETE;
    }
    if (balancer.timedOut()) flags |= FLAG_BALANCE_TIMEOUT;
    return flags;
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

void maybeMaintenanceReboot() {
    // Self-heal for a silently wedged BLE stack: restart once a day, but only
    // when nothing would notice — no central connected, no current flowing, no
    // balancing. Manual switch states and the very-long-idle ship countdown
    // survive the restart via the persistent store, so a reboot can neither
    // re-enable a switched-off output nor postpone SHIP.
    if (millis() < thresholds::maintenanceRebootMs) {
        return;
    }
    if (bleOnline && bleApp.connected()) {
        return;
    }
    const PackSnapshot& snapshot = monitor.snapshot();
    if (abs(snapshot.currentMa) > thresholds::idleCurrentMa || balancer.active()) {
        return;
    }
    Log.warningln("Maintenance reboot to refresh the BLE stack.");
    persistentStore.checkpoint(monitor, snapshot);
    Serial.flush();
    delay(100);
    NVIC_SystemReset();
}

void enterSystemOff() {
    Log.warningln("MCU entering deep sleep. Cycle the power switch to restart.");
    Serial.flush();
    delay(100);
    NRF_POWER->SYSTEMOFF = 1;
    // Not reached; if System OFF ever failed, the watchdog resets us instead.
    while (true) {
        delay(1000);
    }
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
