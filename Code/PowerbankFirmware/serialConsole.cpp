#include <ArduinoLog.h>

#include "serialConsole.h"
#include "utils.h"

void SerialConsole::begin() {
    Serial.setTimeout(60);
}

void SerialConsole::poll(CommandHandler handler, const PackMonitor& monitor, const ControlState& controls, const Bq76920Driver& driver) {
    if (!Serial.available()) {
        return;
    }

    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    if (command.length() == 0) {
        return;
    }

    if (command == "help") {
        printHelp();
        return;
    }
    if (command == "status") {
        printStatus(monitor.snapshot(), controls, monitor.socPercent());
        return;
    }
    if (command == "faults") {
        printFaults(monitor.snapshot());
        return;
    }
    if (command == "raw") {
        printRaw(monitor, driver);
        return;
    }

    bool confirmed = false;
    CommandId id = commandFromString(command, confirmed);
    char result[80] = "";
    if (id == CommandId::None) {
        Log.warningln("Unknown command '%s'. Type 'help'.", command.c_str());
        return;
    }
    if (handler != nullptr && handler(id, confirmed, result, sizeof(result))) {
        Log.noticeln("%s", result);
    } else {
        Log.warningln("%s", result[0] == '\0' ? "Command failed." : result);
    }
}

CommandId SerialConsole::commandFromString(String command, bool& confirmed) {
    confirmed = command.endsWith("!");
    if (confirmed) {
        command.remove(command.length() - 1);
    }
    if (command == "output_on" || command == "discharge_on") {
        return CommandId::OutputOn;
    }
    if (command == "output_off" || command == "discharge_off") {
        return CommandId::OutputOff;
    }
    if (command == "clear_faults" || command == "clear_scd") {
        return CommandId::ClearFaults;
    }
    if (command == "ship" || command == "ship_mode") {
        return CommandId::Ship;
    }
    if (command == "balance_off") {
        return CommandId::BalanceOff;
    }
    if (command == "charge_on") {
        return CommandId::ChargeOn;
    }
    if (command == "charge_off") {
        return CommandId::ChargeOff;
    }
    if (command == "raw_diag") {
        return CommandId::RawDiagnostics;
    }
    return CommandId::None;
}

void SerialConsole::printHelp() {
    Log.noticeln("Commands: help, status, faults, raw, output_on, output_off, clear_faults, balance_off, ship!, charge_on, charge_off");
}

void SerialConsole::printStatus(const PackSnapshot& snapshot, const ControlState& controls, uint8_t socPercent) {
    Log.noticeln("Status: state=%d trusted=%T SOC=%d%% DSG=%T CHG=%T idleOff=%T manualChargeOff=%T manualDischargeOff=%T current=%d mA pack=%d mV temp=%d.%02d C Cell 1=%d mV Cell 2=%d mV Cell 5=%d mV",
        (uint8_t) snapshot.state,
        snapshot.trusted,
        socPercent,
        (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::DSG_ON)) != 0,
        (snapshot.sysCtrl2 & (1 << (uint8_t) SysControlOpt::CHG_ON)) != 0,
        controls.idleOutputOff,
        controls.chargeManuallyDisabled,
        controls.dischargeManuallyDisabled,
        snapshot.currentMa,
        snapshot.packMv,
        snapshot.dieTempCentiC / 100,
        abs(snapshot.dieTempCentiC % 100),
        snapshot.cell1Mv,
        snapshot.cell2Mv,
        snapshot.cell5Mv);
}

void SerialConsole::printFaults(const PackSnapshot& snapshot) {
    Log.noticeln("Faults: flags=0x%X BQ_UV=%T BQ_OV=%T BQ_SCD=%T BQ_OCD=%T xready=%T sensor=%T temp=%T bqOffline=%T lowCell=%T",
        snapshot.faultFlags,
        (snapshot.faultFlags & FAULT_BQ_UV) != 0,
        (snapshot.faultFlags & FAULT_BQ_OV) != 0,
        (snapshot.faultFlags & FAULT_BQ_SCD) != 0,
        (snapshot.faultFlags & FAULT_BQ_OCD) != 0,
        (snapshot.faultFlags & FAULT_BQ_XREADY) != 0,
        (snapshot.faultFlags & FAULT_SENSOR) != 0,
        (snapshot.faultFlags & FAULT_TEMP) != 0,
        (snapshot.faultFlags & FAULT_BQ_OFFLINE) != 0,
        (snapshot.faultFlags & FAULT_OUTPUT_LOW_CELL) != 0);
}

void SerialConsole::printRaw(const PackMonitor& monitor, const Bq76920Driver& driver) {
    const BqRawReadings& raw = monitor.raw();
    Log.noticeln("Raw: VC1=%d VC2=%d VC5=%d BAT=%d CC=%d TEMP=%d SYS_STAT=0x%X SYS_CTRL1=0x%X SYS_CTRL2=0x%X CELLBAL=0x%X ADC_OFFSET=%d ADC_GAIN=%d",
        raw.vc1Raw,
        raw.vc2Raw,
        raw.vc5Raw,
        raw.batRaw,
        raw.ccRaw,
        raw.tempRaw,
        raw.sysStat,
        raw.sysCtrl1,
        raw.sysCtrl2,
        raw.cellBal,
        driver.adcOffset(),
        driver.adcGain());
}
