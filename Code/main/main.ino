/*
This code is very specific to this project and only supports the used components in the
given configuration.
*/

#include <Arduino.h>
#include <ArduinoLog.h>
#include <cstdint>
#include <functional>
#include <stdlib.h>
#include <unordered_map>
#include <Wire.h>

#include "ble_app.h"
#include "main.h"
#include "utils.h"

int8_t adcOffset;
uint adcGain;

std::unordered_map<uint8_t, bool> balanceCells = {
    {balance::CB1, false},
    {balance::CB2, false},
    {balance::CB5, false}
};

std::unordered_map<uint8_t, uint16_t> voltages = {
    {register_map::VC1_HI, 0},
    {register_map::VC2_HI, 0},
    {register_map::VC5_HI, 0}
};
uint16_t packVoltage;
size_t numberCells = voltages.size();

std::unordered_map<uint8_t, bool> systemControl1Config = {
    {systemControl1::ADC_EN, 1},
    {systemControl1::TEMP_SEL, 0},
    {systemControl1::SHUT_A, 0},
    {systemControl1::SHUT_B, 0}
};

std::unordered_map<uint8_t, bool> systemControl2Config = {
    {systemControl2::DELAY_DIS, 0},
    {systemControl2::CC_EN, 1},
    {systemControl2::CC_ONESHOT, 0},
    {systemControl2::DSG_ON, 0},
    {systemControl2::CHG_ON, 0}
};

int16_t packCurrent; 

BLEApp bleApp;

// Runs once
void setup() {
    Serial.begin(9600);
    while(!Serial && !Serial.available()){}
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    
    // bleApp.setup();
    
    Wire.begin();
    Wire.setClock(100000);
    Log.noticeln("\n************* The Last Minute Life Saver Power Bank *************");
    Log.noticeln("Starting up...");
    Log.noticeln("Please boot the BQ76920 device. Waiting 1s...");
    delay(1000);
    
    // Write CC_CFG register
    writeRegister(register_map::CC_CFG, 0x19);
    if (readRegister(register_map::CC_CFG) != 0x19) { // Check if writing was successful by reading CC_CFG back again.
        Log.fatalln("CC_CFG doesn't contain the expected data.");
        exit(1);
    } else {
        Log.noticeln("CC_CFG verification successful. Connection established.");
    };
    delay(250); // 250ms delay because of the transition from NORMAL mode to SHIP mode
    
    adcOffset = (int8_t) readRegister(register_map::ADCOFFSET); // ADCOFFSET is stored as 2's complement; ADCOFFSET in uV
    adcGain = 365 + (((readRegister(register_map::ADCGAIN1) & 0b00001100) << 1) | ((readRegister(register_map::ADCGAIN2) & 0b11100000) >> 5)); // uV/LSB
    Log.verboseln("ADC offset: %d; ADC gain: %d", adcOffset, adcGain);
    
    pushSystemControl(register_map::SYS_CTRL1);
    pushSystemControl(register_map::SYS_CTRL2);
    pushProtection();
    pushBalancing();
    
    Log.noticeln("Start up complete.\n");
}

// Runs repeatedly
void loop() {
    // Keep at start of loop function
    updateVoltages();
    updateCurrent();
    static unsigned long checkTemperaturePreviousMillis = 0;
    every(1000, &checkTemperaturePreviousMillis, []() {
        checkTemperature();
    });
    
    // Serial input
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        if (command == "balance") {
            balanceCells[balance::CB1] = true;
            pushBalancing();
        }
    }
 
    // Keep at end of loop function
    // bleApp.loop();
    delay(500);
}

void updateVoltages() {
    unsigned long adcVal; // Type must be >= 14 bit long
    for (auto &pair : voltages) {
        uint8_t reading[2]; // VCx_HI, VCx_LO
        // For VCx_HI the CRC is based on the slave address and data byte; for VCx_LO only on the data byte.
        readRegisters(pair.first, reading, 2);
        
        adcVal = ((reading[0] & 0b00111111) << 8) | reading[1];
        pair.second = ((adcVal * adcGain) / 1000) + adcOffset;
    }
    
    uint8_t reading[2];
    readRegisters(register_map::BAT_HI, reading, 2);
    adcVal = (reading[0] << 8) | reading[1];
    packVoltage = (4 * adcGain * adcVal) / 1000 + (numberCells * adcOffset);
    
    static unsigned long cellVoltagePreviousMillis = 0;
    every(10000, &cellVoltagePreviousMillis, []() {
        Log.noticeln("Pack voltage: %d V | VC1: %d V, VC2: %d V, VC5: %d V", packVoltage, voltages[register_map::VC1_HI], voltages[register_map::VC2_HI], voltages[register_map::VC5_HI]);
    });
}

