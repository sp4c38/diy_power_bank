# Firmware Hardware Map

This document captures the firmware-relevant facts extracted from the KiCad
schematic/netlist and existing firmware. It is intended as the starting point
for the power-bank firmware rewrite/audit.

## Source Evidence

- Main schematic: `Circuit/KiCAD Circuit/Powerbank.kicad_sch`
- KiCad netlist export: `Circuit/KiCAD Circuit/Powerbank.xml`
- BOM export: `Circuit/KiCAD Circuit/Bill of Materials.csv`
- Existing firmware: `Code/PowerbankFirmware`

Important caveat: `Powerbank.xml` was exported on 2024-01-12, while the KiCad
project and BOM appear to have changed later. Use this map as a firmware guide,
but verify anything safety-critical against the current schematic/board before
depending on it.

## Main Components

| Ref | Part | Firmware relevance |
| --- | --- | --- |
| U1 | BQ7692003PW | 3-5 cell Li-ion monitor/protector with I2C, CRC, FET control, cell voltage, pack voltage, current, temperature, protection, and balancing registers. |
| U2 | Arduino Nano 33 IoT socket | Firmware host. Current firmware uses Arduino APIs, Wire/I2C, serial logging, and optional ArduinoBLE. |
| Q1, Q3 | IRLB4132PBF NMOS | Pack-side protection FET network controlled through BQ76920 `DSG`/`CHG` circuitry. |
| Q2 | PMOS | Part of charge/discharge gate-control network. |
| R2 | 8 milliohm shunt | Current sense resistor for BQ76920 coulomb/current measurement. |
| SW1 | Pushbutton | Connected into the `TS1`/`VC1` area; likely used for boot/wake behavior. Confirm exact intended behavior before firmware relies on it. |
| SW2 | SPST switch | Connects the switched high-side supply path to Arduino `VIN`. |

## Arduino Pin Map

| Arduino pin | Net / target | Firmware use |
| --- | --- | --- |
| A4 / SDA | BQ76920 `SDA` | I2C data. |
| A5 / SCL | BQ76920 `SCL` | I2C clock. |
| D10 | BQ76920 `ALERT` | Alert/fault signal. Current firmware defines `ALERT_PIN = 10`, but does not actively use an interrupt yet. |
| VIN | Connected through `SW2` | Arduino supply input through the external switch path. |
| GND | Pack/BQ ground | Common reference with BQ76920 `VSS`. |

Unconnected in the netlist: Arduino `3V3`, `+5V`, `A0-A3`, `A6-A7`, `D0-D9`
except `D10`, `D11-D13`, and reset pins. That means the firmware should assume
the Arduino mainly talks to the board over I2C plus `ALERT`; it does not appear
to directly control the USB modules, buck converter, or other external modules.

## Pack Topology

The schematic/netlist describes a 3-cell series pack:

| Physical node | Netlist evidence | BQ76920 sense node |
| --- | --- | --- |
| Pack positive / cell 1 positive | `PACK +`, `CELL_1 +` | Through `Rc1` to `VC5` |
| Cell 1 negative / cell 2 positive | `CELL_1 -`, `CELL_2 +` | Through `Rc2` to tied `VC2`, `VC3`, `VC4` |
| Cell 2 negative / cell 3 positive | `CELL_2 -`, `CELL_3 +` | Through `Rc3` to `VC1` |
| Cell 3 negative / pack/BQ ground | `CELL_3 -`, `GND` | Through `Rc4` to `VC0` / `VSS` |

This matches the existing firmware reading `VC1_HI`, `VC2_HI`, and `VC5_HI`.
The cell labels in firmware should be made explicit because the BQ register
names do not read like physical cell numbers in this 3S configuration.

Proposed firmware naming:

| Firmware name | BQ reading | Physical voltage |
| --- | --- | --- |
| `cellBottomMv` | `VC1` | Bottom cell, between `VC1` and `VC0` |
| `cellMiddleMv` | `VC2` | Middle cell, between `VC2` and `VC1` |
| `cellTopMv` | `VC5` | Top cell, between `VC5` and tied `VC2/VC3/VC4` |

## BQ76920 Connections

