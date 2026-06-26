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

/// The hero element on the dashboard: a circular state-of-charge gauge with a
/// gradient track. When charging it erupts into a full energy-flow animation.
struct BatteryRingView: View {
    let soc: Int
    let state: PackState
    let flow: PowerFlow

    private var tint: Color { Theme.socColor(soc) }
    private let lineWidth: CGFloat = 16
    private let size: CGFloat = 220

    private var isCharging: Bool { flow == .charging }

    var body: some View {
        ZStack {
            // Track
            Circle()
                .stroke(tint.opacity(0.15), style: StrokeStyle(lineWidth: lineWidth, lineCap: .round))

            // The cinematic charging layer sits between the track and the
            // state-of-charge arc so the actual level stays readable on top.
            if isCharging {
                ChargingRingEffect(ringWidth: lineWidth)
            }

            // State-of-charge arc
            Circle()
                .trim(from: 0, to: max(0.001, Double(soc) / 100))
                .stroke(
                    AngularGradient(
                        colors: [tint.opacity(0.7), tint],
                        center: .center,
                        startAngle: .degrees(-90),
                        endAngle: .degrees(270)
                    ),
                    style: StrokeStyle(lineWidth: lineWidth, lineCap: .round)
                )
                .rotationEffect(.degrees(-90))
                .shadow(color: isCharging ? tint.opacity(0.8) : .clear, radius: 6)
                .animation(.easeInOut(duration: 0.6), value: soc)

            centerLabel
        }
        .frame(width: size, height: size)
        .padding(.vertical, 4)
    }

    private var centerLabel: some View {
        VStack(spacing: 2) {
            ZStack {
                if isCharging {
                    // Soft bloom behind the bolt.
                    Circle()
                        .fill(tint.opacity(0.35))
                        .frame(width: 30, height: 30)
                        .blur(radius: 10)
                }
                Image(systemName: state.systemImage)
                    .font(.title2)
                    .foregroundStyle(Theme.stateColor(state))
                    .symbolEffect(.pulse, options: .repeating, isActive: isCharging)
            }

            HStack(alignment: .firstTextBaseline, spacing: 1) {
                Text("\(soc)")
                    .font(.system(size: 64, weight: .bold, design: .rounded))
                    .monospacedDigit()
                    .contentTransition(.numericText())
                Text("%")
                    .font(.title2.weight(.semibold))
                    .foregroundStyle(.secondary)
            }
            .animation(.snappy, value: soc)

            Text(state.title)
                .font(.subheadline.weight(.medium))
                .foregroundStyle(.secondary)
        }
    }
}

/// An over-the-top charging visualisation drawn entirely in a `Canvas` and
/// driven by a continuous `TimelineView` clock. Layers, from back to front:
///   1. A breathing glow halo around the ring.
///   2. Energy particles ("electrons") rising up through the core.
///   3. Two counter-rotating comets racing around the ring with glowing trails.
struct ChargingRingEffect: View {
    var ringWidth: CGFloat
    var tint: Color = .green

    var body: some View {
        TimelineView(.animation) { timeline in
            let t = timeline.date.timeIntervalSinceReferenceDate
            Canvas { context, size in
                let center = CGPoint(x: size.width / 2, y: size.height / 2)
                let radius = (min(size.width, size.height) - ringWidth) / 2

                drawGlowHalo(context, center: center, radius: radius, t: t)
                drawRisingParticles(context, center: center, radius: radius, t: t)
                // Two main comets racing in opposite directions, plus a fast
                // bright spark that laps them.
                drawComet(context, center: center, radius: radius,
                          angle: t * 2.4, trailDirection: -1,
                          head: .white, trail: .green, headScale: 1.1)
                drawComet(context, center: center, radius: radius,
                          angle: -t * 1.6 + .pi, trailDirection: 1,
                          head: .mint, trail: .mint, headScale: 1.0)
                drawComet(context, center: center, radius: radius,
                          angle: t * 4.1 + 1.2, trailDirection: -1,
                          head: .white, trail: .cyan, headScale: 0.6)
            }
        }
        .allowsHitTesting(false)
    }

    // MARK: - Layers

