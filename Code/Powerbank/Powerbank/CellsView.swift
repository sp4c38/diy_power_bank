import SwiftUI

struct CellsView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager

    var body: some View {
        NavigationStack {
            ScrollView {
                if let telemetry = ble.telemetry {
                    VStack(spacing: 16) {
                        stateOfCharge(telemetry)
                        balanceSummary(telemetry)
                        cellCards(telemetry)
                    }
                    .padding()
                    .transition(.opacity.combined(with: .scale(scale: 0.98)))
                    .animation(Theme.motion, value: telemetry.cellDeltaMv)
                    .animation(Theme.motion, value: telemetry.balanceMask)
                    .animation(Theme.motion, value: telemetry.flags)
                    .animation(Theme.motion, value: telemetry.socPercent)
                    .animation(Theme.motion, value: telemetry.currentMa)
                } else {
                    EmptyTelemetryView()
                        .padding()
                        .transition(.opacity.combined(with: .scale(scale: 0.98)))
                }
            }
            .navigationTitle("Cells")
            .background(Color(.systemGroupedBackground).ignoresSafeArea())
        }
    }

    private func stateOfCharge(_ t: Telemetry) -> some View {
        SectionCard(title: "State of Charge", systemImage: "battery.75percent") {
            // What the gauge is built from: remaining charge/energy and the
            // current that is being integrated.
            HStack(spacing: 12) {
                summaryItem("Charge", String(format: "%.1f mAh", t.chargeRemainingMah), .primary)
                Divider()
                summaryItem("Energy", String(format: "≈ %.1f Wh", t.energyRemainingWh), .secondary)
                Divider()
                summaryItem("Rate", Format.current(t.currentMa), Theme.flowColor(t.flow))
            }

            if t.chargeComplete, t.flow != .discharging {
                StatusPill(
                    text: "Fully charged",
                    systemImage: "checkmark.circle.fill",
                    tint: .green
                )
            } else if let estimate = ble.runtimeEstimate {
                switch estimate {
                case .toFull(let hours):
                    StatusPill(
                        text: "~\(Format.eta(hours)) to full",
                        systemImage: "clock.badge.checkmark",
                        tint: Theme.flowColor(.charging)
                    )
                case .finishingCharge(let minutes):
                    StatusPill(
                        text: "Finishing charge · ~\(minutes) min",
                        systemImage: "battery.100percent.bolt",
                        tint: Theme.flowColor(.charging)
                    )
                case .toEmpty(let hours):
                    StatusPill(
                        text: "~\(Format.eta(hours)) to empty",
                        systemImage: "hourglass",
                        tint: Theme.flowColor(.discharging)
                    )
                }
            }

            Divider()

            // How the number is derived: coulomb gauge vs the resting-voltage
            // estimate it is anchored to.
            HStack(spacing: 12) {
                summaryItem("Gauge (coulomb)", "\(t.socPercent)%", Theme.socColor(Int(t.socPercent)))
                Divider()
                summaryItem("Voltage estimate", "\(t.voltageEstimatePercent)%", .secondary)
            }

            Text(socMethodCaption(t))
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private func socMethodCaption(_ t: Telemetry) -> String {
        switch t.flow {
        case .charging:
            return "Charging: the gauge counts charge flowing in. Voltage rises ahead of true charge, so the two differ until it rests."
        case .discharging:
            return "Under load the cell voltage sags, so the gauge tracks coulombs (current × time) rather than voltage."
        case .idle:
            return "At rest the coulomb gauge and the voltage estimate should closely agree."
        }
    }

    private func balanceSummary(_ t: Telemetry) -> some View {
        SectionCard(title: "Pack Balance", systemImage: "scalemass") {
            HStack(spacing: 12) {
                summaryItem("Delta", "\(t.cellDeltaMv) mV", deltaColor(t.cellDeltaMv))
                Divider()
                summaryItem("Lowest", Format.volts(t.minCellMv), .secondary)
                Divider()
                summaryItem("Highest", Format.volts(t.maxCellMv), .secondary)
            }

            HStack {
                StatusPill(
                    text: t.balancing ? "Balancing active" : "Balancing idle",
                    systemImage: t.balancing ? "scalemass.fill" : "scalemass",
                    tint: t.balancing ? .green : .secondary
                )
                StatusPill(
                    text: t.trusted ? "Trusted" : "Untrusted",
                    systemImage: t.trusted ? "checkmark.seal.fill" : "exclamationmark.triangle.fill",
                    tint: t.trusted ? .green : .orange
                )
                Spacer()
            }
        }
    }

    private func cellCards(_ t: Telemetry) -> some View {
        SectionCard(title: "Cell Voltages", systemImage: "battery.100percent") {
            VStack(spacing: 18) {
                ForEach(t.cells) { cell in
                    cellRow(cell, telemetry: t)
                }
            }
        }
    }

    private func cellRow(_ cell: Telemetry.Cell, telemetry t: Telemetry) -> some View {
        let balancing = t.isBalancing(cellID: cell.id)
        return VStack(spacing: 6) {
            HStack {
                Text(cell.label)
                    .font(.subheadline.weight(.medium))
                if balancing {
                    Image(systemName: "drop.fill")
                        .font(.caption2)
                        .foregroundStyle(.blue)
                        .help("Bleeding to balance")
                }
                Spacer()
                Text(Format.volts(cell.mv))
                    .font(.subheadline.monospacedDigit().weight(.semibold))
                    .foregroundStyle(Theme.cellColor(cell.mv))
                    .contentTransition(.numericText())
            }
            CellBar(mv: cell.mv)
        }
        .animation(Theme.motion, value: cell.mv)
        .animation(Theme.motion, value: balancing)
    }

    private func summaryItem(_ title: String, _ value: String, _ color: Color) -> some View {
        VStack(spacing: 4) {
            Text(value)
                .font(.headline.monospacedDigit())
                .foregroundStyle(color)
                .contentTransition(.numericText())
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
        .animation(Theme.motion, value: value)
    }

    private func deltaColor(_ delta: UInt16) -> Color {
        switch delta {
        case ..<30: .green
        case ..<80: .orange
        default: .red
        }
    }
}
