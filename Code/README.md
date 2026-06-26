# Powerbank Firmware

Firmware for the Arduino Nano 33 BLE that supervises the BQ76920-based power
bank PCB.

## Layout

- `PowerbankFirmware/` - Arduino sketch and C++ source files.
- `Getting Started with Bluetooth Low Energy.pdf` - BLE reference material.
- `requirements.txt` - historical dependency note from the old setup.

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
- `clear_faults` - clear UV, OV, SCD and OCD fault bits.
- `ship` - enter BQ76920 SHIP mode.
- `charge_on` / `charge_off` - toggle the CHG FET. `charge_off` is a sticky
  manual override until `charge_on` is sent.
- `discharge_on` / `discharge_off` - toggle the DSG FET. `discharge_off` is a
  sticky manual override until `discharge_on` is sent.
- `balance_off` - force all cell balancing off.

Manual/automatic balancing is disabled for now. The old `balance` command is kept
as a guarded no-op so it cannot accidentally enable cell balancing.

Compile the firmware:

```sh
'/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' compile --fqbn arduino:mbed_nano:nano33ble Code/PowerbankFirmware
```

Upload the firmware:

```sh
'/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' upload -p /dev/cu.usbmodem112201 --fqbn arduino:mbed_nano:nano33ble Code/PowerbankFirmware
```

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
