import SwiftUI

/// A titled, rounded card used to group related content on every screen.
struct SectionCard<Content: View>: View {
    var title: String?
    var systemImage: String?
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            if let title {
                Label {
                    Text(title)
                } icon: {
                    if let systemImage { Image(systemName: systemImage) }
                }
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(.secondary)
                .labelStyle(.titleAndIcon)
            }
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(18)
        .background(Theme.cardBackground, in: RoundedRectangle(cornerRadius: Theme.cardCornerRadius))
    }
}

/// Compact state-of-charge surface for the dashboard.
struct BatteryStatusView: View {
    let soc: Int
    let state: PackState
    let flow: PowerFlow
    let currentMa: Int16

    private var tint: Color { Theme.socColor(soc) }
    private var isCharging: Bool { flow == .charging }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(alignment: .firstTextBaseline, spacing: 12) {
                ChargePercentText(soc: soc, tint: tint, isCharging: isCharging)

                Spacer(minLength: 8)

                Image(systemName: state.systemImage)
                    .font(.title3.weight(.semibold))
                    .foregroundStyle(Theme.stateColor(state))
                    .frame(width: 36, height: 36)
                    .background(Theme.stateColor(state).opacity(0.14), in: Circle())
                    .contentTransition(.symbolEffect(.replace))
            }

            BatteryLevelBar(soc: soc, tint: tint, isCharging: isCharging)

            HStack(alignment: .firstTextBaseline) {
                Text(state.title)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(Theme.stateColor(state))

                Spacer(minLength: 8)

                Text(Format.current(currentMa))
                    .font(.subheadline.monospacedDigit().weight(.semibold))
                    .foregroundStyle(Theme.flowColor(flow))
            }
        }
        .padding(18)
        .background(Theme.cardBackground, in: RoundedRectangle(cornerRadius: Theme.cardCornerRadius))
        .animation(Theme.motion, value: soc)
        .animation(Theme.motion, value: state)
        .animation(Theme.motion, value: flow)
        .animation(Theme.motion, value: currentMa)
    }
}

private struct ChargePercentText: View {
    let soc: Int
    let tint: Color
    let isCharging: Bool

    var body: some View {
        if isCharging {
            TimelineView(.animation) { timeline in
                let phase = timeline.date.timeIntervalSinceReferenceDate
                let pulse = 0.5 + 0.5 * sin(phase * 4.0)
                percentText
                    .scaleEffect(1.0 + pulse * 0.10)
                    .shadow(color: tint.opacity(0.22 + pulse * 0.28), radius: 8 + pulse * 8)
            }
        } else {
            percentText
        }
    }

    private var percentText: some View {
        Text("\(soc)%")
            .font(.system(size: 48, weight: .bold, design: .rounded))
            .monospacedDigit()
            .foregroundStyle(tint)
            .contentTransition(.numericText())
    }
}

private struct BatteryLevelBar: View {
    let soc: Int
    let tint: Color
    let isCharging: Bool

    private var fillFraction: Double {
        Double(min(max(soc, 0), 100)) / 100.0
    }

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 7)
                    .fill(.quaternary)

                RoundedRectangle(cornerRadius: 7)
                    .fill(tint.gradient)
                    .frame(width: max(10, geo.size.width * fillFraction))
                    .overlay(alignment: .leading) {
                        if isCharging {
                            TimelineView(.animation) { timeline in
                                let phase = timeline.date.timeIntervalSinceReferenceDate
                                let fillWidth = max(10, geo.size.width * fillFraction)
                                let band: CGFloat = 140
                                let travel = fillWidth + band
                                let period = 2.2
                                let x = (phase.truncatingRemainder(dividingBy: period) / period) * travel - band
                                RoundedRectangle(cornerRadius: 7)
                                    .fill(
                                        LinearGradient(
                                            stops: [
                                                .init(color: .clear, location: 0.0),
                                                .init(color: .white.opacity(0.12), location: 0.3),
                                                .init(color: .white.opacity(0.5), location: 0.5),
                                                .init(color: .white.opacity(0.12), location: 0.7),
                                                .init(color: .clear, location: 1.0),
                                            ],
                                            startPoint: .leading,
                                            endPoint: .trailing
                                        )
                                    )
                                    .frame(width: band)
                                    .offset(x: x)
                            }
                        }
                    }
                    .clipShape(RoundedRectangle(cornerRadius: 7))
            }
        }
        .frame(height: 14)
        .animation(Theme.motion, value: soc)
        .animation(Theme.motion, value: isCharging)
    }
}

/// A compact metric with an icon, big value and caption. The accent tints the icon.
struct MetricTile: View {
    let title: String
    let value: String
    let systemImage: String
    var accent: Color = .secondary

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Image(systemName: systemImage)
                .font(.headline)
                .foregroundStyle(accent)
                .frame(width: 28, height: 28)
                .background(accent.opacity(0.15), in: RoundedRectangle(cornerRadius: 8))

