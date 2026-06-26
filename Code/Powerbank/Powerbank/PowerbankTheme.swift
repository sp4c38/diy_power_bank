import SwiftUI

/// Shared colours and formatting so every screen speaks the same visual language.
enum Theme {
    static let cardCornerRadius: CGFloat = 20
    static let cardBackground = AnyShapeStyle(.thinMaterial)
    static let motion = Animation.snappy(duration: 0.35)

    /// Battery state-of-charge colour: red when low, amber mid, green when healthy.
    static func socColor(_ soc: Int) -> Color {
        switch soc {
        case ..<15: .red
        case ..<35: .orange
        case ..<60: .yellow
        default: .green
        }
    }

    static func flowColor(_ flow: PowerFlow) -> Color {
        switch flow {
        case .charging: .green
        case .discharging: .blue
        case .idle: .secondary
        }
    }

    static func stateColor(_ state: PackState) -> Color {
        switch state {
        case .charging, .balancing: .green
        case .discharging: .blue
        case .idle, .outputOffIdle, .starting: .secondary
        case .ship: .indigo
        case .fault, .sensorFault, .bqOffline: .red
        }
    }

    /// Per-cell colour relative to the operating window (3.1 V empty → 4.15 V full).
    static func cellColor(_ mv: UInt16) -> Color {
        switch mv {
        case ..<3200: .red
        case ..<3500: .orange
        case 4180...: .mint
        default: .green
        }
    }
}

enum Format {
    static func volts(_ mv: UInt16) -> String {
        String(format: "%.3f V", Double(mv) / 1000.0)
    }

    static func volts(_ v: Double) -> String {
        String(format: "%.2f V", v)
    }

    static func current(_ ma: Int16) -> String {
        if ma == 0 { return "0 mA" }
        return String(format: "%+d mA", ma)
    }

    static func power(_ w: Double) -> String {
        String(format: "%.1f W", w)
    }

    static func temperature(_ c: Double) -> String {
        String(format: "%.1f °C", c)
    }

    static func duration(_ hours: Double) -> String {
        let totalMinutes = max(0, Int((hours * 60).rounded()))
        let h = totalMinutes / 60
        let m = totalMinutes % 60
        return h > 0 ? "\(h)h \(m)m" : "\(m)m"
    }

    static func uptime(_ seconds: UInt32) -> String {
        let days = seconds / 86_400
        let hours = (seconds % 86_400) / 3600
        let minutes = (seconds % 3600) / 60
        let secs = seconds % 60
        if days > 0 { return "\(days)d \(hours)h" }
        if hours > 0 { return "\(hours)h \(minutes)m" }
        if minutes > 0 { return "\(minutes)m \(secs)s" }
        return "\(secs)s"
    }
}
