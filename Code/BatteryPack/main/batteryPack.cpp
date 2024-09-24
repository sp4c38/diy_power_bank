#include <ArduinoLog.h>
#include <Wire.h>

#include "batteryPack.h"
#include "utils.h"

void BatteryPack::readOffsetAndGain() {
	adcOffset = (int8_t) readRegister(I2C_BQ76920_ADDRESS, registerMap::ADCOFFSET); // ADCOFFSET is stored as 2's complement; ADCOFFSET in uV
	adcGain = 365 + (((readRegister(I2C_BQ76920_ADDRESS, registerMap::ADCGAIN1) & 0b00001100) << 1) | ((readRegister(I2C_BQ76920_ADDRESS, registerMap::ADCGAIN2) & 0b11100000) >> 5)); // uV/LSB
}

void BatteryPack::updateVoltages() {
	unsigned long adcVal; // Type must be >= 14 bit long
	for (auto &pair : voltages) {
		uint8_t reading[2]; // VCx_HI, VCx_LO
		// For VCx_HI the CRC is based on the slave address and data byte; for VCx_LO only on the data byte.
		readRegisters(I2C_BQ76920_ADDRESS, pair.first, reading, 2);
		adcVal = ((reading[0] & 0b00111111) << 8) | reading[1];
		pair.second = ((adcVal * adcGain) / 1000) + adcOffset;
	}

	uint8_t reading[2];
	readRegisters(I2C_BQ76920_ADDRESS, registerMap::BAT_HI, reading, 2);
	adcVal = (reading[0] << 8) | reading[1];
	voltage = (4 * adcGain * adcVal) / 1000 + (voltages.size() * adcOffset);
}

void BatteryPack::updateCurrent() {
	uint8_t reading[2];
	readRegisters(I2C_BQ76920_ADDRESS, registerMap::CC_HI, reading, 2);
	int16_t adcVal = (reading[0] << 8) | reading[1]; // Registers represent a 2's complement number in 16 bit format
	current = ((long) adcVal * 8.44) / currentSenseResistance; // Result in mA

	// Filter out noise
	if (current >= -3 && current <= 3) {
		current = 0;
	}
}

void BatteryPack::updateSysStatus() {
	uint8_t raw = readRegister(I2C_BQ76920_ADDRESS, registerMap::SYS_STAT);
	for (auto &value : sysStatus) {
		sysStatus[value.first] = (raw >> ((uint8_t) value.first)) & 1;
	}
}

void BatteryPack::updateSysControl() {
	uint8_t reading[2];
	readRegisters(I2C_BQ76920_ADDRESS, registerMap::SYS_CTRL1, reading, 2);
	for (auto &value : sysControl1) {
		sysControl1[value.first] = (reading[0] >> ((uint8_t) value.first)) & 1;
	}
	for (auto &value : sysControl2) {
		sysControl2[value.first] = (reading[1] >> ((uint8_t) value.first)) & 1;
	}
}

void BatteryPack::updateTemperature() {
	uint8_t reading[2];
	readRegisters(I2C_BQ76920_ADDRESS, registerMap::TS1_HI, reading, 2);
	uint16_t adcVal = (((uint16_t) reading[0] & 0b00111111) << 8) | reading[1];
	temp = 25.0f - (((adcVal * 0.000382f) - 1.20f) / 0.0042f);
}

// Transitions the BQ76920 to a low-power mode (SHIP mode).
void BatteryPack::transitionToSHIPMode() {
	Log.noticeln("System transitioning to SHIP mode. To resume operation press the push button.");
	sysControl1[SysControlOpt::SHUT_A] = sysControl1[SysControlOpt::SHUT_B]  = false;
	pushControl();
	sysControl1[SysControlOpt::SHUT_B] = true;
	pushControl();
	sysControl1[SysControlOpt::SHUT_A] = true;
	sysControl1[SysControlOpt::SHUT_B] = false;
	pushControl();
	state.setState(BatteryState::State::SHIPMode);
}