    private func drawGlowHalo(_ context: GraphicsContext, center: CGPoint, radius: CGFloat, t: TimeInterval) {
        let pulse = 0.5 + 0.5 * sin(t * 2.2)
        var glow = context
        glow.addFilter(.blur(radius: 12))
        let rect = CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2)
        glow.stroke(
            Path(ellipseIn: rect),
            with: .color(tint.opacity(0.18 + 0.22 * pulse)),
            lineWidth: ringWidth * (1.1 + 0.6 * pulse)
        )
    }

    private func drawRisingParticles(_ context: GraphicsContext, center: CGPoint, radius: CGFloat, t: TimeInterval) {
        let count = 22
        for i in 0..<count {
            let seed = Double(i) * 0.6180339887
            let speed = 0.4 + (seed.truncatingRemainder(dividingBy: 0.4))
            let progress = (t * speed + seed).truncatingRemainder(dividingBy: 1.0)

            // Drift sideways on a sine path while rising from bottom to top.
            let sway = sin((progress + seed) * .pi * 2) * 0.55
            let x = center.x + sway * radius * 0.72
            let y = center.y + radius * 0.85 - progress * radius * 1.7

            let fade = sin(progress * .pi) // 0 at the ends, 1 in the middle
            let r = 1.5 + 3.2 * fade

            // Soft glow + a brighter hot core gives each spark a comet-like streak.
            var glow = context
            glow.addFilter(.blur(radius: 2.5))
            glow.fill(
                Path(ellipseIn: CGRect(x: x - r * 1.6, y: y - r * 1.6, width: r * 3.2, height: r * 3.2)),
                with: .color(.green.opacity(0.5 * fade))
            )
            var core = context
            core.addFilter(.blur(radius: 1))
            core.fill(
                Path(ellipseIn: CGRect(x: x - r * 0.6, y: y - r * 0.6, width: r * 1.2, height: r * 1.2)),
                with: .color(.mint.opacity(0.95 * fade))
            )
        }
    }

    private func drawComet(_ context: GraphicsContext, center: CGPoint, radius: CGFloat,
                           angle: Double, trailDirection: Double, head: Color, trail: Color,
                           headScale: CGFloat) {
        let trailCount = 26
        for j in stride(from: trailCount - 1, through: 0, by: -1) {
            let a = angle + trailDirection * Double(j) * 0.05
            let px = center.x + cos(a) * radius
            let py = center.y + sin(a) * radius
            let frac = 1.0 - Double(j) / Double(trailCount)
            let taper = pow(frac, 1.3)
            let r = (ringWidth / 2) * (0.2 + 1.0 * taper) * (j == 0 ? headScale : 1)

            var dot = context
            dot.addFilter(.blur(radius: j == 0 ? 2.5 : 4.5))
            let color = (j == 0 ? head : trail).opacity(taper)
            dot.fill(
                Path(ellipseIn: CGRect(x: px - r, y: py - r, width: r * 2, height: r * 2)),
                with: .color(color)
            )
        }
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

            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(16)
        .background(Theme.cardBackground, in: RoundedRectangle(cornerRadius: 16))
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
    }
}

/// A simple horizontal bar showing a cell's voltage within its operating window.
struct CellBar: View {
    let mv: UInt16
    var isLowest: Bool = false
    var isHighest: Bool = false

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
    }
}

/// The connection status banner shown at the top of the dashboard.
struct ConnectionBar: View {
    @EnvironmentObject private var ble: PowerbankBLEManager

    var body: some View {
        HStack(spacing: 14) {
            statusIcon

            VStack(alignment: .leading, spacing: 2) {
                Text(ble.connectionState.title)
                    .font(.subheadline.weight(.semibold))
                    .lineLimit(1)
                Text(ble.deviceInfo)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }

            Spacer(minLength: 8)

            if ble.connectionState.isConnected {
                SignalBars(strength: ble.signalStrength)
            }

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
            } else {
                Image(systemName: ble.connectionState.isConnected ? "bolt.horizontal.circle.fill" : "antenna.radiowaves.left.and.right.slash")
                    .font(.title3)
                    .foregroundStyle(statusColor)
            }
        }
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
    }
}
