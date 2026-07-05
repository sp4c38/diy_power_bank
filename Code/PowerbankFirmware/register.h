#ifndef REGISTER_H
#define REGISTER_H

#include <Arduino.h>
#include <stdint.h>

const char FIRMWARE_VERSION[] = "0.4.0";
const uint8_t BLE_PROTOCOL_VERSION = 3;

const uint8_t I2C_BQ76920_ADDRESS = 0x08;
const uint8_t ALERT_PIN = 10;
const uint8_t SDA_PIN = 18; // A4
const uint8_t SCL_PIN = 19; // A5
const uint8_t currentSenseResistance = 8; // mOhm

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

enum class SysStatusOpt : uint8_t {
    CC_READY = 7,
    DEVICE_XREADY = 5,
    OVRD_ALERT = 4,
    UV = 3,
    OV = 2,
    SCD = 1,
    OCD = 0
};

enum class SysControlOpt : uint8_t {
    ADC_EN = 4,
    TEMP_SEL = 3,
    SHUT_A = 1,
    SHUT_B = 0,
    DELAY_DIS = 7,
    CC_EN = 6,
    CC_ONESHOT = 5,
    DSG_ON = 1,
    CHG_ON = 0
};

enum BalanceMask : uint8_t {
    BALANCE_CELL_1 = 1 << 0,
    BALANCE_CELL_2 = 1 << 1,
    BALANCE_CELL_5 = 1 << 4
};

namespace thresholds {
    const uint16_t chargeStopMv = 4150;
    const uint16_t chargeResumeMv = 4050;
    const uint16_t lowWarnMv = 3300;
    const uint16_t outputOffMv = 3100;
    const uint16_t criticalShipMv = 2850;
    const uint16_t hardwareOvMv = 4230;
    const uint16_t hardwareUvMv = 2700;

    // Coulomb-counting state of charge. The usable capacity is the charge that
    // flows between the 0 % anchor (min cell at outputOffMv) and the 100 % anchor
    // (charge terminated at chargeStopMv). It is less than the NCR18650B's 3350 mAh
    // typical rating because the 4.15 V / 3.10 V window trims both ends of the cell
    // curve. 3S1P pack, so series count does not multiply capacity. Tunable.
    const uint16_t usableCapacityMah = 3000;

    const uint16_t balanceMinCellMv = 4000;
    const uint16_t balanceStartDeltaMv = 20;
    const uint16_t balanceStopDeltaMv = 10;
    const unsigned long balanceMaxDurationMs = 30UL * 60UL * 1000UL;

    const int16_t idleCurrentMa = 20;
    const int16_t loadCurrentMa = -50;
    const unsigned long outputIdleTimeoutMs = 15UL * 60UL * 1000UL;
    const unsigned long veryLongIdleShipMs = 3UL * 24UL * 60UL * 60UL * 1000UL;

    // Robustness: the Arduino is wired straight to the battery (not behind the
    // BQ FETs), so BLE reachability and MCU shutdown are cell-protection issues.
    const unsigned long advertiseKickIntervalMs = 5UL * 60UL * 1000UL;
    const unsigned long bleShutdownDrainMs = 1000UL;
    // Advertising interval in 0.625 ms units: fast while in use so the app
    // connects instantly, slow (2.5 s) once the output has idled off to save
    // radio power at the cost of a few seconds of connect latency.
    const uint16_t advertisingFastUnits = 160;   // 100 ms
    const uint16_t advertisingSlowUnits = 4000;  // 2.5 s
    const uint32_t watchdogTimeoutMs = 16000UL;
    // Self-heal reboot for a wedged BLE stack. Skipped while the very-long-idle
    // ship countdown runs, so it cannot postpone SHIP indefinitely.
    const unsigned long maintenanceRebootMs = 24UL * 60UL * 60UL * 1000UL;

    const uint16_t maxTrustedCellDeltaMv = 250;
    const uint16_t maxPackMismatchMv = 650;
    const uint16_t maxIdleSampleJumpMv = 90;
    const int16_t minDieTempCentiC = -2000;
    const int16_t maxDieTempCentiC = 6500;
}

namespace bleUuid {
    const char service[] = "7E571000-40A1-4E31-8E9D-4AC0D8B2A100";
    const char telemetry[] = "7E571001-40A1-4E31-8E9D-4AC0D8B2A100";
    const char command[] = "7E571002-40A1-4E31-8E9D-4AC0D8B2A100";
    const char commandResult[] = "7E571003-40A1-4E31-8E9D-4AC0D8B2A100";
    const char deviceInfo[] = "7E571004-40A1-4E31-8E9D-4AC0D8B2A100";
    const char historyControl[] = "7E571005-40A1-4E31-8E9D-4AC0D8B2A100";
    const char historyData[] = "7E571006-40A1-4E31-8E9D-4AC0D8B2A100";
    const char health[] = "7E571007-40A1-4E31-8E9D-4AC0D8B2A100";
    const char timeSync[] = "7E571008-40A1-4E31-8E9D-4AC0D8B2A100";
}

enum class PackState : uint8_t {
    Starting = 0,
    Idle = 1,
    Charging = 2,
    Discharging = 3,
    Balancing = 4,
    OutputOffIdle = 5,
    Fault = 6,
    SensorFault = 7,
    Ship = 8,
    BqOffline = 9
};