| BQ76920 pin/function | Netlist connection | Firmware implication |
| --- | --- | --- |
| `SDA` | Arduino `A4_SDA` | Use `Wire` at BQ address `0x08` with CRC enabled. |
| `SCL` | Arduino `A5_SCL` | Current firmware uses 100 kHz I2C. |
| `ALERT` | Arduino `D10`, pull/filter network `R4`/`C4` | Should be monitored, ideally by interrupt or periodic read. |
| `SRP` / `SRN` | Across 8 milliohm current-sense path via `R1`/`R3` and filter caps | Current calculation must use 8 milliohm sense resistance. |
| `BAT` / `REGSRC` | Through `Rf1`, `Cf1`, diode/resistor network | BQ supply / pack voltage measurement path. |
| `REGOUT` | `C9` decoupling | Internal LDO output; do not treat as Arduino supply unless verified. |
| `CAP1` | `C7` decoupling | BQ internal charge-pump support. |
| `TS1` | `R5`, `R6`, `C5`, `SW1` network | Current firmware reads internal die temperature by keeping `TEMP_SEL = false`; external thermistor behavior needs verification. |
| `CHG` | Q2 source / gate-control network | Controls charge FET behavior indirectly. |
| `DSG` | Q1 gate through `R7` | Controls discharge FET behavior. |

## Protection FET / Pack Path

The netlist shows a low-side protection/sense arrangement:

- `PACK -` connects to Q3 source and `R8`.
- Q1 and Q3 drains are tied together.
- Q1 source connects to the current-sense/shunt side through R2 and the SRN
  filter network.
- BQ `DSG` drives Q1 gate through the `R7` network.
- BQ `CHG` drives Q2, which in turn affects Q3 gate through the `R8` network.

Firmware should therefore treat `DSG_ON` and `CHG_ON` as the authoritative BQ
control bits for enabling/disabling discharge and charge. It should not assume
direct Arduino GPIO control of the power path.

## Existing Firmware Constants To Recheck

Current values in `Code/PowerbankFirmware/register.h`:

| Constant | Current value | Meaning |
| --- | ---: | --- |
| `upperVoltageLimit` | 4190 mV | Firmware charge/balance limit. |
| `lowerVoltageLimit` | 2600 mV | Firmware discharge cutoff threshold. |
| `cvCurrentCutOff` | 200 mA | Constant-voltage completion threshold. |
| `allowedBalancingDifference` | 30 mV | Cell delta before balancing starts. |
| `balancingDifference` | 5 mV | Cell delta target before balancing stops. |
| `dischargingThreshold` | -50 mA | Current below this is considered discharging. |
| `currentSenseResistance` | 8 milliohm | Matches BOM value for R2. |
| `I2C_BQ76920_ADDRESS` | `0x08` | BQ7692003PW with CRC enabled. |
| `ALERT_PIN` | `10` | Matches Arduino D10 connection. |

These are plausible, but should be reviewed before final firmware because they
define real battery safety behavior.

## Existing Code Cross-Check

The old firmware is useful evidence because it reportedly worked on the real
PCB. Comparing it against the schematic/netlist shows that the core assumptions
line up:

| Topic | Existing firmware | Circuit evidence | Result |
| --- | --- | --- | --- |
| BQ device/address | `I2C_BQ76920_ADDRESS = 0x08` | `U1 = BQ7692003PW`, CRC-enabled variant expected by comments/code | Consistent. |
| I2C pins | `SDA_PIN = 18`, `SCL_PIN = 19`; `Wire.begin()` uses board defaults | BQ `SDA` -> Arduino `A4_SDA`, BQ `SCL` -> Arduino `A5_SCL` | Consistent. Pin constants are currently documentation only. |
| Alert pin | `ALERT_PIN = 10` | BQ `ALERT` -> Arduino `D10` | Consistent, but old firmware does not actively use the pin. |
| Current shunt | `currentSenseResistance = 8` milliohm | BOM: `R2 = 8m`; netlist places R2 in the SRP/SRN current path | Consistent. |
| Cell readings | Reads `VC1_HI`, `VC2_HI`, `VC5_HI` | 3S topology uses BQ nodes `VC0`, `VC1`, tied `VC2/VC3/VC4`, and `VC5` | Consistent. |
| Balancing bits | Uses `CB1`, `CB2`, `CB5` | Same BQ cell nodes used for the 3S measurement stack | Consistent with the selected BQ nodes. |
| FET control | Uses BQ `SYS_CTRL2` bits `DSG_ON` and `CHG_ON` | BQ `DSG` and `CHG` feed the MOSFET gate-control network | Consistent. |
| Temperature mode | `TEMP_SEL = false`; reads `TS1_HI` as internal die temperature | TS1 has a resistor/capacitor/switch network, but old code worked with internal temperature mode | Consistent for old behavior; external temperature use remains unverified. |
| Boot/shutdown | Assumes BQ is already booted; implements BQ SHIP sequence | Hardware includes `SW1`/`SW2`, and Arduino has no direct GPIO control of the power path | Consistent with manual wake/power hardware. |

