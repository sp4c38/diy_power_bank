uint8_t _crc8_ccitt_update(uint8_t inCrc, uint8_t inData);

void writeRegister(uint8_t address, uint8_t data);
uint8_t readRegister(uint8_t address);
void readRegisters(uint8_t registerAddress, uint8_t* buffer, uint8_t numberBytes);