void updateCurrent() {
    uint8_t reading[2];
    readRegisters(register_map::CC_HI, reading, 2);
    int16_t adcVal = (reading[0] << 8) | reading[1]; // Registers represent a 2's complement number in 16 bit format
    packCurrent = ((long) adcVal * 8.44) / currentSenseResistance; // Result in mA
    
    // Filter out noise
    if (packCurrent >= -3 && packCurrent <= 3) {
        packCurrent = 0;
    }
    
    static unsigned long packCurrentPreviousMillis = 0;
    every(10000, &packCurrentPreviousMillis, []() {
        Log.noticeln("Pack current: %d mA", packCurrent);
    });
}

void checkTemperature() {
    uint8_t reading[2];
    readRegisters(register_map::TS1_HI, reading, 2);
    
    uint16_t adcVal = (((uint16_t) reading[0] & 0b00111111) << 8) | reading[1];
    float temp = 25.0f - (((adcVal * 0.000382f) - 1.20f) / 0.0042f);
    if (!(temp >= -40.0f && temp <= 85.0f)) {
        Log.warning("Internal die temperature outside of operating bounds: %F. Restricting operation.", temp);
        for (auto &balanceConfig : balanceCells) {
            balanceConfig.second = false;
        }
        pushBalancing();
    }
}

void pushBalancing() {
    // Adjacent cells aren't allowed to be balanced!
    bool foundCellToBalance = false;
    for (auto &balanceCellConfig : balanceCells) {
        if (balanceCellConfig.second == true) {
            if (foundCellToBalance == true) {
                if (balanceCells[balance::CB1] == true &&
                    balanceCells[balance::CB5] == true &&
                    balanceCells[balance::CB2] == false) {
                    break;
                } else {
                    Log.fatalln("Trying to balance adjacent cells. Never do this!");
                    exit(1);
                }
            }
            foundCellToBalance = true;
        }
    }
    
    Log.noticeln("Pushing balance config - CB1: %T, CB2: %T, CB5: %T", balanceCells[balance::CB1], balanceCells[balance::CB2], balanceCells[balance::CB5]);
    uint8_t balancingData = 0;
    for (auto &entry : balanceCells) {
        balancingData = balancingData | (((uint8_t) entry.second) << entry.first);
    }
    writeRegister(register_map::CELLBAL1, balancingData);
}

void pushProtection() {
    Log.noticeln("Pushing protection.");
    // RSNS = 0, SCD_D1:0 = 70μs (0x0), SCD_T2:0 = 33mV (0x1) (ISCD = 4,125A)
    uint8_t protect1Config = 0b00000001;
    // OCD_D2:0 = 0x5, OCD_T3:0 = 17mV (0x3) (IOCD = 2,125A)
    uint8_t protect2Config = 0b01010011;
    // UV_D1:0 = 0x1 (4s), OV_D1:0 = 0x1 (2s)
    uint8_t protect3Config = 0b01010000;
    // Desired overvoltage protection: 4.23 Volt
    uint8_t ovTripConfig = ((((uint16_t) 4230 - adcOffset) * 1000 / adcGain) >> 4) & 0x11111111;
    // Desired undervoltage protection: 2.55 Volt
    uint8_t uvTripConfig = ((((uint16_t) 2550 - adcOffset) * 1000 / adcGain) >> 4) & 0x11111111;

    writeRegister(register_map::PROTECT1, protect1Config);
    writeRegister(register_map::PROTECT2, protect2Config);
    writeRegister(register_map::PROTECT3, protect3Config);
    writeRegister(register_map::OV_TRIP, ovTripConfig);
    writeRegister(register_map::UV_TRIP, uvTripConfig);    
}

void pushSystemControl(uint8_t registerAddress) {
    Log.noticeln("Pushing system control.");
    std::unordered_map<uint8_t, bool> *systemControlConfig;
    if (registerAddress == register_map::SYS_CTRL1) {
        systemControlConfig = &systemControl1Config;
    } else if (registerAddress == register_map::SYS_CTRL2) {
        systemControlConfig = &systemControl2Config;
    } else {
        Log.fatalln("Invalid register address parsed: %X", registerAddress);
        exit(1);
    }
    uint8_t data = 0;
    for (auto &entry : *systemControlConfig) {
        data = data | (((uint8_t) entry.second) << entry.first);
    }
    writeRegister(registerAddress, data);
}