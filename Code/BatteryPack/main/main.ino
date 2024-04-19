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

bool faultHandled = false;
// BLEApp bleApp;

// Runs once
void setup() {
    Serial.begin(9600);
    while(!Serial && !Serial.available()){}
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);

    // bleApp.setup();
    Wire.begin(); // Must be executed inside the main.ino file, not any other included file etc.. Otherwise the Arduino will crash due to some reason.
	Wire.setClock(100000);

    Log.noticeln("\n************* The Last Minute Life Saver Power Bank *************");
    Log.noticeln("Starting up...");
    Log.noticeln("Please boot the BQ76920 device. Waiting 1s...");
    delay(1000);

    // Write CC_CFG register
    writeRegister(I2C_BQ76920_ADDRESS, registerMap::CC_CFG, 0x19);
    if (readRegister(I2C_BQ76920_ADDRESS, registerMap::CC_CFG) != 0x19) { // Check if writing was successful by reading CC_CFG back again.
        Log.fatalln("CC_CFG doesn't contain the expected data.");
        exit(1);
    } else {
        Log.noticeln("CC_CFG verification successful. Connection established.");
    };
    delay(250); // 250ms delay because of the transition from NORMAL mode to SHIP mode

    pack.readOffsetAndGain();
    Log.verboseln("ADC offset: %d; ADC gain: %d", pack.adcOffset, pack.adcGain);

    pack.pushControl();
    pack.pushProtection();
    pack.pushBalancing();
    Log.noticeln("Start up complete.\n");
}

// Runs repeatedly
void loop() {
    if (pack.state == BatteryState::SHIPMode) {
        Log.noticeln("Pack in SHIP mode.");
        delay(3000);
        return;
    }
    pack.updateTemperature();
    if (!(pack.temp >= -35.0f && pack.temp <= 80.0f)) {
        Log.warning("Internal die temperature outside of operating bounds: %F.", pack.temp);
        pack.transitionToSHIPMode();
        return;
    }

    // Local copies of registers must be updated to reflect possible changes the IC has made.
    pack.updateSysStatus();
    pack.updateSysControl();
    pack.updateVoltages();
    pack.updateCurrent();
    
    static unsigned long statePreviousMillis = 0;
    every(5000, &statePreviousMillis, []() {
        Log.noticeln("Pack state: %d; pack current: %d; pack voltage: %d (Cell 1: %d; Cell 2: %d; Cell 5: %d)", pack.state, pack.current, pack.voltage, 
                    pack.voltages[registerMap::VC1_HI], pack.voltages[registerMap::VC2_HI], pack.voltages[registerMap::VC5_HI]);
    });

    // Serial input
    // if (Serial.available()) {
    //     String command = Serial.readStringUntil('\n');
    //     if (command == "balance") {
    //         pack.balanceCells[BalanceOpt::CB5] = true;
    //         pack.pushBalancing();
    //     } else if (command == "clear_scd") {
    //         write
    //     }
    // }

    if (pack.sysStatus[SysStatusOpt::UV] == true || pack.sysStatus[SysStatusOpt::OV] == true ||
        pack.sysStatus[SysStatusOpt::SCD] == true || pack.sysStatus[SysStatusOpt::OCD] == true)
    {
        if (!faultHandled) {
            handleSysStatusFault(pack);
            faultHandled = true;
        }
        delay(500);
        return;
    } else { faultHandled = false; }

    performBatteryState(pack);

    // Keep at end of loop function
    // bleApp.loop();

    delay(500);
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
    pack.pushControl();
    for (auto &entry : pack.balanceCells) {
        entry.second = false;
    }
    pack.pushBalancing();
}

void performBatteryState(BatteryPack &pack) {
    if (pack.state == BatteryState::Idle && pack.current > 0) {
        pack.state = BatteryState::Charging;
    } else if (pack.state == BatteryState::Idle && pack.current < 0) {
        pack.state = BatteryState::Discharging;
    } else if (pack.state == BatteryState::Discharging && pack.current == 0) {
        pack.state = BatteryState::Idle;
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
        pack.pushControl();
    } else if (pack.state != BatteryState::Balancing && dsgOn == false && minVoltage.second >= lowerVoltageLimit+300) {
        Log.noticeln("Turning discharging back on.");
        dsgOn = true;
        pack.pushControl();
    }
    
    // Check upper voltage limits
    if (chgOn == true && maxVoltage.second >= upperVoltageLimit) {
        Log.noticeln("Turning off charging, maximum voltage (%d) is >= upper voltage limit (%d).", maxVoltage.second, upperVoltageLimit);
        chgOn = false;
        dsgOn = false; // Needs to be turned off to not interfere with balancing.
        pack.pushControl();
        pack.state = BatteryState::Balancing;
    } else if (pack.state != BatteryState::Balancing && chgOn == false && maxVoltage.second <=  upperVoltageLimit - 100) {
        Log.noticeln("Turning charging back on.");
        chgOn = true;
        pack.pushControl();
    }
    
    if (pack.state == BatteryState::Balancing) {
        performBalancing(pack, minVoltage);
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
                Log.noticeln("Turning off balancing for %s.", balanceCellToStringName.at(cell.first));
                cell.second = false;
                pack.pushBalancing();
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
        Log.noticeln("All cells are balanced. Transitioning to idle state.");
        pack.state = BatteryState::Idle;
        return;
    } else if (cellsToBalance.size() == 1) {
        BalanceOpt balanceOpt = voltageCellToBalanceOpt.at(cellsToBalance[0].first);
        Log.noticeln("Starting to balance single %s.", balanceCellToStringName.at(balanceOpt));
        pack.balanceCells[balanceOpt] = true;
        pack.pushBalancing();
    } else if (cellsToBalance.size() == 2) {
        BalanceOpt cell1 = voltageCellToBalanceOpt.at(cellsToBalance[0].first);
        BalanceOpt cell2 = voltageCellToBalanceOpt.at(cellsToBalance[1].first);

        if (pack.checkIfCellsAreAdjacent(cell1, cell2)) {
            Log.noticeln("Need to balance adjacent cells but as this isn't possible, first starting to balance %s.", balanceCellToStringName.at(cell1));
            pack.balanceCells[cell1] = true;
        } else {
            Log.noticeln("Balancing %s and %s at once. They aren't adjacent.", balanceCellToStringName.at(cell1), balanceCellToStringName.at(cell2));
            pack.balanceCells[cell1] = true;
            pack.balanceCells[cell2] = true;
        }
        pack.pushBalancing();
    } else {
        Log.errorln("Detected that more than 2 cells require balancing. This should never happen!");
        return;
    }
}