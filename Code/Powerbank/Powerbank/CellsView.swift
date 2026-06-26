import SwiftUI

struct CellsView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager

    var body: some View {
        NavigationStack {
            ScrollView {
                if let telemetry = ble.telemetry {
                    VStack(spacing: 16) {
                        balanceSummary(telemetry)
                        cellCards(telemetry)
                    }
                    .padding()
                } else {
                    EmptyTelemetryView()
                        .padding()
                }
            }
            .navigationTitle("Cells")
            .background(Color(.systemGroupedBackground).ignoresSafeArea())
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
        let isLowest = cell.mv == t.minCellMv && t.cellDeltaMv > 0
        let isHighest = cell.mv == t.maxCellMv && t.cellDeltaMv > 0
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
                if isLowest {
                    Text("LOW").font(.caption2.weight(.bold)).foregroundStyle(.orange)
                } else if isHighest {
                    Text("HIGH").font(.caption2.weight(.bold)).foregroundStyle(.mint)
                }
                Spacer()
                Text(Format.volts(cell.mv))
                    .font(.subheadline.monospacedDigit().weight(.semibold))
                    .foregroundStyle(Theme.cellColor(cell.mv))
            }
            CellBar(mv: cell.mv, isLowest: isLowest, isHighest: isHighest)
        }
    }

    private func summaryItem(_ title: String, _ value: String, _ color: Color) -> some View {
        VStack(spacing: 4) {
            Text(value)
                .font(.headline.monospacedDigit())
                .foregroundStyle(color)
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
    }

    private func deltaColor(_ delta: UInt16) -> Color {
        switch delta {
        case ..<30: .green
        case ..<80: .orange
        default: .red
        }
    }
}
