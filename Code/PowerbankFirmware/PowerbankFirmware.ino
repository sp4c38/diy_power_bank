/*
This code is very specific to this project and only supports the used components in the
given configuration.
*/

#include <algorithm>
#include <ArduinoLog.h>
#include <cstdint>
#include <functional>
#include <stdlib.h>
#include <unordered_map>
#include <vector>
#include <Wire.h>

#include "batteryPack.h"
#include "bleApp.h"
#include "register.h"
#include "utils.h"

BatteryPack pack;

bool bqOnline = false;
bool faultHandled = false;
bool chargeManuallyDisabled = false;
bool dischargeManuallyDisabled = false;
// BLEApp bleApp;

bool initializeBq();
bool refreshPackReadings();
void setBqOffline(const char* reason);
void handleSerialCommand();
void printHelp();
bool printStatus();
bool printFaults();
bool clearFaults();
bool disableBalancing();
bool setChargeEnabled(bool enabled);
bool setDischargeEnabled(bool enabled);
void handleSysStatusFault(BatteryPack &pack);
void performBatteryState(BatteryPack &pack);
void performBalancing(BatteryPack &pack, std::pair<const uint8_t, uint16_t> &minVoltage);

// Runs once
void setup() {
    Serial.begin(9600);
    while(!Serial && !Serial.available()){}
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);

    // bleApp.setup();
    Wire.begin(); // Must be executed inside the sketch .ino file. Moving this into another included file made the Arduino crash.
	Wire.setClock(100000);

    Log.noticeln("\n************* The Last Minute Life Saver Power Bank *************");
    Log.noticeln("Assuming that the BQ76920 device is booted.");
    Log.noticeln("Type 'help' for serial commands.");

    bqOnline = initializeBq();
}

// Runs repeatedly
void loop() {
    handleSerialCommand();

    if (!bqOnline) {
        static unsigned long offlinePreviousMillis = 0;
        every(5000, &offlinePreviousMillis, []() {
            Log.warningln("BQ76920 offline. Type 'status' after booting the BQ, or reset the Arduino.");
        });
        delay(500);
        return;
    }

    if (pack.state.currentState == BatteryState::State::SHIPMode) {
        Log.noticeln("Pack in SHIP mode.");
        delay(3000);
        return;
    }

    if (!refreshPackReadings()) {
        setBqOffline("BQ update failed.");
        delay(500);
        return;
    }

    if (!(pack.temp >= -35.0f && pack.temp <= 60.0f)) {
        Log.warning("Internal die temperature outside of operating bounds: %F.", pack.temp);
        if (!pack.transitionToSHIPMode()) {
            setBqOffline("Could not enter SHIP mode after temperature fault.");
        }
        return;
    }
    
    static unsigned long statePreviousMillis = 0;
    every(5000, &statePreviousMillis, []() {
        Log.noticeln("Pack state: %s; DSG: %T; CHG: %T; pack current: %d; pack voltage: %d (Cell 1: %d; Cell 2: %d; Cell 5: %d)", pack.state.getStringRepresentation().c_str(), pack.sysControl2[SysControlOpt::DSG_ON], pack.sysControl2[SysControlOpt::CHG_ON], 
                    pack.current, pack.voltage, pack.voltages[registerMap::VC1_HI], pack.voltages[registerMap::VC2_HI], pack.voltages[registerMap::VC5_HI]);
    });

    if (pack.sysStatus[SysStatusOpt::UV] == true || pack.sysStatus[SysStatusOpt::OV] == true ||
        pack.sysStatus[SysStatusOpt::SCD] == true || pack.sysStatus[SysStatusOpt::OCD] == true)
    {
        if (!faultHandled) {
            Log.noticeln("Fault occurred.");
            handleSysStatusFault(pack);
            faultHandled = true;
        }
        delay(500);
        return;
    } else {
        if (faultHandled == true) {
            Log.noticeln("Fault cleared.");
            faultHandled = false;
        }
    }

    performBatteryState(pack);
 
    // Keep at end of loop function
    // bleApp.loop();

    delay(500);
}