            Text(value)
                .font(.title3.weight(.semibold).monospacedDigit())
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .contentTransition(.numericText())

            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(16)
        .background(Theme.cardBackground, in: RoundedRectangle(cornerRadius: 16))
        .animation(Theme.motion, value: value)
        .animation(Theme.motion, value: systemImage)
    }
}

/// Small rounded status pill.
struct StatusPill: View {
    let text: String
    let systemImage: String
    var tint: Color = .secondary

    var body: some View {
        Label(text, systemImage: systemImage)
            .font(.caption.weight(.semibold))
            .labelStyle(.titleAndIcon)
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(tint.opacity(0.15), in: Capsule())
            .foregroundStyle(tint)
            .contentTransition(.symbolEffect(.replace))
            .animation(Theme.motion, value: text)
            .animation(Theme.motion, value: systemImage)
    }
}

/// A simple horizontal bar showing a cell's voltage within its operating window.
struct CellBar: View {
    let mv: UInt16

    // Operating window for a Li-ion cell on this pack.
    private let minMv: Double = 3000
    private let maxMv: Double = 4200

    private var fraction: Double {
        let clamped = min(max(Double(mv), minMv), maxMv)
        return (clamped - minMv) / (maxMv - minMv)
    }

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                Capsule()
                    .fill(.quaternary)
                Capsule()
                    .fill(Theme.cellColor(mv).gradient)
                    .frame(width: max(6, geo.size.width * fraction))
            }
        }
        .frame(height: 10)
        .animation(Theme.motion, value: mv)
    }
}

/// The connection status banner shown at the top of the dashboard.
struct ConnectionBar: View {
    @EnvironmentObject private var ble: PowerbankBLEManager

    var body: some View {
        Group {
            if ble.connectionState.isConnected {
                connectedBar
                    .transition(.move(edge: .top).combined(with: .opacity))
            } else {
                disconnectedBar
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
        }
        .animation(Theme.motion, value: ble.connectionState)
    }

    private var connectedBar: some View {
        HStack(spacing: 6) {
            Spacer(minLength: 0)

            Circle()
                .fill(Color.green)
                .frame(width: 6, height: 6)
                .transition(.scale.combined(with: .opacity))

            SignalBars(strength: ble.signalStrength)

            Button(action: ble.toggleConnection) {
                Image(systemName: "xmark")
                    .font(.caption2.weight(.bold))
                    .frame(width: 18, height: 18)
            }
            .buttonStyle(.plain)
            .tint(.secondary)
            .accessibilityLabel("Disconnect")
        }
        .padding(.horizontal, 4)
    }

    private var disconnectedBar: some View {
        HStack(spacing: 14) {
            statusIcon

            VStack(alignment: .leading, spacing: 2) {
                Text(ble.connectionState.title)
                    .font(.subheadline.weight(.semibold))
                    .lineLimit(2)
                Text(ble.deviceInfo)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }

            Spacer(minLength: 8)

            Button(action: ble.toggleConnection) {
                Text(connectionButtonTitle)
                    .font(.caption.weight(.bold))
            }
            .buttonStyle(.bordered)
            .buttonBorderShape(.capsule)
            .tint(ble.connectionState.isConnected ? .red : .accentColor)
        }
        .padding(14)
        .background(Theme.cardBackground, in: RoundedRectangle(cornerRadius: 16))
    }

    private var connectionButtonTitle: String {
        if ble.connectionState.isConnected { return "Disconnect" }
        if ble.connectionState.isBusy { return "Cancel" }
        return "Connect"
    }

    @ViewBuilder
    private var statusIcon: some View {
        ZStack {
            Circle()
                .fill(statusColor.opacity(0.15))
                .frame(width: 40, height: 40)
            if ble.connectionState.isBusy {
                ProgressView()
                    .transition(.scale.combined(with: .opacity))
            } else {
                Image(systemName: ble.connectionState.isConnected ? "bolt.horizontal.circle.fill" : "antenna.radiowaves.left.and.right.slash")
                    .font(.title3)
                    .foregroundStyle(statusColor)
                    .contentTransition(.symbolEffect(.replace))
                    .transition(.scale.combined(with: .opacity))
            }
        }
        .animation(Theme.motion, value: ble.connectionState)
    }

    private var statusColor: Color {
        switch ble.connectionState {
        case .connected: .green
        case .scanning, .connecting: .blue
        case .bluetoothUnavailable: .red
        default: .secondary
        }
    }
}

/// Three-bar signal strength indicator.
struct SignalBars: View {
    let strength: SignalStrength

    var body: some View {
        HStack(alignment: .bottom, spacing: 2) {
            ForEach(1...3, id: \.self) { bar in
                RoundedRectangle(cornerRadius: 1)
                    .fill(bar <= strength.bars ? Color.green : Color.secondary.opacity(0.3))
                    .frame(width: 3, height: CGFloat(4 + bar * 3))
            }
        }
        .accessibilityLabel("Signal strength")
        .animation(Theme.motion, value: strength.bars)
    }
}
