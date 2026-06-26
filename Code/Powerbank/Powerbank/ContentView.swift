import SwiftUI

struct ContentView: View {
    var body: some View {
        TabView {
            DashboardView()
                .tabItem { Label("Dashboard", systemImage: "bolt.fill") }
            CellsView()
                .tabItem { Label("Cells", systemImage: "battery.100percent") }
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
                    ConnectionBar()

                    if let telemetry = ble.telemetry {
                        BatteryRingView(soc: Int(telemetry.socPercent), state: telemetry.state, flow: telemetry.flow)

                        FlowCard(telemetry: telemetry)

                        if ble.isTelemetryStale || !telemetry.trusted || telemetry.hasFaults || telemetry.lowCellWarning {
                            WarningStrip(telemetry: telemetry, isStale: ble.isTelemetryStale)
                        }

                        metrics(telemetry)

                        outputControls(telemetry)
                    } else {
                        EmptyTelemetryView()
                    }
                }
                .padding()
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
            MetricTile(title: "Charging Path", value: t.chargeEnabled ? "On" : "Off", systemImage: t.chargeEnabled ? "bolt.batteryblock.fill" : "bolt.batteryblock", accent: t.chargeEnabled ? .green : .secondary)
        }
    }

    private func outputControls(_ t: Telemetry) -> some View {
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
        }
        .controlSize(.large)
        .buttonBorderShape(.roundedRectangle(radius: 14))
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

/// Prominent card showing whether the pack is charging or discharging, with power.
private struct FlowCard: View {
    let telemetry: Telemetry

    private var color: Color { Theme.flowColor(telemetry.flow) }

    var body: some View {
        HStack(spacing: 16) {
            Image(systemName: telemetry.flow.systemImage)
                .font(.title)
                .foregroundStyle(color)
                .frame(width: 48, height: 48)
                .background(color.opacity(0.15), in: Circle())
                .symbolEffect(.bounce, options: .repeating, isActive: telemetry.flow != .idle)

            VStack(alignment: .leading, spacing: 2) {
                Text(telemetry.flow.title)
                    .font(.headline)
                Text(Format.current(telemetry.currentMa))
                    .font(.subheadline.monospacedDigit())
                    .foregroundStyle(.secondary)
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 2) {
                Text(Format.power(telemetry.powerW))
                    .font(.title2.weight(.bold).monospacedDigit())
                    .foregroundStyle(color)
                Text("Power")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(18)
        .background(Theme.cardBackground, in: RoundedRectangle(cornerRadius: Theme.cardCornerRadius))
    }
}

private struct WarningStrip: View {
    let telemetry: Telemetry
    let isStale: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if isStale {
                warning("Telemetry is stale", "Readings haven't updated recently.", "clock.badge.exclamationmark", .orange)
            }
            if !telemetry.trusted {
                warning("Measurements untrusted", "The firmware can't vouch for these readings.", "exclamationmark.triangle.fill", .orange)
            }
            ForEach(telemetry.decodedFaults) { fault in
                warning(fault.title, fault.detail, fault.systemImage, .red)
            }
            if telemetry.lowCellWarning && !telemetry.hasFaults {
                warning("Low cell warning", "A cell is approaching the cutoff.", "battery.25percent", .orange)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(16)
        .background(Theme.cardBackground, in: RoundedRectangle(cornerRadius: Theme.cardCornerRadius))
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
    }
}

struct EmptyTelemetryView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager

    var body: some View {
        VStack(spacing: 18) {
            Image(systemName: ble.connectionState.isBusy ? "antenna.radiowaves.left.and.right" : "bolt.horizontal.circle")
                .font(.system(size: 56))
                .foregroundStyle(.secondary)
                .symbolEffect(.variableColor.iterative, options: .repeating, isActive: ble.connectionState.isBusy)

            VStack(spacing: 6) {
                Text(ble.connectionState.isBusy ? "Looking for your Powerbank" : "Not connected")
                    .font(.title3.weight(.semibold))
                Text(ble.connectionState.isBusy
                     ? "Make sure the Powerbank is powered and nearby."
                     : "Connect to start seeing live telemetry.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }

            if !ble.connectionState.isBusy {
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
}

#Preview {
    ContentView()
        .environmentObject(PowerbankBLEManager())
}