No hardware-code mismatch was found in the essential firmware-facing
connections. The things to improve are mostly software robustness, clarity, and
power management rather than correcting the basic hardware model.

Minor code issues to clean up later, without changing hardware assumptions:

- `readRegisters()` and `writeRegister()` accept an I2C address parameter but
  internally use the global BQ address in a couple of places. This is harmless
  for this single-chip project, but confusing.
- Cell labels are currently logged as `Cell 1`, `Cell 2`, and `Cell 5`, matching
  BQ register names rather than a user-facing 3-cell pack view.
- `ALERT_PIN` is defined but not yet used for interrupt-driven fault handling
  or low-power wake behavior.
- The firmware waits forever for serial in `setup()`, which was fine for bench
  testing but not for standalone use.

## BQ Registers Already Used

The existing firmware already touches these BQ76920 functions:

- `CC_CFG`: written to `0x19` during setup.
- `ADCGAIN1`, `ADCGAIN2`, `ADCOFFSET`: used for voltage calibration.
- `VC1_HI`, `VC2_HI`, `VC5_HI`: cell voltage readings.
- `BAT_HI`: pack voltage.
- `CC_HI`: current/coulomb-counter reading.
- `TS1_HI`: temperature reading.
- `SYS_STAT`: fault flags.
- `SYS_CTRL1`, `SYS_CTRL2`: ADC, CC, CHG, DSG, SHIP sequence.
- `PROTECT1`, `PROTECT2`, `PROTECT3`, `OV_TRIP`, `UV_TRIP`: protection thresholds.
- `CELLBAL1`: cell balancing.

## Firmware Design Implications

1. The Arduino should be treated as a supervisor/UI node. The BQ76920 is the
   actual battery protection and measurement device.
2. Serial-only firmware should come before BLE. We need a trusted status loop
   first: BQ init, readings, fault handling, FET state, and SHIP mode.
3. The `ALERT` pin should become part of the safety loop. Polling `SYS_STAT`
   alone is useful, but `ALERT` lets firmware react quickly and sleep more
   efficiently.
4. Current firmware reads only internal die temperature. If the TS1 network was
   intended as an external thermistor or wake button path, that behavior needs
   a bench check.
5. Low-power behavior must account for hardware outside firmware control. If
   USB modules or converters draw idle current independently, firmware sleep
   alone will not be enough.
6. The BQ SHIP mode sequence is already implemented and should be preserved,
   but we need a clear policy for when firmware enters it.
7. BLE should be telemetry-first: expose pack status, cell voltages, current,
   temperatures, FET states, faults, balancing state, and uptime. Control
   commands should be restricted or guarded.

## Open Questions For Bench Verification

- Does `SW1` wake the BQ from SHIP mode, boot the pack, or serve another role?
- Does `SW2` fully remove Arduino power, or only switch part of the supply path?
- What is the actual idle current with CHG/DSG enabled and no load?
- What is the actual idle current with Arduino awake, asleep, BLE advertising,
  and BLE connected?
- Are the USB-A, USB-C, and buck modules powered from `PACK +` / `PACK -`
  after the protection FETs, and do they have enable pins not present in the
  KiCad netlist?
- Is there an external temperature sensor fitted, or should firmware only rely
  on BQ die temperature?
- Which physical cell should be displayed as cell 1 in the user interface:
  bottom-to-top electrical order, or the labels used in the schematic?

## Suggested First Firmware Milestone

Create a serial-only supervisor firmware that:

1. Initializes BQ76920 safely.
2. Verifies I2C/CRC communication.
3. Reads and prints all three cell voltages, pack voltage, current, temperature,
   `SYS_STAT`, and CHG/DSG state.
4. Exposes serial commands: `status`, `faults`, `clear_faults`, `ship`,
   `charge_on`, `charge_off`, `discharge_on`, `discharge_off`, and `balance_off`.
5. Keeps all balancing disabled unless explicitly testing it.
6. Never enables charge/discharge if voltage, current, temperature, or BQ fault
   status is outside the configured safety envelope.
