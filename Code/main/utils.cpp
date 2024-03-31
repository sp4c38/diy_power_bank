#include <ArduinoLog.h>
#include <cstdint>
#include <Wire.h>

#include "register.h"
#include "utils.h"

void readRegisters(uint8_t register, uint8_t* buffer, uint8_t numberBytes) {
	Wire.beginTransmission(I2C_BQ76920_ADDRESS);
	Wire.write(register);
	Wire.endTransmission(false); // false means a repeated start will be sent instead of releasing the bus.
	Wire.requestFrom(I2C_BQ76920_ADDRESS, numberBytes);
	
	for (int i = 0; i < numberBytes; i++) {
		if (Wire.available()) {
			buffer[i] = Wire.read();	
		}
	}
}

uint8_t readRegister(uint8_t register) {
	Wire.beginTransmission(I2C_BQ76920_ADDRESS);
	Wire.write(register);
	Wire.endTransmission(false); // false means a repeated start will be sent instead of releasing the bus.
	Wire.requestFrom(I2C_BQ76920_ADDRESS, 1);
	return Wire.read();
}

void writeRegister(uint8_t register, uint8_t data) {
	Log.noticeln("\nWrite: register %X; data: %X", register, data);
	// Blocks to send are: 1) Start 2) Slave address 3) Register address 4) Data 5) CRC 6) Stop
	Wire.beginTransmission(I2C_BQ76920_ADDRESS); // 1) and 2) step
	Wire.write(register); // 3) step
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
	  exit(1);
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