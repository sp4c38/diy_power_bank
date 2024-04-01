#include <ArduinoLog.h>
#include <cstdint>
#include <Wire.h>

#include "register.h"
#include "utils.h"

static const uint8_t crc8_ccitt_small_table[16] = {
	0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
	0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d
};

// numberBytes are the expected data bytes; the amount must not include the CRC bytes
void readRegisters(uint8_t registerAddress, uint8_t* data, uint8_t numberBytes) {
	uint8_t buffer[2*numberBytes+1] = { // Multiply by 2 to account for the CRC data bytes
		(I2C_BQ76920_ADDRESS << 1) | 1U, // Slave address and R/W bit
	}
	
	// Read
	Wire.beginTransmission(I2C_BQ76920_ADDRESS);
	Wire.write(registerAddress);
	Wire.endTransmission(false); // false means a repeated start will be sent instead of releasing the bus.
	Wire.requestFrom(I2C_BQ76920_ADDRESS, numberBytes);
	
	for (int i = 0; i < numberBytes; i++) {
		if (Wire.available()) {
			buffer[i] = Wire.read();	
		}
	}
	
	// Check CRC
	// First CRC is calculated over slave address and the first data byte.
	if (crc8_ccitt(0, buffer, 2) != buffer[2]) {
		Log.fatalln("CRC failed. CRC of slave address and data byte don't match.");
	};
	
	for (int i = 0; i < (numberBytes-1); i++) {
		crc8_ccitt(0, buffer+3, 2);
	}
}

uint8_t readRegister(uint8_t registerAddress) {
	uint8_t buffer[1];
	readRegisters(registerAddress, buffer, 1);
	return buffer[0];
}

void writeRegister(uint8_t registerAddress, uint8_t data) {
	Log.noticeln("\nWrite: register %X; data: %X", registerAddress, data);
	// Blocks to send are: 1) Start 2) Slave address 3) Register address 4) Data 5) CRC 6) Stop
	Wire.beginTransmission(I2C_BQ76920_ADDRESS); // 1) and 2) step
	Wire.write(registerAddress); // 3) step
	Wire.write(data); // 4) step
	
	// CRC is calculated over slave address (including R/W bit), register address and data.
	uint8_t crc = 0;
	crc = _crc8_ccitt_update(crc, (I2C_BQ76920_ADDRESS << 1) | 0);
	crc = _crc8_ccitt_update(crc, registerAddress);
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

// Copied from the Zephyr project. (https://github.com/zephyrproject-rtos/zephyr/blob/7291450151ef585c7b612f16752dad4ff7df9bf1/lib/crc/crc8_sw.c)
uint8_t crc8_ccitt(uint8_t val, const void *buf, size_t cnt) {
	size_t i;
	const uint8_t *p = buf;

	for (i = 0; i < cnt; i++) {
		val ^= p[i];
		val = (val << 4) ^ crc8_ccitt_small_table[val >> 4];
		val = (val << 4) ^ crc8_ccitt_small_table[val >> 4];
	}
	return val;
}