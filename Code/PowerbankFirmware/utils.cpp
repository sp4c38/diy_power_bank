#include <ArduinoLog.h>
#include <cstdint>
#include <functional>
#include <Wire.h>

#include "register.h"
#include "utils.h"

const uint8_t crc8_ccitt_small_table[16] = {
	0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
	0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d
};

// numberBytes are the expected data bytes; the amount must not include the CRC bytes
void readRegisters(uint8_t i2cAddress, uint8_t registerAddress, uint8_t* data, uint8_t numberBytes) {
	uint8_t buffer[2*numberBytes+1] = { // Multiply by 2 to account for the CRC data bytes
		(i2cAddress << 1) | 1, // Slave address and R/W bit
	};

	// Read
	Wire.beginTransmission(i2cAddress);
	Wire.write(registerAddress);
	Wire.endTransmission(true); // false means a repeated start will be sent instead of releasing the bus.
	Wire.requestFrom(I2C_BQ76920_ADDRESS, 2*numberBytes);

	for (int i = 0; i < 2*numberBytes; i++) {
		if (Wire.available()) {
			buffer[i+1] = Wire.read();
		} else {
			Log.errorln("Expected bytes can't be read.");
			exit(1);
		}
	};

	// Check CRC
	// First CRC is calculated over slave address and the first data byte.
	if (crc8_ccitt(0, buffer, 2) != buffer[2]) {
		Log.errorln("CRC failed. CRCs of slave address and data byte don't match.");
		exit(1);
	};
	data[0] = buffer[1];

	for (int i = 0; i < (numberBytes-1); i++) {
		data[i+1] = buffer[3+i*2];
		if (crc8_ccitt(0, buffer+3+i*2, 1) != buffer[4+i*2]) {
			Log.errorln("CRC failed. CRC of data byte %d doesn't match.", i+2);
			exit(1);
		};
	};
}

uint8_t readRegister(uint8_t i2cAddress, uint8_t registerAddress) {
	uint8_t data[1];
	readRegisters(i2cAddress, registerAddress, data, 1);
	return data[0];
}

void writeRegister(uint8_t i2cAddress, uint8_t registerAddress, uint8_t data) {
	// Blocks to send are: 1) Start 2) Slave address 3) Register address 4) Data 5) CRC 6) Stop
	Wire.beginTransmission(i2cAddress); // 1) and 2) step
	Wire.write(registerAddress); // 3) step
	Wire.write(data); // 4) step

	// CRC is calculated over slave address (including R/W bit), register address and data.
	uint8_t crcBuffer[3] = {(I2C_BQ76920_ADDRESS << 1) | 0, registerAddress, data};
	uint8_t crc = crc8_ccitt(0, crcBuffer, 3);

	Wire.write(crc);
	uint response = Wire.endTransmission();
	if (response != 0) {
	  Log.errorln("Write failed; response code: %d", response);
	  exit(1);
	} else {
	  Log.noticeln("Write: register %X; data: %X; CRC: %X successful.", registerAddress, data, crc);
	};
}

// Copied from the Zephyr project and slightly modified. (https://github.com/zephyrproject-rtos/zephyr/blob/7291450151ef585c7b612f16752dad4ff7df9bf1/lib/crc/crc8_sw.c)
uint8_t crc8_ccitt(uint8_t val, const uint8_t *buf, size_t cnt) {
	size_t i;

	for (i = 0; i < cnt; i++) {
		val ^= buf[i];
		val = (val << 4) ^ crc8_ccitt_small_table[val >> 4];
		val = (val << 4) ^ crc8_ccitt_small_table[val >> 4];
	}
	return val;
}

void every(unsigned int interval, unsigned long* previousMillis, std::function<void()> codeBlock) {
	unsigned long currentMillis = millis();

	if (currentMillis - *previousMillis >= interval) {
		*previousMillis = currentMillis;
		codeBlock();
	}
}
