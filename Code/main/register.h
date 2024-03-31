// Either the BQ7692003PW or BQ7692003PWR IC must be used. CRC is enabled.
const uint8_t I2C_BQ76920_ADDRESS = 0x08;
const uint8_t ALERT_PIN = 10;
const uint8_t SDA_PIN = 18; // A4
const uint8_t SCL_PIN = 19; // A5


namespace register_map {
	const uint8_t SYS_CTRL1 = 0x04;
	const uint8_t SYS_CTRL2 = 0x05;
	const uint8_t CC_CFG = 0x0B;
	const uint8_t ADCGAIN1 = 0x50;
	const uint8_t ADCGAIN2 = 0x59;
	const uint8_t ADCOFFSET = 0x51;
}