void BatteryPack::pushBalancing() {
	// Balance enabled adjacent cells aren't allowed! Check this.
	// Can't use the checkIfCellsAreAdjacent as this just takes two cells and doesn't evaluate if balancing is enabled or is disabled for these cells.
	auto it1 = balanceCells.begin();
	auto it2 = it1;
	++it2;
	while (it2 != balanceCells.end()) {
		if (it1->second && it2->second) {
			Log.errorln("Trying to push balancing config that balances adjacent cells. Never do this!");
			return;
		}
		++it1;
		++it2;
	}

    Log.noticeln("Pushing balance config - CB1: %T, CB2: %T, CB5: %T", balanceCells[BalanceOpt::CB1], balanceCells[BalanceOpt::CB2], balanceCells[BalanceOpt::CB5]);
	uint8_t balancingData = 0;
	for (auto &entry : balanceCells) {
		balancingData = balancingData | (((uint8_t) entry.second) << ((uint8_t) entry.first));
	}
	writeRegister(I2C_BQ76920_ADDRESS, registerMap::CELLBAL1, balancingData);
}

bool BatteryPack::checkIfCellsAreAdjacent(const BalanceOpt a, const BalanceOpt b) {
	auto it1 = balanceCells.find(a);
	auto it1_next = it1;
	++it1_next;
	auto it2 = balanceCells.find(b);
	auto it2_next = it2;
	++it2_next;

	if (it1 != balanceCells.end() && it2 != balanceCells.end()) {
		// The issue with using ++it1 directly is that it doesn't just give back the increased iterator, it also assigns the increased
		// iterator to it1 which we don't want because that would influence the other statments of the if-clause.
		if ((it1_next == it2) || (it2_next == it1)) {
			return true;
		}
	}

	return false;
}

void BatteryPack::pushProtection() {
	Log.noticeln("Pushing protection.");
	// RSNS = 0, SCD_D1:0 = 400μs (0x3), SCD_T2:0 = 100mV (0x7) (ISCD = 12,5A)
	uint8_t protect1Config = 0b00011111;
	// OCD_D2:0 = 160ms (0x4), OCD_T3:0 = 17mV (0x3) (IOCD = 2,125A)
	uint8_t protect2Config = 0b01000011;
	// UV_D1:0 = 0x1 (4s), OV_D1:0 = 0x1 (2s)
	uint8_t protect3Config = 0b01010000;
	// Desired overvoltage protection: 4.23 Volt
	uint8_t ovTripConfig = ((((unsigned long) 4230 - adcOffset) * 1000 / adcGain) >> 4) & 0b11111111;
	// Desired undervoltage protection: 2.55 Volt
	uint8_t uvTripConfig = ((((unsigned long) 2550 - adcOffset) * 1000 / adcGain) >> 4) & 0b11111111;
	writeRegister(I2C_BQ76920_ADDRESS, registerMap::PROTECT1, protect1Config);
	writeRegister(I2C_BQ76920_ADDRESS, registerMap::PROTECT2, protect2Config);
	writeRegister(I2C_BQ76920_ADDRESS, registerMap::PROTECT3, protect3Config);
	writeRegister(I2C_BQ76920_ADDRESS, registerMap::OV_TRIP, ovTripConfig);
	writeRegister(I2C_BQ76920_ADDRESS, registerMap::UV_TRIP, uvTripConfig);
}

void BatteryPack::pushControl() {
	Log.noticeln("Pushing system control.");
	uint8_t sysControl1Data = 0;
	uint8_t sysControl2Data = 0;
	for (const auto &entry : sysControl1) {
		sysControl1Data = sysControl1Data | (((uint8_t) entry.second) << ((uint8_t) entry.first));
	}
	for (const auto &entry : sysControl2) {
		sysControl2Data = sysControl2Data | (((uint8_t) entry.second) << ((uint8_t) entry.first));
	}
	writeRegister(I2C_BQ76920_ADDRESS, registerMap::SYS_CTRL1, sysControl1Data);
	writeRegister(I2C_BQ76920_ADDRESS, registerMap::SYS_CTRL2, sysControl2Data);
}