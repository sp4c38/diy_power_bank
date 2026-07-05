#include <ArduinoLog.h>

#include "bq76920Driver.h"
#include "utils.h"

bool Bq76920Driver::begin() {
    online = false;
    cachedSysCtrl1 = (1 << (uint8_t) SysControlOpt::ADC_EN);
    // FETs stay off at init; the safety policy enables them once readings are
    // trusted. Otherwise every reboot would briefly power the output even when
    // it was manually or automatically switched off before the restart.
    cachedSysCtrl2 = (1 << (uint8_t) SysControlOpt::CC_EN);
    cachedCellBal = 0;

    if (!writeRegister(I2C_BQ76920_ADDRESS, registerMap::CC_CFG, 0x19)) {
        return false;
    }
    uint8_t ccConfig = 0;
    if (!readRegister(I2C_BQ76920_ADDRESS, registerMap::CC_CFG, &ccConfig) || ccConfig != 0x19) {
        Log.errorln("CC_CFG verification failed.");
        return false;
    }
    delay(250);

    if (!readOffsetAndGain()) {
        return false;
    }
    if (!pushProtection()) {
        return false;
    }
    if (!clearFaults()) {
        return false;
    }
    if (!pushControl()) {
        return false;
    }
    if (!disableBalancing()) {
        return false;
    }

    online = true;
    return true;
}

bool Bq76920Driver::isOnline() const {
    return online;
}

void Bq76920Driver::markOffline() {
    online = false;
}

bool Bq76920Driver::readOffsetAndGain() {
    uint8_t offset = 0;
    uint8_t gain1 = 0;
    uint8_t gain2 = 0;
    if (!readRegister(I2C_BQ76920_ADDRESS, registerMap::ADCOFFSET, &offset) ||
        !readRegister(I2C_BQ76920_ADDRESS, registerMap::ADCGAIN1, &gain1) ||
        !readRegister(I2C_BQ76920_ADDRESS, registerMap::ADCGAIN2, &gain2))
    {
        return false;
    }

    adcOffsetUv = (int8_t) offset;
    adcGainUvPerLsb = 365 + (((gain1 & 0b00001100) << 1) | ((gain2 & 0b11100000) >> 5));
    Log.noticeln("BQ calibration: ADC offset %d uV; ADC gain %d uV/LSB", adcOffsetUv, adcGainUvPerLsb);
    return true;
}

bool Bq76920Driver::pushProtection() {
    uint8_t protect1Config = 0b00011111; // SCD: 400 us, 100 mV => about 12.5 A at 8 mOhm.
    uint8_t protect2Config = 0b01000111; // OCD: 160 ms, 28 mV => about 3.5 A.
    uint8_t protect3Config = 0b01010000; // UV delay 4s, OV delay 2s.
    uint8_t ovTripConfig = ((((unsigned long) thresholds::hardwareOvMv - adcOffsetUv) * 1000 / adcGainUvPerLsb) >> 4) & 0xFF;
    uint8_t uvTripConfig = ((((unsigned long) thresholds::hardwareUvMv - adcOffsetUv) * 1000 / adcGainUvPerLsb) >> 4) & 0xFF;

    bool written = writeRegister(I2C_BQ76920_ADDRESS, registerMap::PROTECT1, protect1Config) &&
        writeRegister(I2C_BQ76920_ADDRESS, registerMap::PROTECT2, protect2Config) &&
        writeRegister(I2C_BQ76920_ADDRESS, registerMap::PROTECT3, protect3Config) &&
        writeRegister(I2C_BQ76920_ADDRESS, registerMap::OV_TRIP, ovTripConfig) &&
        writeRegister(I2C_BQ76920_ADDRESS, registerMap::UV_TRIP, uvTripConfig);
    if (!written) {
        return false;
    }

    uint8_t protect2Readback = 0;
    if (!readRegister(I2C_BQ76920_ADDRESS, registerMap::PROTECT2, &protect2Readback)) {
        Log.errorln("PROTECT2 readback failed.");
        return false;
    }
    if (protect2Readback != protect2Config) {
        Log.errorln("PROTECT2 verification failed: wrote %X, read %X.", protect2Config, protect2Readback);
        return false;
    }

    Log.noticeln("BQ protection configured: PROTECT2=%X; OCD=3500 mA; delay=160 ms.", protect2Readback);
    return true;
}