bool initializeBq() {
    // Write CC_CFG register
    if (!writeRegister(I2C_BQ76920_ADDRESS, registerMap::CC_CFG, 0x19)) {
        setBqOffline("Could not write CC_CFG.");
        return false;
    }

    uint8_t ccConfig = 0;
    if (!readRegister(I2C_BQ76920_ADDRESS, registerMap::CC_CFG, &ccConfig) || ccConfig != 0x19) { // Check if writing was successful by reading CC_CFG back again.
        setBqOffline("CC_CFG doesn't contain the expected data.");
        return false;
    }
    Log.noticeln("CC_CFG verification successful. Connection established.");
    delay(250); // 250ms delay because of the transition from NORMAL mode to SHIP mode

    if (!pack.readOffsetAndGain()) {
        setBqOffline("Could not read ADC offset and gain.");
        return false;
    }
    Log.verboseln("ADC offset: %d; ADC gain: %d", pack.adcOffset, pack.adcGain);

    // Configure protection before turning on CHG and DSG because downstream capacitors can draw a short startup current pulse.
    if (!pack.pushProtection() || !pack.pushControl() || !pack.pushBalancing()) {
        setBqOffline("Could not push startup configuration.");
        return false;
    }

    bqOnline = true;
    chargeManuallyDisabled = false;
    dischargeManuallyDisabled = false;
    Log.noticeln("Start up complete.\n");
    return true;
}

bool refreshPackReadings() {
    // Local copies of registers must be updated to reflect possible changes the IC has made.
    return pack.updateTemperature() &&
        pack.updateSysStatus() &&
        pack.updateSysControl() &&
        pack.updateVoltages() &&
        pack.updateCurrent();
}

void setBqOffline(const char* reason) {
    bqOnline = false;
    pack.sysControl2[SysControlOpt::DSG_ON] = false;
    pack.sysControl2[SysControlOpt::CHG_ON] = false;
    chargeManuallyDisabled = true;
    dischargeManuallyDisabled = true;
    disableBalancing();
    Log.errorln("%s", reason);
    Log.warningln("BQ76920 marked offline. Serial monitor remains active.");
}

void handleSerialCommand() {
    if (!Serial.available()) {
        return;
    }

    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();

    if (command.length() == 0) {
        return;
    } else if (command == "help") {
        printHelp();
    } else if (command == "status") {
        printStatus();
    } else if (command == "faults") {
        printFaults();
    } else if (command == "clear_faults" || command == "clear_scd") {
        clearFaults();
    } else if (command == "ship" || command == "ship_mode") {
        if (!bqOnline) {
            Log.warningln("Cannot enter SHIP mode because the BQ76920 is offline.");
        } else if (!pack.transitionToSHIPMode()) {
            setBqOffline("SHIP mode command failed.");
        }
    } else if (command == "charge_on") {
        setChargeEnabled(true);
    } else if (command == "charge_off") {
        setChargeEnabled(false);
    } else if (command == "discharge_on") {
        setDischargeEnabled(true);
    } else if (command == "discharge_off") {
        setDischargeEnabled(false);
    } else if (command == "balance_off") {
        if (!disableBalancing()) {
            setBqOffline("Could not disable balancing.");
        }
    } else if (command == "balance") {
        Log.warningln("Manual balancing is disabled for now. Use 'balance_off' to make sure all balancing is off.");
    } else {
        Log.warningln("Unknown command '%s'. Type 'help'.", command.c_str());
    }
}

void printHelp() {
    Log.noticeln("Commands: help, status, faults, clear_faults, ship, charge_on, charge_off, discharge_on, discharge_off, balance_off");
}

