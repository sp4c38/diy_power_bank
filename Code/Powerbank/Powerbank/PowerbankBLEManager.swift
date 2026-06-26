import Combine
import CoreBluetooth
import Foundation

@MainActor
final class PowerbankBLEManager: NSObject, ObservableObject {
    @Published var connectionState: BLEConnectionState = .idle
    @Published var telemetry: Telemetry?
    @Published var deviceInfo = "Unknown firmware"
    @Published var commandResult = "No command sent yet"
    @Published var lastCommand: PowerbankCommand?
    @Published var events: [EventLogItem] = []
    @Published var rssi: Int?
    @Published var now = Date()

    private let serviceUUID = CBUUID(string: "7E571000-40A1-4E31-8E9D-4AC0D8B2A100")
    private let telemetryUUID = CBUUID(string: "7E571001-40A1-4E31-8E9D-4AC0D8B2A100")
    private let commandUUID = CBUUID(string: "7E571002-40A1-4E31-8E9D-4AC0D8B2A100")
    private let commandResultUUID = CBUUID(string: "7E571003-40A1-4E31-8E9D-4AC0D8B2A100")
    private let deviceInfoUUID = CBUUID(string: "7E571004-40A1-4E31-8E9D-4AC0D8B2A100")

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var commandCharacteristic: CBCharacteristic?
    private var timer: Timer?

