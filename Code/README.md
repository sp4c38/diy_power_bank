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

Compile the firmware:

```sh
'/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' compile --fqbn arduino:mbed_nano:nano33ble Code/PowerbankFirmware
```

Upload the firmware:

```sh
'/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' upload -p /dev/cu.usbmodem112201 --fqbn arduino:mbed_nano:nano33ble Code/PowerbankFirmware
```
