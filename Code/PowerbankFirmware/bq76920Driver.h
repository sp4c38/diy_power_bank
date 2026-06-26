#ifndef BQ76920_DRIVER_H
#define BQ76920_DRIVER_H

#include <Arduino.h>

#include "register.h"

class Bq76920Driver {
public:
    bool begin();
    bool isOnline() const;
    void markOffline();

    bool readRaw(BqRawReadings& raw);
    bool readStatus(uint8_t& sysStat);
    bool readControl(uint8_t& sysCtrl1, uint8_t& sysCtrl2);
    bool readCellBalancing(uint8_t& cellBal);
    bool clearFaults();

    bool setChargeEnabled(bool enabled);
    bool setDischargeEnabled(bool enabled);
    bool setBalancingMask(uint8_t mask);
    bool disableBalancing();
    bool enterShipMode();

    uint16_t cellMvFromRaw(uint16_t raw14) const;
    uint16_t packMvFromRaw(uint16_t raw16) const;
    int16_t currentMaFromRaw(int16_t raw16) const;
    int16_t dieTempCentiCFromRaw(uint16_t raw14) const;

    int8_t adcOffset() const;
    uint16_t adcGain() const;
    uint8_t sysCtrl1() const;
    uint8_t sysCtrl2() const;
    uint8_t cellBalancing() const;

private:
    bool readOffsetAndGain();
    bool pushProtection();
    bool pushControl();
    bool writeSysCtrl1(uint8_t value);
    bool writeSysCtrl2(uint8_t value);
    bool readRaw14(uint8_t address, uint16_t& value);
    bool readRaw16(uint8_t address, uint16_t& value);

    bool online = false;
    int8_t adcOffsetUv = 0;
    uint16_t adcGainUvPerLsb = 365;
    uint8_t cachedSysCtrl1 = (1 << (uint8_t) SysControlOpt::ADC_EN);
    uint8_t cachedSysCtrl2 = (1 << (uint8_t) SysControlOpt::CC_EN) |
        (1 << (uint8_t) SysControlOpt::DSG_ON) |
        (1 << (uint8_t) SysControlOpt::CHG_ON);
    uint8_t cachedCellBal = 0;
};

#endif