bool printStatus() {
    if (!bqOnline && !initializeBq()) {
        Log.warningln("Status unavailable because the BQ76920 is offline.");
        return false;
    }
    if (!refreshPackReadings()) {
        setBqOffline("Status update failed.");
        return false;
    }
    Log.noticeln("Status: state: %s; DSG: %T; CHG: %T; manual charge off: %T; manual discharge off: %T; current: %d mA; voltage: %d mV; temp: %F C; Cell 1: %d mV; Cell 2: %d mV; Cell 5: %d mV",
        pack.state.getStringRepresentation().c_str(),
        pack.sysControl2[SysControlOpt::DSG_ON],
        pack.sysControl2[SysControlOpt::CHG_ON],
        chargeManuallyDisabled,
        dischargeManuallyDisabled,
        pack.current,
        pack.voltage,
        pack.temp,
        pack.voltages[registerMap::VC1_HI],
        pack.voltages[registerMap::VC2_HI],
        pack.voltages[registerMap::VC5_HI]);
    return true;
}

bool printFaults() {
    if (!bqOnline && !initializeBq()) {
        Log.warningln("Fault status unavailable because the BQ76920 is offline.");
        return false;
    }
    if (!pack.updateSysStatus()) {
        setBqOffline("Fault status update failed.");
        return false;
    }
    Log.noticeln("Faults: UV: %T; OV: %T; SCD: %T; OCD: %T; OVRD_ALERT: %T; CC_READY: %T",
        pack.sysStatus[SysStatusOpt::UV],
        pack.sysStatus[SysStatusOpt::OV],
        pack.sysStatus[SysStatusOpt::SCD],
        pack.sysStatus[SysStatusOpt::OCD],
        pack.sysStatus[SysStatusOpt::OVRD_ALERT],
        pack.sysStatus[SysStatusOpt::CC_READY]);
    return true;
}

bool clearFaults() {
    if (!bqOnline) {
        Log.warningln("Cannot clear faults because the BQ76920 is offline.");
        return false;
    }
    const uint8_t faultMask = (1 << (uint8_t) SysStatusOpt::UV) |
        (1 << (uint8_t) SysStatusOpt::OV) |
        (1 << (uint8_t) SysStatusOpt::SCD) |
        (1 << (uint8_t) SysStatusOpt::OCD);

    if (!writeRegister(I2C_BQ76920_ADDRESS, registerMap::SYS_STAT, faultMask)) {
        setBqOffline("Could not clear fault bits.");
        return false;
    }
    faultHandled = false;
    Log.noticeln("Fault clear command sent.");
    return printFaults();
}

bool disableBalancing() {
    for (auto &entry : pack.balanceCells) {
        entry.second = false;
    }
    if (!bqOnline) {
        return true;
    }
    return pack.pushBalancing();
}

bool setChargeEnabled(bool enabled) {
    if (!bqOnline) {
        Log.warningln("Cannot change charge FET because the BQ76920 is offline.");
        return false;
    }
    chargeManuallyDisabled = !enabled;
    pack.sysControl2[SysControlOpt::CHG_ON] = enabled;
    if (!pack.pushControl()) {
        setBqOffline("Could not update charge FET state.");
        return false;
    }
    Log.noticeln("Charge FET: %T", enabled);
    return true;
}

bool setDischargeEnabled(bool enabled) {
    if (!bqOnline) {
        Log.warningln("Cannot change discharge FET because the BQ76920 is offline.");
        return false;
    }
    dischargeManuallyDisabled = !enabled;
    pack.sysControl2[SysControlOpt::DSG_ON] = enabled;
    if (!pack.pushControl()) {
        setBqOffline("Could not update discharge FET state.");
        return false;
    }
    Log.noticeln("Discharge FET: %T", enabled);
    return true;
}

// Turns off charge, discharge and balancing.
void handleSysStatusFault(BatteryPack &pack) {
    // Does the same for UV, OV, SCD and OCD events.
    std::map<SysStatusOpt, bool> &status = pack.sysStatus;
    Log.error("System status fault detected. ");
    Log.error("[UV: %T, OV: %T] ", status[SysStatusOpt::UV], status[SysStatusOpt::OV]);
    Log.errorln("[SCD: %T, OCD: %T]", status[SysStatusOpt::SCD], status[SysStatusOpt::OCD]);
    pack.sysControl2[SysControlOpt::DSG_ON] = false;
    pack.sysControl2[SysControlOpt::CHG_ON] = false;
    if (!pack.pushControl()) {
        setBqOffline("Could not disable FETs after system status fault.");
        return;
    }
    for (auto &entry : pack.balanceCells) {
        entry.second = false;
    }
    if (!pack.pushBalancing()) {
        setBqOffline("Could not disable balancing after system status fault.");
    }
}

