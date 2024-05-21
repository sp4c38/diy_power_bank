#include <map>
#include <string>

#ifndef REGISTER_H
#define REGISTER_H

const uint16_t upperVoltageLimit = 4190; // in mV
const uint16_t lowerVoltageLimit = 2800; // in mV

// The variable balancingDifference must be smaller than allowedBalancingDifference. This means that the cells get balanced lower than the amount needed to start balancing again.
// The battery voltage jiggles a bit around its actual value. So if allowedBalancingDifference and balancingDifference were the same we would frequently reenable balancing just because the
// battery hops a little bit higher than allowedBalancingDifference due to its internal chemistry etc..
const uint16_t allowedBalancingDifference = 30; // Maximum difference that is allowed between cells to not start balancing.
const uint16_t balancingDifference = 10; // Balance to this difference when balancing gets enabled.

const int16_t dischargingThreshold = -50; // in mA; If the current drops below this value, the pack is considered to be discharging.

const uint8_t currentSenseResistance = 8; // in mOhm

// Either the BQ7692003PW or BQ7692003PWR IC must be used. CRC is enabled.
const uint8_t I2C_BQ76920_ADDRESS = 0x08;
const uint8_t ALERT_PIN = 10;
const uint8_t SDA_PIN = 18; // A4
const uint8_t SCL_PIN = 19; // A5

enum class SysStatusOpt : uint8_t {
// Store the BIT place number like indicated in the data sheet.
	CC_READY = 7,
	OVRD_ALERT = 4,
	UV = 3,
	OV = 2,
	SCD = 1,
	OCD = 0
};

enum class BalanceOpt : uint8_t {
	// Store the BIT place number like indicated in the data sheet.
	CB1 = 0,
	CB2 = 1,
	CB5 = 4
};

enum class SysControlOpt : uint8_t {
	// Store the BIT place number like indicated in the data sheet.
	// SYS_CTRL1
	ADC_EN = 4,
	TEMP_SEL = 3,
	SHUT_A = 1,
	SHUT_B = 0,

	// SYS_CTRL2
	DELAY_DIS = 7,
	CC_EN = 6,
	CC_ONESHOT = 5,
	DSG_ON = 1,
	CHG_ON = 0
};

namespace registerMap {
	const uint8_t SYS_STAT = 0x00;
	const uint8_t CELLBAL1 = 0x01;
	const uint8_t SYS_CTRL1 = 0x04;
	const uint8_t SYS_CTRL2 = 0x05;
	const uint8_t PROTECT1 = 0x06;
	const uint8_t PROTECT2 = 0x07;
	const uint8_t PROTECT3 = 0x08;
	const uint8_t OV_TRIP = 0x09;
	const uint8_t UV_TRIP = 0x0A;
	const uint8_t CC_CFG = 0x0B;

	const uint8_t VC1_HI = 0x0C;
	const uint8_t VC2_HI = 0x0E;
	const uint8_t VC5_HI = 0x14;
	
	const uint8_t BAT_HI = 0x2A;
	const uint8_t TS1_HI = 0x2C;
	const uint8_t CC_HI = 0x32;
	const uint8_t ADCGAIN1 = 0x50;
	const uint8_t ADCOFFSET = 0x51;
	const uint8_t ADCGAIN2 = 0x59;
}

const std::map<BalanceOpt, uint8_t> balanceCellToVoltageCell = {
	{BalanceOpt::CB1, registerMap::VC1_HI},
	{BalanceOpt::CB2, registerMap::VC2_HI},
	{BalanceOpt::CB5, registerMap::VC5_HI}
};

const std::map<uint8_t, BalanceOpt> voltageCellToBalanceOpt = {
	{registerMap::VC1_HI, BalanceOpt::CB1},
	{registerMap::VC2_HI, BalanceOpt::CB2},
	{registerMap::VC5_HI, BalanceOpt::CB5}
};

const std::map<BalanceOpt, std::string> balanceCellToStringName = {
	{BalanceOpt::CB1, "Cell 1"},
	{BalanceOpt::CB2, "Cell 2"},
	{BalanceOpt::CB5, "Cell 5"}
};

enum class BatteryState {
	SHIPMode,
	Charging,
	Balancing,
	Discharging,
	Idle
};

const std::map<BatteryState, std::string> batteryStateToStringName = {
	{BatteryState::SHIPMode, "SHIP mode"},
	{BatteryState::Charging, "Charging"},
	{BatteryState::Balancing, "Balancing"},
	{BatteryState::Discharging, "Discharging"},
	{BatteryState::Idle, "Idle"}
};

#endif