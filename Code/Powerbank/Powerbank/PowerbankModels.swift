import Foundation

enum PackState: UInt8, CaseIterable {
    case starting = 0
    case idle = 1
    case charging = 2
    case discharging = 3
    case balancing = 4
    case outputOffIdle = 5
    case fault = 6
    case sensorFault = 7
    case ship = 8
    case bqOffline = 9

    var title: String {
        switch self {
        case .starting: "Starting"
        case .idle: "Idle"
        case .charging: "Charging"
        case .discharging: "Discharging"
        case .balancing: "Balancing"
        case .outputOffIdle: "Output Off"
        case .fault: "Fault"
        case .sensorFault: "Sensor Fault"
        case .ship: "Ship Mode"
        case .bqOffline: "BQ Offline"
        }
    }

    var systemImage: String {
        switch self {
        case .starting: "power"
        case .idle: "pause.circle"
        case .charging: "bolt.fill"
        case .discharging: "arrow.down.circle"
        case .balancing: "scalemass"
        case .outputOffIdle: "powerplug.portrait"
        case .fault, .sensorFault, .bqOffline: "exclamationmark.triangle.fill"
        case .ship: "moon.zzz.fill"
        }
    }

    /// A short, friendly description of what the pack is doing right now.
    var detail: String {
        switch self {
        case .starting: "Booting up and reading the cells."
        case .idle: "Resting. No meaningful current flow."
        case .charging: "Topping up the cells."
        case .discharging: "Powering the output."
        case .balancing: "Equalising cells near full charge."
        case .outputOffIdle: "Output disabled after a period of idle."
        case .fault: "A protection fault is active."
        case .sensorFault: "Measurements can't be trusted right now."
        case .ship: "Deep sleep. Reconnect power to wake."
        case .bqOffline: "Lost contact with the battery monitor."
        }
    }

    var isHealthy: Bool {
        switch self {
        case .fault, .sensorFault, .bqOffline: false
        default: true
        }
    }
}

/// Whether energy is flowing into or out of the pack right now.
enum PowerFlow {
    case charging
    case discharging
    case idle

    var title: String {
        switch self {
        case .charging: "Charging"
        case .discharging: "Discharging"
        case .idle: "Idle"
        }
    }

    var systemImage: String {
        switch self {
        case .charging: "arrow.down.circle.fill"
        case .discharging: "arrow.up.circle.fill"
        case .idle: "minus.circle.fill"
        }
    }
}

struct Telemetry: Equatable {
    let protocolVersion: UInt8
    let state: PackState
    let flags: UInt16
    let faults: UInt16
    let balanceMask: UInt8
    let cell1Mv: UInt16
    let cell2Mv: UInt16
    let cell5Mv: UInt16
    let packMv: UInt16
    let currentMa: Int16
    let dieTempCentiC: Int16
    let socPercent: UInt8
    let uptimeSec: UInt32
    let receivedAt: Date

    // MARK: - Flag accessors (mirror firmware TelemetryFlags)
    var trusted: Bool { flags & (1 << 0) != 0 }
    var chargeEnabled: Bool { flags & (1 << 1) != 0 }
    var dischargeEnabled: Bool { flags & (1 << 2) != 0 }
    var manualChargeOff: Bool { flags & (1 << 3) != 0 }
    var manualDischargeOff: Bool { flags & (1 << 4) != 0 }
    var idleOutputOff: Bool { flags & (1 << 5) != 0 }
    var balancing: Bool { flags & (1 << 6) != 0 }
    var lowCellWarning: Bool { flags & (1 << 7) != 0 }
    var staleFromFirmware: Bool { flags & (1 << 8) != 0 }
    var bleConnected: Bool { flags & (1 << 9) != 0 }

    // MARK: - Derived values
    var temperatureC: Double { Double(dieTempCentiC) / 100.0 }
    var packVoltage: Double { Double(packMv) / 1000.0 }

    /// Power magnitude in watts, regardless of direction.
    var powerW: Double { packVoltage * abs(Double(currentMa)) / 1000.0 }

    var flow: PowerFlow {
        if currentMa > 0 { return .charging }
        if currentMa < 0 { return .discharging }
        return .idle
    }

    struct Cell: Identifiable, Equatable {
        let id: Int
        let label: String
        let mv: UInt16
        var volts: Double { Double(mv) / 1000.0 }
    }

    /// The three populated channels on the BQ76920. Channels 3 and 4 are unused
    /// on this PCB, which is why the labels skip to "Cell 5".
    var cells: [Cell] {
        [
            Cell(id: 1, label: "Cell 1", mv: cell1Mv),
            Cell(id: 2, label: "Cell 2", mv: cell2Mv),
            Cell(id: 5, label: "Cell 5", mv: cell5Mv)
        ]
    }