void performBatteryState(BatteryPack &pack) {
    if (pack.state.currentState != BatteryState::State::Balancing) {
        if (pack.current > 0) {
            if (pack.state.currentState != BatteryState::State::Charging) {
                pack.state.setState(BatteryState::State::Charging);
                pack.state.setChargingMode(BatteryState::ChargingMode::ConstantCurrent);
                Log.noticeln("Charging enabled by starting with the CC (constant current) phase.");
            }
        }
        if (pack.state.currentState != BatteryState::State::Charging) {
            if (pack.current < dischargingThreshold) {
                pack.state.setState(BatteryState::State::Discharging);
            } else {
                pack.state.setState(BatteryState::State::Idle);
            }
        }
    }
    
    bool &dsgOn = pack.sysControl2[SysControlOpt::DSG_ON];
    bool &chgOn = pack.sysControl2[SysControlOpt::CHG_ON];
    
    std::pair<const uint8_t, uint16_t> &minVoltage = *min_element(pack.voltages.begin(), pack.voltages.end(),
        [](const auto& l, const auto& r) { return l.second < r.second; });
    std::pair<const uint8_t, uint16_t> &maxVoltage = *std::max_element(pack.voltages.begin(), pack.voltages.end(),
        [](const auto& l, const auto& r) { return l.second < r.second; });
    
    // Check lower voltage limits
    if (dsgOn == true && minVoltage.second <= lowerVoltageLimit) {
        Log.noticeln("Turning off discharging, minimum voltage (%d) is <= lower voltage limit (%d).", minVoltage.second, lowerVoltageLimit);
        dsgOn = false;
        if (!pack.pushControl()) {
            setBqOffline("Could not turn off discharging at lower voltage limit.");
            return;
        }
    }
    if ((pack.state.currentState == BatteryState::State::Idle || pack.state.currentState == BatteryState::State::Charging)
        && dsgOn == false && minVoltage.second >= lowerVoltageLimit+400 && !dischargeManuallyDisabled) {
        Log.noticeln("Turning discharging back on.");
        dsgOn = true;
        if (!pack.pushControl()) {
            setBqOffline("Could not turn discharging back on.");
            return;
        }
    }
    
    if (pack.state.currentState == BatteryState::State::Charging) {
        if (pack.state.chargingMode == BatteryState::ChargingMode::ConstantCurrent) {
            if (maxVoltage.second >= upperVoltageLimit) {
                Log.noticeln("Turning off charging, maximum voltage (%d) is >= upper voltage limit (%d).", maxVoltage.second, upperVoltageLimit);
                chgOn = false;
                dsgOn = false;
                if (!pack.pushControl()) {
                    setBqOffline("Could not turn off charging at upper voltage limit.");
                    return;
                }
                disableBalancing();
                pack.state.setState(BatteryState::State::Idle);
            }
        }
        if (pack.state.chargingMode == BatteryState::ChargingMode::ConstantVoltage) {
            if (chgOn == false && !chargeManuallyDisabled) {
                Log.noticeln("Turning charging back on. Now performing the CV (constant voltage) phase.");
                chgOn = true;
                if (!pack.pushControl()) {
                    setBqOffline("Could not turn charging back on for CV phase.");
                }
                return;
            }
            if (pack.current <= cvCurrentCutOff) {
                Log.noticeln("Charging completed as pack current %d is smaller or equal the cut off current of %d.", pack.current, cvCurrentCutOff);
                pack.state.setState(BatteryState::State::Idle);
                chgOn = false;
                dsgOn = !dischargeManuallyDisabled;
                if (!pack.pushControl()) {
                    setBqOffline("Could not update FETs after charge completion.");
                    return;
                }
            }
        }
    }
    if ((pack.state.currentState == BatteryState::State::Idle || pack.state.currentState == BatteryState::State::Discharging) 
        && chgOn == false && maxVoltage.second <=  upperVoltageLimit-400 && !chargeManuallyDisabled)
    {
        Log.noticeln("Turning charging back on.");
        chgOn = true;
        if (!pack.pushControl()) {
            setBqOffline("Could not turn charging back on.");
            return;
        }
    }
    
    if (pack.state.currentState == BatteryState::State::Balancing) {
        Log.warningln("Automatic balancing is disabled for now. Turning all balancing off.");
        disableBalancing();
        pack.state.setState(BatteryState::State::Idle);
    }
}

