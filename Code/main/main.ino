/*
This code is very specific to this project and only supports the used components in the
given configuration.
*/

#include <Arduino.h>
#include <ArduinoLog.h>
#include <cstdint>
#include <stdlib.h>
#include <Wire.h>

#include "ble_app.h"
#include "register.h"
#include "utils.h"

int adcOffset;
int adcGain;
uint16_t voltages[3];

BLEApp bleApp;

// Runs once
void setup() {
    Serial.begin(9600);
    while(!Serial && !Serial.available()){}
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    
    // bleApp.setup();
    
    Wire.begin();
    Wire.setClock(100000);
    Log.noticeln("************* The Last Minute Life Saver Power Bank *************");
    Log.noticeln("Please boot the BQ76920 device. Waiting 1s.");
    delay(1000);
    Log.noticeln("Continuing...");
    
    // Write CC_CFG register
    writeRegister(register_map::CC_CFG, 0x19);
    if (readRegister(register_map::CC_CFG) != 0x19) { // Check if writing was successful by reading CC_CFG back again.
        Log.fatalln("CC_CFG doesn't contain the expected data.");
        exit(1);
    } else {
        Log.noticeln("CC_CFG verification successful. Connection established.");
    };
    delay(250); // 250ms delay because of the transition from NORMAL mode to SHIP mode
    
    // Write start-up configuration
    writeRegister(register_map::SYS_CTRL1, 0b00010000); // Turn on ADC, select the internal die temperature
    writeRegister(register_map::SYS_CTRL2, 0b01000000); // Enable CC continuous readings
    
    adcOffset = (int8_t) readRegister(register_map::ADCOFFSET); // ADCOFFSET is stored as 2's complement; ADCOFFSET in uV
    adcGain = 365 + (((readRegister(register_map::ADCGAIN1) & 0b00001100) << 1) | ((readRegister(register_map::ADCGAIN2) & 0b11100000) >> 5)); // uV/LSB
    Log.verboseln("ADC offset: %d; ADC gain: %d", adcOffset, adcGain);
}

// Runs repeatedly
void loop() {
    // bleApp.loop();
    updateVoltages();
    delay(250);
}

void updateVoltages() {
    uint8_t cellRegistersToRead[] = {register_map::VC1_HI, register_map::VC2_HI, register_map::VC5_HI};
    uint16_t adcVal; // Type must be >= 14 bit long
    
    for (int i = 0; i < 3; i++) {
        uint8_t reading[4]; // VCx_HI, VCx_HI CRC, VCx_LO, VCx_LO CRC
        // For VCx_HI the CRC is based on the slave address and data byte; for VCx_LO only on the data byte.
        readRegisters(cellRegistersToRead[i], reading, 4);
        
        adcVal = ((reading[0] & 0b00111111) << 8) | reading[2];
        uint16_t voltage = (adcVal * adcGain / 1000) + adcOffset;
        voltages[i] = voltage;
    }
}