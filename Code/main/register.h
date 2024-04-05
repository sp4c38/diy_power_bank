const uint8_t currentSenseResistance = 8; // in mOhm

// Either the BQ7692003PW or BQ7692003PWR IC must be used. CRC is enabled.
const uint8_t I2C_BQ76920_ADDRESS = 0x08;
const uint8_t ALERT_PIN = 10;
const uint8_t SDA_PIN = 18; // A4
const uint8_t SCL_PIN = 19; // A5

namespace balance {
	const uint8_t NONE = 8;
	// Store the BIT place number like indicated in the data sheet.
	const uint8_t CB1 = 0;
	const uint8_t CB2 = 1;
	const uint8_t CB5 = 4;
}

namespace systemControl1 {
	const uint8_t NONE = 8; // Only one NONE is needed so it is just placed in systemControl1 instead of systemControl2.
	// Store the BIT place number like indicated in the data sheet.
	const uint8_t ADC_EN = 4;
	const uint8_t TEMP_SEL = 3;
	const uint8_t SHUT_A = 1;
	const uint8_t SHUT_B = 0;
}

namespace systemControl2 {
	const uint8_t DELAY_DIS = 7;
	const uint8_t CC_EN = 6;
	const uint8_t CC_ONESHOT = 5;
	const uint8_t DSG_ON = 1;
	const uint8_t CHG_ON = 0;
}

namespace register_map {
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