    var minCellMv: UInt16 { [cell1Mv, cell2Mv, cell5Mv].min() ?? 0 }
    var maxCellMv: UInt16 { [cell1Mv, cell2Mv, cell5Mv].max() ?? 0 }
    var cellDeltaMv: UInt16 { maxCellMv - minCellMv }

    /// Which cells the balancer is currently bleeding (matches firmware BalanceMask).
    func isBalancing(cellID: Int) -> Bool {
        switch cellID {
        case 1: balanceMask & (1 << 0) != 0
        case 2: balanceMask & (1 << 1) != 0
        case 5: balanceMask & (1 << 4) != 0
        default: false
        }
    }

    var decodedFaults: [PowerbankFault] { PowerbankFault.decode(faults) }
    var decodedFlags: [PowerbankFlag] { PowerbankFlag.decode(flags) }
    var hasFaults: Bool { faults != 0 }

    static func parse(_ data: Data) -> Telemetry? {
        guard data.count >= 24 else { return nil }
        var offset = 0
        func u8() -> UInt8 {
            defer { offset += 1 }
            return data[offset]
        }
        func u16() -> UInt16 {
            defer { offset += 2 }
            return UInt16(data[offset]) | (UInt16(data[offset + 1]) << 8)
        }
        func i16() -> Int16 {
            Int16(bitPattern: u16())
        }
        func u32() -> UInt32 {
            defer { offset += 4 }
            return UInt32(data[offset]) |
                (UInt32(data[offset + 1]) << 8) |
                (UInt32(data[offset + 2]) << 16) |
                (UInt32(data[offset + 3]) << 24)
        }

        let protocolVersion = u8()
        let state = PackState(rawValue: u8()) ?? .fault
        let flags = u16()
        let faults = u16()
        let balanceMask = u8()
        let cell1Mv = u16()
        let cell2Mv = u16()
        let cell5Mv = u16()
        let packMv = u16()
        let currentMa = i16()
        let dieTemp = i16()
        let soc = u8()
        let uptime = u32()

        return Telemetry(
            protocolVersion: protocolVersion,
            state: state,
            flags: flags,
            faults: faults,
            balanceMask: balanceMask,
            cell1Mv: cell1Mv,
            cell2Mv: cell2Mv,
            cell5Mv: cell5Mv,
            packMv: packMv,
            currentMa: currentMa,
            dieTempCentiC: dieTemp,
            socPercent: soc,
            uptimeSec: uptime,
            receivedAt: Date()
        )
    }
}

/// Human-readable decoding of the firmware `FaultFlags` bitmask.
struct PowerbankFault: Identifiable, Equatable {
    let id: UInt16
    let title: String
    let detail: String
    let systemImage: String

    static func decode(_ faults: UInt16) -> [PowerbankFault] {
        var result: [PowerbankFault] = []
        func add(_ bit: Int, _ title: String, _ detail: String, _ image: String) {
            let mask = UInt16(1 << bit)
            if faults & mask != 0 {
                result.append(PowerbankFault(id: mask, title: title, detail: detail, systemImage: image))
            }
        }
        add(0, "Undervoltage", "A cell dropped below the safe minimum.", "battery.0percent")
        add(1, "Overvoltage", "A cell rose above the safe maximum.", "battery.100percent.bolt")
        add(2, "Short Circuit", "Short-circuit-in-discharge protection tripped.", "bolt.trianglebadge.exclamationmark")
        add(3, "Overcurrent", "Overcurrent-in-discharge protection tripped.", "exclamationmark.arrow.triangle.2.circlepath")
        add(4, "Sensor", "Cell measurements are implausible or unstable.", "sensor.tag.radiowaves.forward")
        add(5, "Temperature", "Die temperature is out of range.", "thermometer.high")
        add(6, "BQ Offline", "Lost I2C contact with the BQ76920.", "cable.connector.slash")
        add(7, "Output Low Cell", "Output disabled to protect a low cell.", "battery.25percent")
        return result
    }
}

/// Human-readable decoding of the firmware `TelemetryFlags` bitmask, for the
/// diagnostics screen.
struct PowerbankFlag: Identifiable, Equatable {
    let id: UInt16
    let title: String
    let isOn: Bool

    static func decode(_ flags: UInt16) -> [PowerbankFlag] {
        let definitions: [(Int, String)] = [
            (0, "Measurements Trusted"),
            (1, "Charge FET On"),
            (2, "Discharge FET On"),
            (3, "Manual Charge Off"),
            (4, "Manual Discharge Off"),
            (5, "Idle Output Off"),
            (6, "Balancing"),
            (7, "Low Cell Warning"),
            (8, "Firmware Stale"),
            (9, "BLE Connected")
        ]
        return definitions.map { bit, title in
            let mask = UInt16(1 << bit)
            return PowerbankFlag(id: mask, title: title, isOn: flags & mask != 0)
        }
    }
}

