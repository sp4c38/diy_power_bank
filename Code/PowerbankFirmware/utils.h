#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <functional>

bool readRegister(uint8_t i2cAddress, uint8_t address, uint8_t* data);
bool readRegisters(uint8_t i2cAddress, uint8_t registerAddress, uint8_t* buffer, uint8_t numberBytes);
bool writeRegister(uint8_t i2cAddress, uint8_t address, uint8_t data);
uint8_t crc8_ccitt(uint8_t val, const uint8_t *buf, size_t cnt);
void every(unsigned long interval, unsigned long* previousMillis, std::function<void()> codeBlock);
uint16_t median5(uint16_t values[5], uint8_t count);
int16_t median5Int(int16_t values[5], uint8_t count);
const char* boolText(bool value);

#endif
