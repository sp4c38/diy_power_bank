/*
This code is very specific to this project and only supports the used components in the
given configuration.
*/

#include <Arduino.h>
#include <ArduinoLog.h>
#include <cstdint>
#include <stdlib.h>
#include <Wire.h>

#include "register.h"

// Either the BQ7692003PW or BQ7692003PWR IC must be used. CRC is enabled.
const uint8_t I2C_BQ76920_ADDRESS = 0x08;
const uint8_t ALERT_PIN = 10;
const uint8_t SDA_PIN = 18; // A4
const uint8_t SCL_PIN = 19; // A5

int adcOffset;
int adcGain;

// Runs once
void setup() {
    Serial.begin(9600);
    while(!Serial && !Serial.available()){}
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    
    Wire.begin();
    Wire.setClock(100000);
    Log.noticeln("************* The Last Minute Life Saver Power Bank *************");
    Log.noticeln("Please boot the BQ76920 device.");
    // delay(5000);
    
    writeRegister(register_map::CC_CFG, 0x19);
    if (readRegister(register_map::CC_CFG) != 0x19) { // Check if writing was successful by reading CC_CFG back again.
        Log.fatalln("CC_CFG doesn't contain the expected data.");
        // exit(1);
    };
    
    writeRegister(register_map::SYS_CTRL1, 0b00010000); // Turn on ADC, select the internal die temperature
    writeRegister(register_map::SYS_CTRL2, 0b01000000); // Enable CC continuous readings
    
    adcOffset = (int8_t) readRegister(register_map::ADCOFFSET); // ADCOFFSET is stored as 2's complement; ADCOFFSET in uV
    adcGain = 365 + (((readRegister(register_map::ADCGAIN1) & 0b00001100) << 1) | ((readRegister(register_map::ADCGAIN2) & 0b11100000) >> 5)); // uV/LSB
    Log.verboseln("ADC offset: %d; ADC gain: %d", adcOffset, adcGain);
}

// Runs repeatedly
void loop() {
    Log.noticeln("Looping");
    delay(5000);
}

uint8_t readRegister(uint8_t address) {
    Wire.beginTransmission(I2C_BQ76920_ADDRESS);
    Wire.write(address);
    Wire.endTransmission(false); // false means a repeated start will be sent instead of releasing the bus.
    Wire.requestFrom(I2C_BQ76920_ADDRESS, 1);
    return Wire.read();
}

void writeRegister(uint8_t address, uint8_t data) {
    Log.noticeln("Write: address %X; data: %X", address);
    // Blocks to send are: 1) Start 2) Slave address 3) Register address 4) Data 5) CRC 6) Stop
    Wire.beginTransmission(I2C_BQ76920_ADDRESS); // 1) and 2) step
    Wire.write(address); // 3) step
    Wire.write(data); // 4) step
    
    // CRC is calculated over slave address (including R/W bit), register address and data.
    uint8_t crc = 0;
    crc = _crc8_ccitt_update(crc, (I2C_BQ76920_ADDRESS << 1) | 0);
    crc = _crc8_ccitt_update(crc, address);
    crc = _crc8_ccitt_update(crc, data);
    
    Wire.write(crc);
    uint response = Wire.endTransmission();
    if (response != 0) {
      Log.errorln("Transmission failed; response code: %d", response);
      // exit(1);
    } else {
      Log.noticeln("Transmission successful; CRC: %X\n", crc);
    };
}

uint8_t _crc8_ccitt_update(uint8_t inCrc, uint8_t inData) {
    uint8_t i;
    uint8_t data;
    data = inCrc ^ inData;
    
    for (i = 0; i < 8; i++) {
        if ((data & 0x80) != 0) {
          data <<= 1;
          data ^= 0x07;
        } else data <<= 1;
    }
    
    return data;
}