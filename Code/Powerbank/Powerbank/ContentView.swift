import SwiftData
import SwiftUI

struct ContentView: View {
    var body: some View {
        TabView {
            DashboardView()
                .tabItem { Label("Dashboard", systemImage: "bolt.fill") }
            CellsView()
                .tabItem { Label("Cells", systemImage: "battery.100percent") }
            HistoryView()
                .tabItem { Label("History", systemImage: "chart.xyaxis.line") }
            DiagnosticsView()
                .tabItem { Label("Diagnostics", systemImage: "waveform.path.ecg") }
            ControlsView()
                .tabItem { Label("Controls", systemImage: "slider.horizontal.3") }
        }
    }
}

struct DashboardView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if let telemetry = ble.telemetry {
                        ConnectionBar()
                            .transition(.move(edge: .top).combined(with: .opacity))

                        BatteryStatusView(
                            soc: Int(telemetry.socPercent),
                            state: telemetry.state,
                            flow: telemetry.flow,
                            currentMa: telemetry.currentMa
                        )

                        CellOverviewCard(telemetry: telemetry)

                        if ble.isTelemetryStale || !telemetry.trusted || telemetry.hasFaults || telemetry.lowCellWarning {
                            WarningStrip(telemetry: telemetry, isStale: ble.isTelemetryStale)
                                .transition(.move(edge: .top).combined(with: .opacity))
                        }

                        metrics(telemetry)

                        outputControls(telemetry)
                    } else {
                        EmptyTelemetryView()
                            .transition(.opacity.combined(with: .scale(scale: 0.98)))
                    }
                }
                .padding()
                .animation(Theme.motion, value: ble.telemetry != nil)
                .animation(Theme.motion, value: ble.telemetry?.state)
                .animation(Theme.motion, value: ble.telemetry?.flags)
                .animation(Theme.motion, value: ble.telemetry?.faults)
            }
            .navigationTitle("Powerbank")
            .background(backgroundGradient)
        }
    }

    private func metrics(_ t: Telemetry) -> some View {
        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
            MetricTile(title: "Pack Voltage", value: Format.volts(t.packVoltage), systemImage: "bolt.fill", accent: .yellow)
            MetricTile(title: "Temperature", value: Format.temperature(t.temperatureC), systemImage: "thermometer.medium", accent: .orange)
            MetricTile(title: "Output", value: t.dischargeEnabled ? "On" : "Off", systemImage: t.dischargeEnabled ? "powerplug.fill" : "powerplug", accent: t.dischargeEnabled ? .green : .secondary)
            MetricTile(title: "Power", value: Format.power(t.powerW), systemImage: "gauge.with.dots.needle.67percent", accent: Theme.flowColor(t.flow))
        }
    }

    private func outputControls(_ t: Telemetry) -> some View {
        VStack(spacing: 10) {
            if let remaining = t.idleRemainingSec {
                HStack {
                    StatusPill(
                        text: "Output turns off in \(idleCountdown(remaining))",
                        systemImage: "timer",
                        tint: remaining <= 60 ? .orange : .secondary
                    )
                    Spacer()
                }
                .transition(.move(edge: .top).combined(with: .opacity))
            } else if t.idleOutputOff {
                HStack {
                    StatusPill(
                        text: "Output turned off after 15 minutes idle",
                        systemImage: "powerplug.portrait",
                        tint: .secondary
                    )
                    Spacer()
                }
                .transition(.move(edge: .top).combined(with: .opacity))
            }

            HStack(spacing: 12) {
                Button {
                    ble.send(.outputOn)
                } label: {
                    Label("Output On", systemImage: "power")
                        .fontWeight(.semibold)
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.green)
                .disabled(!ble.canSendCommands || t.dischargeEnabled)
                .opacity((!ble.canSendCommands || t.dischargeEnabled) ? 0.55 : 1)

                Button {
                    ble.send(.outputOff)
                } label: {
                    Label("Output Off", systemImage: "poweroff")
                        .fontWeight(.semibold)
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .tint(.red)
                .disabled(!ble.canSendCommands || !t.dischargeEnabled)
                .opacity((!ble.canSendCommands || !t.dischargeEnabled) ? 0.55 : 1)
            }
            .controlSize(.large)
            .buttonBorderShape(.roundedRectangle(radius: 14))
        }
        .animation(Theme.motion, value: ble.canSendCommands)
        .animation(Theme.motion, value: t.dischargeEnabled)
        .animation(Theme.motion, value: t.idleRemainingSec)
        .animation(Theme.motion, value: t.idleOutputOff)
    }

    private func idleCountdown(_ seconds: UInt16) -> String {
        let minutes = seconds / 60
        let remainder = seconds % 60
        return String(format: "%d:%02d", minutes, remainder)
    }

    private var backgroundGradient: some View {
        LinearGradient(
            colors: [Color(.systemGroupedBackground), Color(.systemGroupedBackground).opacity(0.4)],
            startPoint: .top,
            endPoint: .bottom
        )
        .ignoresSafeArea()
    }
}

