uint8_t readRegister(uint8_t i2cAddress, uint8_t address);
void readRegisters(uint8_t i2cAddress, uint8_t registerAddress, uint8_t* buffer, uint8_t numberBytes);
void writeRegister(uint8_t i2cAddress, uint8_t address, uint8_t data);

uint8_t crc8_ccitt(uint8_t val, const uint8_t *buf, size_t cnt);
uint8_t _crc8_ccitt_update(uint8_t inCrc, uint8_t inData);

void every(unsigned int interval, unsigned long* previousMillis, std::function<void()> codeBlock);