// Perform balancing if necessary
void performBalancing(BatteryPack &pack, std::pair<const uint8_t, uint16_t> &minVoltage) {
    // Check if we can turn off balancing for any cells that have balancing already turned on.
    bool anyCellAlreadyBalancing = false;
    for (auto &cell : pack.balanceCells) {
        if (cell.second == true) {
            uint8_t voltageCell = balanceCellToVoltageCell.at(cell.first);
            unsigned int difference = pack.voltages[voltageCell] - minVoltage.second;
            if (difference <= balancingDifference) {
                Log.noticeln("Turning off balancing for %s.", balanceCellToStringName.at(cell.first).c_str());
                cell.second = false;
                if (!pack.pushBalancing()) {
                    setBqOffline("Could not update balancing state.");
                    return;
                }
            } else {
                anyCellAlreadyBalancing = true;
            }
        }
    }

    if (anyCellAlreadyBalancing) {
        return;
    }

    // Get cells that need balancing
    std::vector<std::pair<uint8_t, uint16_t>> cellsToBalance; // List of all cells that need balancing (except the lowest) and their voltage difference to the lowest cell.
    for (auto &voltage : pack.voltages) {
        if (voltage.first == minVoltage.first) { continue; } // Skip minimum voltage cell.
        unsigned int difference = voltage.second - minVoltage.second;
        if (difference > allowedBalancingDifference) {
            cellsToBalance.push_back(voltage);
        }
    }

    if (cellsToBalance.size() == 0) {
        Log.noticeln("All cells are balanced. Transitioning to charging state with contant voltage mode.");
        pack.state.setState(BatteryState::State::Charging);
        pack.state.setChargingMode(BatteryState::ChargingMode::ConstantVoltage);
        return;
    } else if (cellsToBalance.size() == 1) {
        BalanceOpt balanceOpt = voltageCellToBalanceOpt.at(cellsToBalance[0].first);
        Log.noticeln("Starting to balance single %s.", balanceCellToStringName.at(balanceOpt).c_str());
        pack.balanceCells[balanceOpt] = true;
        if (!pack.pushBalancing()) {
            setBqOffline("Could not start balancing.");
        }
    } else if (cellsToBalance.size() == 2) {
        BalanceOpt cell1 = voltageCellToBalanceOpt.at(cellsToBalance[0].first);
        BalanceOpt cell2 = voltageCellToBalanceOpt.at(cellsToBalance[1].first);

        if (pack.checkIfCellsAreAdjacent(cell1, cell2)) {
            Log.noticeln("Need to balance adjacent cells but as this isn't possible, first starting to balance %s.", balanceCellToStringName.at(cell1).c_str());
            pack.balanceCells[cell1] = true;
        } else {
            Log.noticeln("Balancing %s and %s at once. They aren't adjacent.", balanceCellToStringName.at(cell1).c_str(), balanceCellToStringName.at(cell2).c_str());
            pack.balanceCells[cell1] = true;
            pack.balanceCells[cell2] = true;
        }
        if (!pack.pushBalancing()) {
            setBqOffline("Could not start balancing.");
        }
    } else {
        Log.errorln("Detected that more than 2 cells require balancing. This should never happen!");
        return;
    }
}