bool Bq76920Driver::pushControl() {
    return writeSysCtrl1(cachedSysCtrl1) && writeSysCtrl2(cachedSysCtrl2);
}

bool Bq76920Driver::writeSysCtrl1(uint8_t value) {
    if (!writeRegister(I2C_BQ76920_ADDRESS, registerMap::SYS_CTRL1, value)) {
        online = false;
        return false;
    }
    cachedSysCtrl1 = value;
    return true;
}

bool Bq76920Driver::writeSysCtrl2(uint8_t value) {
    if (!writeRegister(I2C_BQ76920_ADDRESS, registerMap::SYS_CTRL2, value)) {
        online = false;
        return false;
    }
    cachedSysCtrl2 = value;
    uint8_t readback = 0;
    if (readRegister(I2C_BQ76920_ADDRESS, registerMap::SYS_CTRL2, &readback) && readback != value) {
        Log.warningln("SYS_CTRL2 write mismatch: wrote 0x%X, read 0x%X.", value, readback);
    }
    return true;
}

bool Bq76920Driver::readRaw14(uint8_t address, uint16_t& value) {
    uint8_t reading[2];
    if (!readRegisters(I2C_BQ76920_ADDRESS, address, reading, 2)) {
        online = false;
        return false;
    }
    value = (((uint16_t) reading[0] & 0b00111111) << 8) | reading[1];
    return true;
}

bool Bq76920Driver::readRaw16(uint8_t address, uint16_t& value) {
    uint8_t reading[2];
    if (!readRegisters(I2C_BQ76920_ADDRESS, address, reading, 2)) {
        online = false;
        return false;
    }
    value = ((uint16_t) reading[0] << 8) | reading[1];
    return true;
}

bool Bq76920Driver::readRaw(BqRawReadings& raw) {
    uint16_t cc = 0;
    if (!readRaw14(registerMap::VC1_HI, raw.vc1Raw) ||
        !readRaw14(registerMap::VC2_HI, raw.vc2Raw) ||
        !readRaw14(registerMap::VC5_HI, raw.vc5Raw) ||
        !readRaw16(registerMap::BAT_HI, raw.batRaw) ||
        !readRaw16(registerMap::CC_HI, cc) ||
        !readRaw14(registerMap::TS1_HI, raw.tempRaw) ||
        !readStatus(raw.sysStat) ||
        !readControl(raw.sysCtrl1, raw.sysCtrl2) ||
        !readCellBalancing(raw.cellBal))
    {
        online = false;
        return false;
    }
    raw.ccRaw = (int16_t) cc;
    online = true;
    return true;
}

bool Bq76920Driver::readStatus(uint8_t& sysStat) {
    if (!readRegister(I2C_BQ76920_ADDRESS, registerMap::SYS_STAT, &sysStat)) {
        online = false;
        return false;
    }
    return true;
}

bool Bq76920Driver::readControl(uint8_t& sysCtrl1, uint8_t& sysCtrl2) {
    uint8_t reading[2];
    if (!readRegisters(I2C_BQ76920_ADDRESS, registerMap::SYS_CTRL1, reading, 2)) {
        online = false;
        return false;
    }
    sysCtrl1 = reading[0];
    sysCtrl2 = reading[1];
    cachedSysCtrl1 = sysCtrl1;
    cachedSysCtrl2 = sysCtrl2;
    return true;
}

bool Bq76920Driver::readCellBalancing(uint8_t& cellBal) {
    if (!readRegister(I2C_BQ76920_ADDRESS, registerMap::CELLBAL1, &cellBal)) {
        online = false;
        return false;
    }
    cachedCellBal = cellBal;
    return true;
}