    /// When the user explicitly disconnects we stop auto-reconnecting until they
    /// ask us to scan again.
    private var autoReconnect = true

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { _ in
            DispatchQueue.main.async { [weak self] in
                self?.now = Date()
                self?.refreshSignal()
            }
        }
    }

    // MARK: - Derived state

    var isTelemetryStale: Bool {
        guard let telemetry else { return true }
        return now.timeIntervalSince(telemetry.receivedAt) > 8 || telemetry.staleFromFirmware
    }

    var canSendCommands: Bool {
        connectionState.isConnected && commandCharacteristic != nil
    }

    var signalStrength: SignalStrength { SignalStrength(rssi: rssi) }

    var lastTelemetryAge: TimeInterval? {
        guard let telemetry else { return nil }
        return now.timeIntervalSince(telemetry.receivedAt)
    }

    // MARK: - User actions

    func startScanning() {
        autoReconnect = true
        guard central.state == .poweredOn else {
            connectionState = .bluetoothUnavailable(Self.bluetoothReason(central.state))
            return
        }
        guard !connectionState.isConnected else { return }
        appendEvent("Searching for Powerbank")
        connectionState = .scanning
        central.scanForPeripherals(withServices: [serviceUUID], options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    func disconnect() {
        autoReconnect = false
        central.stopScan()
        if let peripheral {
            appendEvent("Disconnect requested")
            central.cancelPeripheralConnection(peripheral)
        } else {
            connectionState = .disconnected
        }
    }

    /// Single entry point for the connection button: scan if idle, otherwise drop.
    func toggleConnection() {
        if connectionState.isConnected || connectionState.isBusy {
            disconnect()
        } else {
            startScanning()
        }
    }

    func send(_ command: PowerbankCommand, confirmed: Bool = false) {
        guard let peripheral, let commandCharacteristic else {
            commandResult = "Not connected"
            appendEvent("Command blocked: not connected", isError: true)
            return
        }
        if command.requiresConfirmation && !confirmed {
            commandResult = "\(command.title) needs confirmation"
            appendEvent(commandResult, isError: true)
            return
        }
        let payload = Data([command.rawValue, confirmed ? 0xA5 : 0x00])
        peripheral.writeValue(payload, for: commandCharacteristic, type: .withResponse)
        lastCommand = command
        appendEvent("Sent \(command.title)")
    }

    func clearEvents() {
        events.removeAll()
    }

    // MARK: - Internals

    private func refreshSignal() {
        guard connectionState.isConnected else { return }
        peripheral?.readRSSI()
    }

    private func appendEvent(_ text: String, isError: Bool = false) {
        events.insert(EventLogItem(text: text, isError: isError), at: 0)
        if events.count > 80 {
            events.removeLast(events.count - 80)
        }
    }

    private static func bluetoothReason(_ state: CBManagerState) -> String {
        switch state {
        case .unknown: "state unknown"
        case .resetting: "resetting"
        case .unsupported: "unsupported"
        case .unauthorized: "access denied"
        case .poweredOff: "turned off"
        case .poweredOn: "ready"
        @unknown default: "unavailable"
        }
    }
}

extension PowerbankBLEManager: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            if central.state == .poweredOn {
                if self.autoReconnect {
                    self.startScanning()
                }
            } else {
                self.connectionState = .bluetoothUnavailable(Self.bluetoothReason(central.state))
                self.appendEvent("Bluetooth \(Self.bluetoothReason(central.state))", isError: true)
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        Task { @MainActor in
            self.central.stopScan()
            self.peripheral = peripheral
            self.rssi = RSSI.intValue
            peripheral.delegate = self
            let name = peripheral.name ?? "Powerbank"
            self.connectionState = .connecting(name)
            self.appendEvent("Found \(name)")
            self.central.connect(peripheral)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            let name = peripheral.name ?? "Powerbank"
            self.connectionState = .connected(name)
            self.appendEvent("Connected to \(name)")
            peripheral.readRSSI()
            peripheral.discoverServices([self.serviceUUID])
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            self.connectionState = .disconnected
            self.commandCharacteristic = nil
            self.rssi = nil
            self.appendEvent(error.map { "Disconnected: \($0.localizedDescription)" } ?? "Disconnected", isError: error != nil)
            if self.autoReconnect {
                self.startScanning()
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            self.connectionState = .disconnected
            self.appendEvent(error.map { "Connect failed: \($0.localizedDescription)" } ?? "Connect failed", isError: true)
            if self.autoReconnect {
                self.startScanning()
            }
        }
    }
}

extension PowerbankBLEManager: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didReadRSSI RSSI: NSNumber, error: Error?) {
        Task { @MainActor in
            guard error == nil else { return }
            self.rssi = RSSI.intValue
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        Task { @MainActor in
            if let error {
                self.appendEvent("Service discovery failed: \(error.localizedDescription)", isError: true)
                return
            }
            peripheral.services?.forEach { service in
                peripheral.discoverCharacteristics([self.telemetryUUID, self.commandUUID, self.commandResultUUID, self.deviceInfoUUID], for: service)
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        Task { @MainActor in
            if let error {
                self.appendEvent("Characteristic discovery failed: \(error.localizedDescription)", isError: true)
                return
            }
            service.characteristics?.forEach { characteristic in
                switch characteristic.uuid {
                case self.telemetryUUID:
                    peripheral.setNotifyValue(true, for: characteristic)
                    peripheral.readValue(for: characteristic)
                case self.commandUUID:
                    self.commandCharacteristic = characteristic
                case self.commandResultUUID:
                    peripheral.setNotifyValue(true, for: characteristic)
                    peripheral.readValue(for: characteristic)
                case self.deviceInfoUUID:
                    peripheral.readValue(for: characteristic)
                default:
                    break
                }
            }
            self.appendEvent("Powerbank ready")
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        Task { @MainActor in
            if let error {
                self.appendEvent("Read failed: \(error.localizedDescription)", isError: true)
                return
            }
            guard let data = characteristic.value else { return }
            switch characteristic.uuid {
            case self.telemetryUUID:
                if let parsed = Telemetry.parse(data) {
                    self.telemetry = parsed
                } else {
                    self.appendEvent("Telemetry parse failed", isError: true)
                }
            case self.commandResultUUID:
                self.commandResult = String(data: data, encoding: .utf8) ?? "Unreadable command result"
                self.appendEvent(self.commandResult)
            case self.deviceInfoUUID:
                self.deviceInfo = String(data: data, encoding: .utf8) ?? "Unknown firmware"
            default:
                break
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        Task { @MainActor in
            if let error {
                self.commandResult = "Write failed: \(error.localizedDescription)"
                self.appendEvent(self.commandResult, isError: true)
            }
        }
    }
}
