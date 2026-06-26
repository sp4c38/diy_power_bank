import SwiftUI

struct DiagnosticsView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if let t = ble.telemetry {
                        statusCard(t)
                            .transition(.opacity.combined(with: .scale(scale: 0.98)))
                        faultsCard(t)
                            .transition(.opacity.combined(with: .scale(scale: 0.98)))
                        flagsCard(t)
                            .transition(.opacity.combined(with: .scale(scale: 0.98)))
                    }
                    commandResultCard
                    eventLogCard
                }
                .padding()
                .animation(Theme.motion, value: ble.telemetry?.state)
                .animation(Theme.motion, value: ble.telemetry?.faults)
                .animation(Theme.motion, value: ble.telemetry?.flags)
                .animation(Theme.motion, value: ble.commandResult)
                .animation(Theme.motion, value: ble.events.count)
            }
            .navigationTitle("Diagnostics")
            .background(Color(.systemGroupedBackground).ignoresSafeArea())
        }
    }

    private func statusCard(_ t: Telemetry) -> some View {
        SectionCard(title: "Status", systemImage: "info.circle") {
            VStack(spacing: 0) {
                row("State", t.state.title, color: Theme.stateColor(t.state))
                Divider()
                row("Temperature", Format.temperature(t.temperatureC))
                Divider()
                row("Uptime", Format.uptime(t.uptimeSec))
                Divider()
                row("Signal", signalText)
                Divider()
                row("Last update", lastUpdateText)
                Divider()
                row("Protocol", "v\(t.protocolVersion)")
            }
            Text(t.state.detail)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private func faultsCard(_ t: Telemetry) -> some View {
        SectionCard(title: "Faults", systemImage: "exclamationmark.octagon") {
            if t.decodedFaults.isEmpty {
                Label("No active faults", systemImage: "checkmark.circle.fill")
                    .font(.subheadline)
                    .foregroundStyle(.green)
                    .transition(.opacity.combined(with: .scale(scale: 0.98)))
            } else {
                VStack(spacing: 12) {
                    ForEach(t.decodedFaults) { fault in
                        HStack(alignment: .top, spacing: 12) {
                            Image(systemName: fault.systemImage)
                                .foregroundStyle(.red)
                                .frame(width: 24)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(fault.title).font(.subheadline.weight(.semibold))
                                Text(fault.detail).font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer()
                        }
                        .transition(.move(edge: .top).combined(with: .opacity))
                    }
                }
                Text("Raw: \(t.faults.hexString)")
                    .font(.caption2.monospaced())
                    .foregroundStyle(.tertiary)
            }
        }
    }

    private func flagsCard(_ t: Telemetry) -> some View {
        SectionCard(title: "Flags", systemImage: "flag") {
            LazyVGrid(columns: [GridItem(.flexible(), alignment: .leading), GridItem(.flexible(), alignment: .leading)], spacing: 10) {
                ForEach(t.decodedFlags) { flag in
                    HStack(spacing: 8) {
                        Image(systemName: flag.isOn ? "checkmark.circle.fill" : "circle")
                            .foregroundStyle(flag.isOn ? .green : .secondary)
                            .font(.caption)
                            .contentTransition(.symbolEffect(.replace))
                        Text(flag.title)
                            .font(.caption)
                            .foregroundStyle(flag.isOn ? .primary : .secondary)
                        Spacer(minLength: 0)
                    }
                    .animation(Theme.motion, value: flag.isOn)
                }
            }
            Text("Raw: \(t.flags.hexString)")
                .font(.caption2.monospaced())
                .foregroundStyle(.tertiary)
        }
    }

    private var commandResultCard: some View {
        SectionCard(title: "Last Command Result", systemImage: "terminal") {
            HStack(spacing: 10) {
                if let cmd = ble.lastCommand {
                    Image(systemName: cmd.systemImage)
                        .foregroundStyle(.secondary)
                        .contentTransition(.symbolEffect(.replace))
                        .transition(.scale.combined(with: .opacity))
                }
                Text(ble.commandResult)
                    .font(.subheadline.monospaced())
                    .contentTransition(.opacity)
                Spacer()
            }
        }
    }

    private var eventLogCard: some View {
        SectionCard(title: "Event Log", systemImage: "list.bullet.rectangle") {
            if ble.events.isEmpty {
                Text("No events yet")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .transition(.opacity)
            } else {
                VStack(spacing: 0) {
                    ForEach(ble.events.prefix(40)) { item in
                        HStack(alignment: .top, spacing: 10) {
                            Circle()
                                .fill(item.isError ? Color.red : Color.secondary.opacity(0.4))
                                .frame(width: 6, height: 6)
                                .padding(.top, 6)
                            Text(item.text)
                                .font(.caption)
                                .foregroundStyle(item.isError ? .red : .primary)
                            Spacer()
                            Text(item.date, style: .time)
                                .font(.caption2.monospacedDigit())
                                .foregroundStyle(.tertiary)
                        }
                        .padding(.vertical, 5)
                        .transition(.move(edge: .top).combined(with: .opacity))
                        if item.id != ble.events.prefix(40).last?.id {
                            Divider()
                        }
                    }
                }

                Button(role: .destructive) {
                    ble.clearEvents()
                } label: {
                    Label("Clear log", systemImage: "trash")
                        .font(.caption)
                }
                .padding(.top, 4)
            }
        }
    }

    private func row(_ title: String, _ value: String, color: Color = .primary) -> some View {
        HStack {
            Text(title).font(.subheadline).foregroundStyle(.secondary)
            Spacer()
            Text(value)
                .font(.subheadline.monospacedDigit().weight(.medium))
                .foregroundStyle(color)
                .contentTransition(.numericText())
        }
        .padding(.vertical, 8)
        .animation(Theme.motion, value: value)
    }

    private var signalText: String {
        guard ble.connectionState.isConnected, let rssi = ble.rssi else { return "—" }
        return "\(rssi) dBm"
    }

    private var lastUpdateText: String {
        guard let age = ble.lastTelemetryAge else { return "—" }
        if age < 2 { return "just now" }
        return "\(Int(age))s ago"
    }
}