bool Bq76920Driver::clearFaults() {
    uint8_t faultMask = (1 << (uint8_t) SysStatusOpt::CC_READY) |
        (1 << (uint8_t) SysStatusOpt::DEVICE_XREADY) |
        (1 << (uint8_t) SysStatusOpt::OVRD_ALERT) |
        (1 << (uint8_t) SysStatusOpt::UV) |
        (1 << (uint8_t) SysStatusOpt::OV) |
        (1 << (uint8_t) SysStatusOpt::SCD) |
        (1 << (uint8_t) SysStatusOpt::OCD);
    return writeRegister(I2C_BQ76920_ADDRESS, registerMap::SYS_STAT, faultMask);
}

bool Bq76920Driver::setChargeEnabled(bool enabled) {
    if (enabled) {
        cachedSysCtrl2 |= (1 << (uint8_t) SysControlOpt::CHG_ON);
    } else {
        cachedSysCtrl2 &= ~(1 << (uint8_t) SysControlOpt::CHG_ON);
    }
    return writeSysCtrl2(cachedSysCtrl2);
}

bool Bq76920Driver::setDischargeEnabled(bool enabled) {
    if (enabled) {
        cachedSysCtrl2 |= (1 << (uint8_t) SysControlOpt::DSG_ON);
    } else {
        cachedSysCtrl2 &= ~(1 << (uint8_t) SysControlOpt::DSG_ON);
    }
    return writeSysCtrl2(cachedSysCtrl2);
}

bool Bq76920Driver::setBalancingMask(uint8_t mask) {
    mask &= (BALANCE_CELL_1 | BALANCE_CELL_2 | BALANCE_CELL_5);
    bool adjacentCell1And2 = (mask & BALANCE_CELL_1) && (mask & BALANCE_CELL_2);
    if (adjacentCell1And2) {
        Log.errorln("Refusing to balance adjacent cells.");
        return false;
    }
    if (!writeRegister(I2C_BQ76920_ADDRESS, registerMap::CELLBAL1, mask)) {
        online = false;
        return false;
    }
    cachedCellBal = mask;
    return true;
}

bool Bq76920Driver::disableBalancing() {
    return setBalancingMask(0);
}

bool Bq76920Driver::enterShipMode() {
    disableBalancing();
    setDischargeEnabled(false);
    setChargeEnabled(false);

    uint8_t base = cachedSysCtrl1 & ~((1 << (uint8_t) SysControlOpt::SHUT_A) | (1 << (uint8_t) SysControlOpt::SHUT_B));
    if (!writeSysCtrl1(base)) {
        return false;
    }
    if (!writeSysCtrl1(base | (1 << (uint8_t) SysControlOpt::SHUT_B))) {
        return false;
    }
    if (!writeSysCtrl1(base | (1 << (uint8_t) SysControlOpt::SHUT_A))) {
        return false;
    }
    online = false;
    return true;
}

uint16_t Bq76920Driver::cellMvFromRaw(uint16_t raw14) const {
    long value = (((long) raw14 * adcGainUvPerLsb) / 1000L) + adcOffsetUv;
    if (value < 0) {
        return 0;
    }
    return (uint16_t) value;
}

uint16_t Bq76920Driver::packMvFromRaw(uint16_t raw16) const {
    long value = (4L * adcGainUvPerLsb * raw16) / 1000L + (3L * adcOffsetUv);
    if (value < 0) {
        return 0;
    }
    return (uint16_t) value;
}

int16_t Bq76920Driver::currentMaFromRaw(int16_t raw16) const {
    int16_t value = (int16_t) (((long) raw16 * 844L) / (100L * currentSenseResistance));
    if (value >= -3 && value <= 3) {
        return 0;
    }
    return value;
}

int16_t Bq76920Driver::dieTempCentiCFromRaw(uint16_t raw14) const {
    float tempC = 25.0f - ((((float) raw14 * 0.000382f) - 1.20f) / 0.0042f);
    return (int16_t) (tempC * 100.0f);
}

int8_t Bq76920Driver::adcOffset() const {
    return adcOffsetUv;
}

uint16_t Bq76920Driver::adcGain() const {
    return adcGainUvPerLsb;
}

uint8_t Bq76920Driver::sysCtrl1() const {
    return cachedSysCtrl1;
}

uint8_t Bq76920Driver::sysCtrl2() const {
    return cachedSysCtrl2;
}

uint8_t Bq76920Driver::cellBalancing() const {
    return cachedCellBal;
}
