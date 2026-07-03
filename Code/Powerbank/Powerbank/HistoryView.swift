import Charts
import Foundation
import SwiftUI

struct HistoryView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager
    @EnvironmentObject private var historyStore: HistoryStore
    @State private var range: HistoryRange = .day
    @State private var samples: [HistorySample] = []
    @State private var unknownTimeCount = 0
    @State private var reloadTask: Task<Void, Never>?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    rangePicker

                    if samples.isEmpty {
                        emptyState
                    } else {
                        chargeChart
                        powerChart
                        temperatureChart
                        cellsChart
                        sessionsCard
                    }

                    healthCard

                    if unknownTimeCount > 0 {
                        unknownTimeNote
                    }
                }
                .padding()
            }
            .navigationTitle("History")
            .background(Color(.systemGroupedBackground).ignoresSafeArea())
            .onAppear(perform: reloadHistory)
            .onChange(of: range) { _, _ in reloadHistory() }
            .onChange(of: historyStore.revision) { _, _ in scheduleReload() }
            .onDisappear {
                reloadTask?.cancel()
                reloadTask = nil
            }
        }
    }

    private func reloadHistory() {
        samples = historyStore.samples(since: Date().addingTimeInterval(-range.interval))
        unknownTimeCount = historyStore.unknownTimeSamples().count
    }

    private func scheduleReload() {
        reloadTask?.cancel()
        reloadTask = Task {
            try? await Task.sleep(nanoseconds: 400_000_000)
            guard !Task.isCancelled else { return }
            reloadHistory()
        }
    }

    private var rangePicker: some View {
        Picker("Range", selection: $range) {
            ForEach(HistoryRange.allCases) { range in
                Text(range.title).tag(range)
            }
        }
        .pickerStyle(.segmented)
        .accessibilityLabel("History range")
    }

    private var emptyState: some View {
        VStack(spacing: 14) {
            Image(systemName: "chart.xyaxis.line")
                .font(.system(size: 44))
                .foregroundStyle(.secondary)
            Text("No history yet")
                .font(.headline)
            Text("Measurements will appear here while the Powerbank runs.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 44)
    }

    private var chargeChart: some View {
        chartCard(title: "Charge", systemImage: "battery.75percent") {
            Chart(chartSamples) { sample in
                if let date = sample.date {
                    LineMark(
                        x: .value("Time", date),
                        y: .value("Charge", sample.socPercent)
                    )
                    .foregroundStyle(.green)
                    .interpolationMethod(.catmullRom)
                }
            }
            .chartYScale(domain: 0...100)
            .chartYAxisLabel("%")
        }
    }

    private var powerChart: some View {
        chartCard(title: "Power", systemImage: "bolt.fill") {
            Chart(chartSamples) { sample in
                if let date = sample.date {
                    LineMark(
                        x: .value("Time", date),
                        y: .value("Power", sample.powerW)
                    )
                    .foregroundStyle(Theme.stateColor(sample.state))
                    .interpolationMethod(.linear)
                }
            }
            .chartYAxisLabel("W")
        }
    }

    private var temperatureChart: some View {
        chartCard(title: "Temperature", systemImage: "thermometer.medium") {
            Chart(chartSamples) { sample in
                if let date = sample.date {
                    LineMark(
                        x: .value("Time", date),
                        y: .value("Temperature", sample.temperatureC)
                    )
                    .foregroundStyle(.orange)
                    .interpolationMethod(.catmullRom)
                }
            }
            .chartYAxisLabel("°C")
        }
    }

    private var cellsChart: some View {
        chartCard(title: "Cell Voltages", systemImage: "battery.100percent") {
            Chart(cellPoints) { point in
                LineMark(
                    x: .value("Time", point.date),
                    y: .value("Voltage", point.volts)
                )
                .foregroundStyle(by: .value("Cell", point.cell))
                .interpolationMethod(.catmullRom)
            }
            .chartYAxisLabel("V")
        }
    }

    private var sessionsCard: some View {
        SectionCard(title: "Recent Sessions", systemImage: "clock.arrow.circlepath") {
            let sessions = Array(powerSessions.suffix(8).reversed())
            if sessions.isEmpty {
                Text("No completed charging or discharging sessions in this range.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            } else {
                VStack(spacing: 0) {
                    ForEach(sessions) { session in
                        HStack(spacing: 12) {
                            Image(systemName: session.flow.systemImage)
                                .foregroundStyle(Theme.flowColor(session.flow))
                                .frame(width: 24)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(session.flow.title)
                                    .font(.subheadline.weight(.semibold))
                                Text(session.start.formatted(date: .abbreviated, time: .shortened))
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                            Spacer()
                            Text(session.duration)
                                .font(.subheadline.monospacedDigit())
                        }
                        .padding(.vertical, 8)
                        if session.id != sessions.last?.id {
                            Divider()
                        }
                    }
                }
            }
        }
    }

    @ViewBuilder
    private var healthCard: some View {
        SectionCard(title: "Battery Health", systemImage: "heart.text.square") {
            if let health = ble.batteryHealth {
                HStack(spacing: 12) {
                    healthMetric(
                        health.isLearned ? "\(health.healthPercent)%" : "Learning",
                        "Capacity"
                    )
                    Divider()
                    healthMetric(String(format: "%.1f", health.equivalentCycles), "Cycles")
                    Divider()
                    healthMetric(String(format: "%.1f Wh", health.totalEnergyWh), "Energy")
                }

                Divider()

                VStack(spacing: 9) {
                    healthRow("Usable capacity", "\(health.learnedCapacityMah) mAh")
                    healthRow("Maximum temperature", Format.temperature(health.maximumTemperatureC))
                    healthRow("Time above 45 °C", "\(health.hotMinutes) min")
                    healthRow("Average idle difference", "\(health.averageIdleDeltaMv) mV")
                    healthRow("Largest difference", "\(health.maximumIdleDeltaMv) mV")
                }

                if !health.isLearned {
                    Text("A trusted full-to-empty discharge is needed for a capacity estimate.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            } else {
                Text("Battery health requires firmware protocol v3.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var unknownTimeNote: some View {
        HStack(spacing: 10) {
            Image(systemName: "clock.badge.questionmark")
                .foregroundStyle(.secondary)
            Text("Time unavailable for \(unknownTimeCount) older samples.")
                .font(.caption)
                .foregroundStyle(.secondary)
            Spacer()
        }
        .padding(.horizontal, 4)
    }

    private var cellPoints: [CellHistoryPoint] {
        chartSamples.flatMap { sample -> [CellHistoryPoint] in
            guard let date = sample.date else { return [] }
            return [
                CellHistoryPoint(date: date, cell: "Cell 1", millivolts: sample.cell1Mv),
                CellHistoryPoint(date: date, cell: "Cell 2", millivolts: sample.cell2Mv),
                CellHistoryPoint(date: date, cell: "Cell 5", millivolts: sample.cell5Mv),
            ]
        }
    }

    private var chartSamples: [HistorySample] {
        let maximumPoints = 1_500
        guard samples.count > maximumPoints else { return samples }
        let step = max(1, samples.count / maximumPoints)
        var result = stride(from: 0, to: samples.count, by: step).map { samples[$0] }
        if let last = samples.last, result.last?.uniqueKey != last.uniqueKey {
            result.append(last)
        }
        return result
    }

    private var powerSessions: [PowerSession] {
        var result: [PowerSession] = []
        var activeFlow: PowerFlow?
        var start: Date?
        var previousDate: Date?

        for sample in samples {
            guard let date = sample.date else { continue }
            let flow: PowerFlow? = switch sample.state {
            case .charging: .charging
            case .discharging: .discharging
            default: nil
            }
            let gap = previousDate.map { date.timeIntervalSince($0) > 20 * 60 } ?? false
            if flow != activeFlow || gap {
                if let activeFlow, let start, let previousDate {
                    result.append(PowerSession(flow: activeFlow, start: start, end: previousDate))
                }
                activeFlow = flow
                start = flow == nil ? nil : date
            }
            previousDate = date
        }
        if let activeFlow, let start, let previousDate {
            result.append(PowerSession(flow: activeFlow, start: start, end: previousDate))
        }
        return result
    }

    private func chartCard<ChartContent: View>(
        title: String,
        systemImage: String,
        @ViewBuilder chart: () -> ChartContent
    ) -> some View {
        SectionCard(title: title, systemImage: systemImage) {
            chart()
                .frame(height: 180)
        }
    }

    private func healthMetric(_ value: String, _ title: String) -> some View {
        VStack(spacing: 4) {
            Text(value)
                .font(.headline.monospacedDigit())
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
    }

    private func healthRow(_ title: String, _ value: String) -> some View {
        HStack {
            Text(title)
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Text(value)
                .font(.subheadline.monospacedDigit().weight(.medium))
        }
    }
}

private enum HistoryRange: String, CaseIterable, Identifiable {
    case day
    case week
    case month

    var id: String { rawValue }
    var title: String {
        switch self {
        case .day: "24h"
        case .week: "7d"
        case .month: "30d"
        }
    }
    var interval: TimeInterval {
        switch self {
        case .day: 24 * 60 * 60
        case .week: 7 * 24 * 60 * 60
        case .month: 30 * 24 * 60 * 60
        }
    }
}

private struct CellHistoryPoint: Identifiable {
    let date: Date
    let cell: String
    let millivolts: Int
    var id: String { "\(cell)-\(date.timeIntervalSinceReferenceDate)" }
    var volts: Double { Double(millivolts) / 1000 }
}

private struct PowerSession: Identifiable {
    let flow: PowerFlow
    let start: Date
    let end: Date
    var id: String { "\(flow.title)-\(start.timeIntervalSinceReferenceDate)" }
    var duration: String {
        Format.duration(max(0, end.timeIntervalSince(start)) / 3600)
    }
}