/// A compact dashboard summary of the three physical cells.
private struct CellOverviewCard: View {
    let telemetry: Telemetry

    var body: some View {
        SectionCard(title: nil, systemImage: nil) {
            HStack(spacing: 10) {
                ForEach(Array(telemetry.cells.enumerated()), id: \.element.id) { index, cell in
                    cellTile(cell, physicalIndex: index + 1)
                }
            }
        }
    }

    private func cellTile(_ cell: Telemetry.Cell, physicalIndex: Int) -> some View {
        let tint = Theme.cellColor(cell.mv)

        return VStack(alignment: .leading, spacing: 7) {
            Text("Cell \(physicalIndex)")
                .font(.caption.weight(.semibold))
                .foregroundStyle(.secondary)
                .lineLimit(1)

            CellChargeBar(mv: cell.mv, tint: tint)

            Text(Format.volts(cell.mv))
                .font(.subheadline.monospacedDigit().weight(.semibold))
                .foregroundStyle(tint)
                .lineLimit(1)
                .minimumScaleFactor(0.75)
                .contentTransition(.numericText())
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .animation(Theme.motion, value: cell.mv)
    }
}

private struct CellChargeBar: View {
    let mv: UInt16
    let tint: Color

    private var fillFraction: Double {
        let minMv = 3100.0
        let maxMv = 4150.0
        let clamped = min(max(Double(mv), minMv), maxMv)
        return (clamped - minMv) / (maxMv - minMv)
    }

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                Capsule()
                    .fill(.quaternary)
                Capsule()
                    .fill(tint)
                    .frame(width: max(5, geo.size.width * fillFraction))
            }
        }
        .frame(height: 6)
        .accessibilityHidden(true)
        .animation(Theme.motion, value: mv)
    }
}

private struct WarningStrip: View {
    let telemetry: Telemetry
    let isStale: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if isStale {
                warning("Telemetry is stale", "Readings haven't updated recently.", "clock.badge.exclamationmark", .orange)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
            if !telemetry.trusted {
                warning("Measurements untrusted", "The firmware can't vouch for these readings.", "exclamationmark.triangle.fill", .orange)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
            ForEach(telemetry.decodedFaults) { fault in
                warning(fault.title, fault.detail, fault.systemImage, .red)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
            if telemetry.lowCellWarning && !telemetry.hasFaults {
                warning("Low cell warning", "A cell is approaching the cutoff.", "battery.25percent", .orange)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(16)
        .background(Theme.cardBackground, in: RoundedRectangle(cornerRadius: Theme.cardCornerRadius))
        .animation(Theme.motion, value: isStale)
        .animation(Theme.motion, value: telemetry.trusted)
        .animation(Theme.motion, value: telemetry.faults)
        .animation(Theme.motion, value: telemetry.lowCellWarning)
    }

    private func warning(_ title: String, _ detail: String, _ image: String, _ tint: Color) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: image)
                .foregroundStyle(tint)
                .font(.headline)
                .frame(width: 24)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.subheadline.weight(.semibold))
                Text(detail).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
        }
        .contentTransition(.opacity)
    }
}

struct EmptyTelemetryView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager

    var body: some View {
        VStack(spacing: 18) {
            Image(systemName: isWaiting ? "antenna.radiowaves.left.and.right" : "bolt.horizontal.circle")
                .font(.system(size: 56))
                .foregroundStyle(.secondary)
                .symbolEffect(.variableColor.iterative, options: .repeating, isActive: isWaiting)

            VStack(spacing: 6) {
                Text(title)
                    .font(.title3.weight(.semibold))
                Text(detail)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }

            if !isWaiting {
                Button {
                    ble.startScanning()
                } label: {
                    Label("Connect", systemImage: "antenna.radiowaves.left.and.right")
                        .fontWeight(.semibold)
                        .padding(.horizontal, 8)
                }
                .buttonStyle(.borderedProminent)
                .buttonBorderShape(.capsule)
                .controlSize(.large)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 60)
    }

    private var isWaiting: Bool {
        ble.connectionState.isBusy || ble.connectionState.isConnected
    }

    private var title: String {
        if ble.connectionState.isConnected {
            return "Powerbank connected"
        }
        if ble.connectionState.isBusy {
            return "Looking for your Powerbank"
        }
        return "Not connected"
    }

    private var detail: String {
        if ble.connectionState.isConnected {
            return "Loading live data…"
        }
        if ble.connectionState.isBusy {
            return "Make sure the Powerbank is powered and nearby."
        }
        return "Connect to start seeing live telemetry."
    }
}

#Preview {
    let historyStore = HistoryStore(inMemory: true)
    let alertManager = PowerbankAlertManager()
    ContentView()
        .environmentObject(PowerbankBLEManager(
            historyStore: historyStore,
            alertManager: alertManager
        ))
        .environmentObject(historyStore)
        .environmentObject(alertManager)
        .modelContainer(historyStore.container)
}
