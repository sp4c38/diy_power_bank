# Powerbank Firmware and App

Firmware for the Arduino Nano 33 BLE that supervises the BQ76920-based power
bank PCB, plus the native SwiftUI iOS app that talks to it over BLE.

## Layout

- `PowerbankFirmware/` - Arduino sketch and C++ source files.
- `Powerbank/` - SwiftUI iOS app.
- `Getting Started with Bluetooth Low Energy.pdf` - BLE reference material.
- `requirements.txt` - historical dependency note from the old setup.

## Firmware Architecture

- `Bq76920Driver` owns CRC I2C, calibration, raw ADC reads, FET control,
  protection registers, balancing, fault clearing and SHIP mode.
- `PackMonitor` converts/filter readings and marks telemetry untrusted if cell
  measurements jump or become implausible.
- `SafetyPolicy` decides whether charge, discharge and balancing are allowed.
- `Balancer` performs conservative one-cell-at-a-time passive balancing.
- `PowerManager` turns the output off after idle and requests SHIP after very
  long idle.
- `BLEApp` exposes telemetry and guarded commands to the iOS app.
- `SerialConsole` mirrors the app commands for bench testing.

## Board

- Board: Arduino Nano 33 BLE
- FQBN: `arduino:mbed_nano:nano33ble`
- Current serial port: `/dev/cu.usbmodem112201`
- Serial baud rate used by the old firmware: `9600`

## Dependencies

Installed through Arduino CLI:

- `ArduinoBLE`
- `ArduinoLog`

## Useful Commands

List connected boards:

```sh
'/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' board list
```

Open the serial monitor:

```sh
'/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' monitor -p /dev/cu.usbmodem112201 -c baudrate=9600
```

Serial commands:

- `help` - print the command list.
- `status` - print state, FET status, current, pack voltage, die temperature and cell voltages.
- `faults` - print BQ76920 fault/status bits.
- `raw` - print raw ADC/register diagnostics.
- `clear_faults` - clear UV, OV, SCD and OCD fault bits.
- `output_on` / `output_off` - request the normal discharge/output path.
- `ship!` - enter BQ76920 SHIP mode. The `!` is intentional confirmation.
- `charge_on!` / `charge_off` - developer CHG FET override.
- `balance_off` - force all cell balancing off.

Balancing is automatic only when readings are trusted, cells are near the top of
charge, current is near idle, and only one cell needs bleeding.

Compile the firmware:

```sh
'/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' compile --fqbn arduino:mbed_nano:nano33ble Code/PowerbankFirmware
```

Upload the firmware:

```sh
'/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' upload -p /dev/cu.usbmodem112201 --fqbn arduino:mbed_nano:nano33ble Code/PowerbankFirmware
```

Build the iOS app:

```sh
xcodebuild -project Code/Powerbank/Powerbank.xcodeproj -scheme Powerbank -destination 'platform=iOS Simulator,name=iPhone 17' build
```

## BLE Protocol

Service UUID: `7E571000-40A1-4E31-8E9D-4AC0D8B2A100`

- Telemetry: `7E571001-40A1-4E31-8E9D-4AC0D8B2A100`, read + notify, packed
  little-endian protocol v1 payload.
- Command: `7E571002-40A1-4E31-8E9D-4AC0D8B2A100`, write, two bytes:
  command id and optional confirmation byte `0xA5`.
- Command result: `7E571003-40A1-4E31-8E9D-4AC0D8B2A100`, read + notify,
  UTF-8 status text.
- Device info: `7E571004-40A1-4E31-8E9D-4AC0D8B2A100`, read, UTF-8 firmware
  and protocol version.

## Bring-Up Notes

2026-06-26:

- Arduino Nano 33 BLE detected on `/dev/cu.usbmodem112201`.
- Firmware compiles and uploads with Arduino CLI.
- Upload may require manually double-tapping reset to enter bootloader mode.
- With cells inserted and BQ76920 booted, I2C/CRC communication works.
- Known-good idle serial baseline:

```text
Pack state: Idle; DSG: true; CHG: true; pack current: -5; pack voltage: 11334
Cell 1: 3787; Cell 2: 3775; Cell 5: 3770
```

- Longer idle observation with the USB-A output module connected is stable at
  roughly `-4` to `-6 mA`, caused by always-on LEDs on that module. This is
  acceptable during bring-up, but the firmware still needs a later low-power
  strategy so that LED load cannot drain the battery pack empty.

Earlier in bring-up, a poorly seated cell/contact produced `Cell 2: 50` and
the firmware correctly disabled discharge. Reseating fixed the reading.