enum TelemetryFlags : uint16_t {
    FLAG_MEASUREMENTS_TRUSTED = 1 << 0,
    FLAG_CHG_ON = 1 << 1,
    FLAG_DSG_ON = 1 << 2,
    FLAG_MANUAL_CHARGE_OFF = 1 << 3,
    FLAG_MANUAL_DISCHARGE_OFF = 1 << 4,
    FLAG_IDLE_OUTPUT_OFF = 1 << 5,
    FLAG_BALANCING = 1 << 6,
    FLAG_LOW_CELL_WARN = 1 << 7,
    FLAG_STALE = 1 << 8,
    FLAG_BLE_CONNECTED = 1 << 9,
    FLAG_CHARGE_COMPLETE = 1 << 10,
    FLAG_BALANCE_TIMEOUT = 1 << 11
};

enum FaultFlags : uint16_t {
    FAULT_NONE = 0,
    FAULT_BQ_UV = 1 << 0,
    FAULT_BQ_OV = 1 << 1,
    FAULT_BQ_SCD = 1 << 2,
    FAULT_BQ_OCD = 1 << 3,
    FAULT_SENSOR = 1 << 4,
    FAULT_TEMP = 1 << 5,
    FAULT_BQ_OFFLINE = 1 << 6,
    FAULT_OUTPUT_LOW_CELL = 1 << 7,
    FAULT_BQ_XREADY = 1 << 8
};

enum class CommandId : uint8_t {
    None = 0,
    OutputOn = 1,
    OutputOff = 2,
    ClearFaults = 3,
    Ship = 4,
    BalanceOff = 5,
    ChargeOn = 6,
    ChargeOff = 7,
    DischargeOn = 8,
    DischargeOff = 9,
    RawDiagnostics = 10,
    ResetLearnedBattery = 11
};

typedef bool (*CommandHandler)(CommandId command, bool confirmed, char* result, size_t resultSize);

struct BqRawReadings {
    uint16_t vc1Raw = 0;
    uint16_t vc2Raw = 0;
    uint16_t vc5Raw = 0;
    uint16_t batRaw = 0;
    int16_t ccRaw = 0;
    uint16_t tempRaw = 0;
    uint8_t sysStat = 0;
    uint8_t sysCtrl1 = 0;
    uint8_t sysCtrl2 = 0;
    uint8_t cellBal = 0;
};

struct PackSnapshot {
    uint16_t cell1Mv = 0;
    uint16_t cell2Mv = 0;
    uint16_t cell5Mv = 0;
    uint16_t packMv = 0;
    int16_t currentMa = 0;
    int16_t dieTempCentiC = 0;
    uint8_t balanceMask = 0;
    uint8_t sysStat = 0;
    uint8_t sysCtrl1 = 0;
    uint8_t sysCtrl2 = 0;
    PackState state = PackState::Starting;
    uint16_t faultFlags = FAULT_NONE;
    bool trusted = false;
    bool lowCellWarning = false;
    bool stale = true;
    unsigned long updatedAtMs = 0;
};

#pragma pack(push, 1)
struct TelemetryPayload {
    uint8_t protocolVersion;
    uint8_t state;
    uint16_t flags;
    uint16_t faults;
    uint8_t balanceMask;
    uint16_t cell1Mv;
    uint16_t cell2Mv;
    uint16_t cell5Mv;
    uint16_t packMv;
    int16_t currentMa;
    int16_t dieTempCentiC;
    uint8_t socPercent;
    uint32_t uptimeSec;
    uint16_t chargeMahTenths; // coulomb-counted charge in 0.1 mAh units (protocol v2)
    uint16_t idleRemainingSec; // protocol v3; 0xFFFF when no countdown is active
};

struct CommandPayload {
    uint8_t command;
    uint8_t confirmation;
};

struct HistoryRequestPayload {
    uint8_t operation; // 1 = stream records after sequence, 2 = cancel
    uint32_t afterSequence;
};

struct HistoryStatusPayload {
    uint8_t state; // 0 = idle, 1 = streaming, 2 = complete
    uint32_t oldestSequence;
    uint32_t latestSequence;
    uint32_t bootId;
};

struct HistoryRecordPayload {
    uint32_t sequence;
    uint32_t bootId;
    uint32_t epochSec; // 0 when wall-clock time was unavailable
    uint32_t uptimeSec;
    uint16_t flags;
    uint16_t faults;
    uint16_t cell1Mv;
    uint16_t cell2Mv;
    uint16_t cell5Mv;
    uint16_t packMv;
    int16_t currentMa;
    int16_t dieTempCentiC;
    uint8_t socPercent;
    uint8_t state;
};

struct HistoryChunkPayload {
    uint32_t sequence;
    uint8_t chunkIndex;
    uint8_t chunkCount;
    uint8_t payloadLength;
    uint8_t payload[13];
};

struct HealthPayload {
    uint8_t version;
    uint8_t confidence; // 0 = learning, 1 = estimated
    uint16_t learnedCapacityMah;
    uint32_t totalDischargedMah;
    uint32_t totalEnergyWhTenths;
    uint32_t equivalentCyclesTenths;
    uint32_t hotMinutes;
    int16_t maximumTempCentiC;
    uint16_t averageIdleDeltaMv;
    uint16_t maximumIdleDeltaMv;
    uint16_t validCapacityCycles;
};

struct TimeSyncPayload {
    uint32_t unixTime;
};
#pragma pack(pop)

static_assert(sizeof(TelemetryPayload) == 28, "Telemetry protocol v3 layout changed");
static_assert(sizeof(HistoryRecordPayload) == 34, "History record layout changed");
static_assert(sizeof(HistoryChunkPayload) == 20, "History chunks must fit the minimum BLE payload");
static_assert(sizeof(HealthPayload) == 28, "Health payload layout changed");

#endif
