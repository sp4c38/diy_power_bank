/*
This code is very specific to this project and only supports the used components in the
given configuration.
*/

#include <algorithm>
#include <Arduino.h>
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

void handleSysStatusFault(BatteryPack &pack);

// BLEApp bleApp;

// Runs once
void setup() {
    Serial.begin(9600);
    while(!Serial && !Serial.available()){}
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);

    // bleApp.setup();

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
        Log.noticeln("Pack state: %d; pack current: %d; pack voltage: %d.", pack.state, pack.current, pack.voltage, 
                    pack.voltages[registerMap::VC1_HI], pack.voltages[registerMap::VC2_HI], pack.voltages[registerMap::VC5_HI]);
    });

    if (pack.sysStatus[SysStatusOpt::UV] == true || pack.sysStatus[SysStatusOpt::OV] == true ||
        pack.sysStatus[SysStatusOpt::SCD] == true || pack.sysStatus[SysStatusOpt::OCD] == true)
    {
        handleSysStatusFault(pack);
        return;
    }

    performBatteryState();

    // // Serial input
    // if (Serial.available()) {
    //     String command = Serial.readStringUntil('\n');
    //     if (command == "balance") {
    //         balanceCells[balanceOpt::CB1] = true;
    //         pushBalancing();
    //     }
    // }

    // Keep at end of loop function
    // bleApp.loop();

    delay(500);
}

// Turns off charge, discharge and balancing.
void handleSysStatusFault(BatteryPack &pack) {
    // Does the same for UV, OV, SCD and OCD events.
    std::map<SysStatusOpt, bool> &status = pack.sysStatus;
    Log.error("System status fault detected.");
    Log.error("[UV: %T, OV: %T] ", status[SysStatusOpt::UV], status[SysStatusOpt::OV]);
    Log.error("[OV: %T, SCD: %T]\n", status[SysStatusOpt::SCD], status[SysStatusOpt::OCD]);
    pack.sysControl2[SysControlOpt::DSG_ON] = false;
    pack.sysControl2[SysControlOpt::CHG_ON] = false;
    pack.pushControl();
    for (auto &entry : pack.balanceCells) {
        entry.second = false;
    }
    pack.pushBalancing();
}

void performBatteryState() {
    if (state == BatteryState::Idle && packCurrent > 0) {
        state = BatteryState::Charging;
    } else if (state == BatteryState::Idle && packCurrent < 0) {
        state = BatteryState::Discharging;
    } else if (state == BatteryState::Discharging && packCurrent == 0) {
        state = BatteryState::Idle;
    }
    
    bool &dsgOn = control2Config[control2Opt::DSG_ON];
    bool &chgOn = control2Config[control2Opt::CHG_ON]; 
    
    std::pair<const uint8_t, uint16_t> &minVoltage = *min_element(voltages.begin(), voltages.end(),
        [](const auto& l, const auto& r) { return l.second < r.second; });
    std::pair<const uint8_t, uint16_t> &maxVoltage = *std::max_element(voltages.begin(), voltages.end(),
        [](const auto& l, const auto& r) { return l.second < r.second; });
    
    // Check discharge
    if (dsgOn == true && minVoltage.second <= lowerVoltageLimit) {
        dsgOn = false;
        pushControl(registerMap::SYS_CTRL2);
    } else if (dsgOn == false && minVoltage.second >= lowerVoltageLimit+300) {
        dsgOn = true;
        pushControl(registerMap::SYS_CTRL2);
    }
    
    // Check charge
    if (chgOn == true && maxVoltage.second >= upperVoltageLimit) {
        chgOn = false;
        pushControl(registerMap::SYS_CTRL2);
        state = BatteryState::Balancing;
    } else if (chgOn == false && maxVoltage.second <=  upperVoltageLimit - 100) {
        chgOn = true;
        pushControl(registerMap::SYS_CTRL2);
    }
    
    if (BatteryState::Balancing) {
        performBalancing(state, minVoltage);
    }
}

// Perform balancing if necessary
void performBalancing(BatteryState &state, std::pair<const uint8_t, uint16_t> &minVoltage) {
    std::vector<std::tuple<std::pair<uint8_t, uint16_t>, unsigned int>> cellsToBalance; // List of all cells that need balancing (except the lowest) and their voltage difference to the lowest cell.
    for (auto &entry : voltages) {
        if (entry.first == minVoltage.first) { continue; }
        unsigned int difference = entry.second - minVoltage.second;
        if (difference > 20) {
            cellsToBalance.push_back(std::make_tuple(entry, (entry.second - minVoltage.second)));
        }
    }
    if (cellsToBalance.size() == 0) {
        Log.noticeln("All cells are balanced.");
        return;
    } else if (cellsToBalance.size() == 2) {
        if (!(std::get<0>(cellsToBalance[0]).first == registerMap::VC1_HI &&
              std::get<0>(cellsToBalance[1]).first == registerMap::VC5_HI)) {
              cellsToBalance.erase(cellsToBalance.begin());
              Log.noticeln("Need to balance adjacent cells, only one will be balanced at a time.");
        }
    }
    
    for (auto &entry : cellsToBalance) {
        uint8_t cellRegister = std::get<0>(entry).first;
        if (cellRegister == registerMap::VC1_HI) {
            balanceCells[balanceOpt::CB1] = true;
        } else if (cellRegister == registerMap::VC2_HI) {
            balanceCells[balanceOpt::CB2] = true;
        } else if (cellRegister == registerMap::VC5_HI) {
            balanceCells[balanceOpt::CB5] = true;
        }
    }
    pushBalancing();
}