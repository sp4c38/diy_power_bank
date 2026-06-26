#include <ArduinoLog.h>
#include <Wire.h>

#include "utils.h"

const uint8_t crc8_ccitt_small_table[16] = {
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
    0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d
};

bool readRegisters(uint8_t i2cAddress, uint8_t registerAddress, uint8_t* data, uint8_t numberBytes) {
    uint8_t buffer[2 * numberBytes + 1] = {
        (uint8_t) ((i2cAddress << 1) | 1),
    };

    Wire.beginTransmission(i2cAddress);
    Wire.write(registerAddress);
    uint endTransmissionResponse = Wire.endTransmission(true);
    if (endTransmissionResponse != 0) {
        Log.errorln("Read setup failed for register %X; response code: %d", registerAddress, endTransmissionResponse);
        return false;
    }

    uint8_t requestedBytes = 2 * numberBytes;
    uint8_t receivedBytes = Wire.requestFrom(i2cAddress, requestedBytes);
    if (receivedBytes != requestedBytes) {
        Log.errorln("Expected %d bytes from register %X but got %d.", requestedBytes, registerAddress, receivedBytes);
        return false;
    }

    for (int i = 0; i < requestedBytes; i++) {
        if (!Wire.available()) {
            Log.errorln("Expected bytes can't be read.");
            return false;
        }
        buffer[i + 1] = Wire.read();
    }

    if (crc8_ccitt(0, buffer, 2) != buffer[2]) {
        Log.errorln("CRC failed for register %X first data byte.", registerAddress);
        return false;
    }
    data[0] = buffer[1];

    for (int i = 0; i < (numberBytes - 1); i++) {
        data[i + 1] = buffer[3 + i * 2];
        if (crc8_ccitt(0, buffer + 3 + i * 2, 1) != buffer[4 + i * 2]) {
            Log.errorln("CRC failed for register %X data byte %d.", registerAddress, i + 2);
            return false;
        }
    }

    return true;
}

bool readRegister(uint8_t i2cAddress, uint8_t registerAddress, uint8_t* data) {
    uint8_t reading[1];
    if (!readRegisters(i2cAddress, registerAddress, reading, 1)) {
        return false;
    }
    *data = reading[0];
    return true;
}

bool writeRegister(uint8_t i2cAddress, uint8_t registerAddress, uint8_t data) {
    Wire.beginTransmission(i2cAddress);
    Wire.write(registerAddress);
    Wire.write(data);

    uint8_t crcBuffer[3] = {(uint8_t) ((i2cAddress << 1) | 0), registerAddress, data};
    uint8_t crc = crc8_ccitt(0, crcBuffer, 3);
    Wire.write(crc);

    uint response = Wire.endTransmission();
    if (response != 0) {
        Log.errorln("Write failed; response code: %d", response);
        return false;
    }
    return true;
}

uint8_t crc8_ccitt(uint8_t val, const uint8_t *buf, size_t cnt) {
    for (size_t i = 0; i < cnt; i++) {
        val ^= buf[i];
        val = (val << 4) ^ crc8_ccitt_small_table[val >> 4];
        val = (val << 4) ^ crc8_ccitt_small_table[val >> 4];
    }
    return val;
}

void every(unsigned long interval, unsigned long* previousMillis, std::function<void()> codeBlock) {
    unsigned long currentMillis = millis();
    if (currentMillis - *previousMillis >= interval) {
        *previousMillis = currentMillis;
        codeBlock();
    }
}

uint16_t median5(uint16_t values[5], uint8_t count) {
    uint16_t sorted[5] = {0, 0, 0, 0, 0};
    for (uint8_t i = 0; i < count && i < 5; i++) {
        sorted[i] = values[i];
    }
    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (sorted[j] < sorted[i]) {
                uint16_t temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }
    return sorted[count / 2];
}

int16_t median5Int(int16_t values[5], uint8_t count) {
    int16_t sorted[5] = {0, 0, 0, 0, 0};
    for (uint8_t i = 0; i < count && i < 5; i++) {
        sorted[i] = values[i];
    }
    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (sorted[j] < sorted[i]) {
                int16_t temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }
    return sorted[count / 2];
}

const char* boolText(bool value) {
    return value ? "true" : "false";
}