enum PowerbankCommand: UInt8, CaseIterable, Identifiable {
    case outputOn = 1
    case outputOff = 2
    case clearFaults = 3
    case ship = 4
    case balanceOff = 5
    case chargeOn = 6
    case chargeOff = 7
    case dischargeOn = 8
    case dischargeOff = 9
    case rawDiagnostics = 10

    var id: UInt8 { rawValue }

    var title: String {
        switch self {
        case .outputOn: "Output On"
        case .outputOff: "Output Off"
        case .clearFaults: "Clear Faults"
        case .ship: "Ship Mode"
        case .balanceOff: "Stop Balancing"
        case .chargeOn: "Charge On"
        case .chargeOff: "Charge Off"
        case .dischargeOn: "Discharge On"
        case .dischargeOff: "Discharge Off"
        case .rawDiagnostics: "Raw Diagnostics"
        }
    }

    var systemImage: String {
        switch self {
        case .outputOn: "power"
        case .outputOff: "poweroff"
        case .clearFaults: "arrow.clockwise"
        case .ship: "moon.zzz.fill"
        case .balanceOff: "scalemass"
        case .chargeOn: "bolt.fill"
        case .chargeOff: "bolt.slash.fill"
        case .dischargeOn: "arrow.down.circle.fill"
        case .dischargeOff: "arrow.down.circle"
        case .rawDiagnostics: "doc.text.magnifyingglass"
        }
    }

    /// A plain-language note about what the command does, shown in the UI.
    var caption: String {
        switch self {
        case .outputOn: "Enable the normal discharge path to the output."
        case .outputOff: "Cut the output until you turn it back on."
        case .clearFaults: "Clear UV, OV, short-circuit and overcurrent latches."
        case .ship: "Enter deep sleep. Needs charger to wake."
        case .balanceOff: "Force all passive cell balancing off."
        case .chargeOn: "Developer override: allow the charge FET."
        case .chargeOff: "Developer override: block the charge FET."
        case .dischargeOn: "Developer override: allow the discharge FET."
        case .dischargeOff: "Developer override: block the discharge FET."
        case .rawDiagnostics: "Dump raw ADC/register diagnostics over serial."
        }
    }

    var requiresConfirmation: Bool {
        switch self {
        case .ship, .chargeOn, .dischargeOn:
            true
        default:
            false
        }
    }

    /// Commands that change power flow in a potentially surprising way get an
    /// extra in-app confirmation prompt.
    var isDestructive: Bool {
        switch self {
        case .ship, .outputOff, .chargeOff, .dischargeOff:
            true
        default:
            false
        }
    }
}

enum BLEConnectionState: Equatable {
    case idle
    case bluetoothUnavailable(String)
    case scanning
    case connecting(String)
    case connected(String)
    case disconnected

    var title: String {
        switch self {
        case .idle: "Idle"
        case .bluetoothUnavailable(let reason): "Bluetooth \(reason)"
        case .scanning: "Searching…"
        case .connecting(let name): "Connecting to \(name)…"
        case .connected(let name): "Connected to \(name)"
        case .disconnected: "Disconnected"
        }
    }

    var isConnected: Bool {
        if case .connected = self { return true }
        return false
    }

    var isBusy: Bool {
        switch self {
        case .scanning, .connecting: true
        default: false
        }
    }
}

/// Strength buckets for the BLE RSSI reading.
enum SignalStrength {
    case strong, good, fair, weak, unknown

    init(rssi: Int?) {
        guard let rssi, rssi != 127 else { self = .unknown; return }
        switch rssi {
        case ...(-90): self = .weak
        case (-89)...(-75): self = .fair
        case (-74)...(-60): self = .good
        default: self = .strong
        }
    }

    var systemImage: String {
        switch self {
        case .strong, .good: "wifi"
        case .fair: "wifi.exclamationmark"
        case .weak: "wifi.exclamationmark"
        case .unknown: "wifi.slash"
        }
    }

    var bars: Int {
        switch self {
        case .strong: 3
        case .good: 2
        case .fair: 1
        case .weak: 1
        case .unknown: 0
        }
    }
}

struct EventLogItem: Identifiable, Equatable {
    let id = UUID()
    let date = Date()
    let text: String
    var isError: Bool = false
}

extension UInt16 {
    var hexString: String { "0x" + String(format: "%04X", self) }
}

extension UInt8 {
    var hexString: String { "0x" + String(format: "%02X", self) }
}
