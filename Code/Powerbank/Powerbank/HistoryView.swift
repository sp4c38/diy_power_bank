import Charts
import Foundation
import SwiftUI

struct HistoryView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager
    @EnvironmentObject private var historyStore: HistoryStore
    @State private var socRange: SocRange = .day
    @State private var samples: [HistorySample] = []
    @State private var unknownTimeCount = 0
    @State private var reloadTask: Task<Void, Never>?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if samples.isEmpty {
                        emptyState
                    } else {
                        socCard
                        sessionsCard
                        recentCellsCard
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
            .onChange(of: historyStore.revision) { _, _ in scheduleReload() }
            .onDisappear {
                reloadTask?.cancel()
                reloadTask = nil
            }
        }
    }

    private func reloadHistory() {
        // Load the full retained week once; the individual cards narrow it down.
        samples = historyStore.samples(since: Date().addingTimeInterval(-7 * 24 * 60 * 60))
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

    // MARK: - Charge (SoC) over time

    private var socCard: some View {
        SectionCard(title: "Charge", systemImage: "battery.75percent") {
            Picker("Range", selection: $socRange) {
                ForEach(SocRange.allCases) { range in
                    Text(range.title).tag(range)
                }
            }
            .pickerStyle(.segmented)
            .accessibilityLabel("Charge history range")

            let cutoff = Date().addingTimeInterval(-socRange.interval)
            let points = ChartData.line(
                samples.compactMap { sample in
                    sample.date.flatMap { $0 >= cutoff ? ($0, Double(sample.socPercent)) : nil }
                }
            )
            if points.isEmpty {
                noDataNote("No samples in this range.")
            } else {
                Chart(points) { point in
                    LineMark(
                        x: .value("Time", point.date),
                        y: .value("Charge", point.value),
                        series: .value("Segment", point.segmentKey)
                    )
                    .foregroundStyle(.green)
                    .interpolationMethod(.monotone)
                }
                .chartYScale(domain: 0...100)
                .chartYAxisLabel("%")
                .frame(height: 180)
            }
        }
    }

    // MARK: - Sessions

    private var sessionsCard: some View {
        SectionCard(title: "Sessions", systemImage: "clock.arrow.circlepath") {
            let sessions = Array(ChargeSession.detect(in: samples).suffix(10).reversed())
            if sessions.isEmpty {
                Text("No charging or discharging sessions recorded yet.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            } else {
                VStack(spacing: 0) {
                    ForEach(sessions) { session in
                        NavigationLink {
                            SessionDetailView(session: session)
                        } label: {
                            sessionRow(session)
                        }
                        .buttonStyle(.plain)
                        if session.id != sessions.last?.id {
                            Divider()
                        }
                    }
                }
            }
        }
    }

    private func sessionRow(_ session: ChargeSession) -> some View {
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
            VStack(alignment: .trailing, spacing: 2) {
                Text("\(session.startSoc)% → \(session.endSoc)%")
                    .font(.subheadline.monospacedDigit().weight(.medium))
                Text(Format.duration(session.durationHours))
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
            Image(systemName: "chevron.right")
                .font(.caption.weight(.semibold))
                .foregroundStyle(.tertiary)
        }
        .padding(.vertical, 8)
        .contentShape(Rectangle())
    }

    // MARK: - Recent cell voltages

    private var recentCellsCard: some View {
        SectionCard(title: "Cell Voltages", systemImage: "battery.100percent") {
            let cutoff = Date().addingTimeInterval(-6 * 60 * 60)
            let recent = samples.filter { ($0.date ?? .distantPast) >= cutoff }
            let points = ChartData.cellLines(recent)
            if points.isEmpty {
                noDataNote("No samples in the last 6 hours.")
            } else {
                Chart(points) { point in
                    LineMark(
                        x: .value("Time", point.date),
                        y: .value("Voltage", point.value),
                        series: .value("Segment", point.segmentKey)
                    )
                    .foregroundStyle(by: .value("Cell", point.series))
                    .interpolationMethod(.monotone)
                }
                .chartForegroundStyleScale(ChartData.cellColors)
                .chartYScale(domain: ChartData.voltageDomain(points))
                .chartYAxisLabel("V")
                .chartScrollableAxes(.horizontal)
                .chartXVisibleDomain(length: 2 * 60 * 60)
                .chartScrollPosition(initialX: (points.last?.date ?? Date()).addingTimeInterval(-2 * 60 * 60))
                .frame(height: 200)
            }
        }
    }

    // MARK: - Health

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

    private func noDataNote(_ text: String) -> some View {
        Text(text)
            .font(.subheadline)
            .foregroundStyle(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
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

private enum SocRange: String, CaseIterable, Identifiable {
    case day
    case week

    var id: String { rawValue }
    var title: String {
        switch self {
        case .day: "24h"
        case .week: "7d"
        }
    }
    var interval: TimeInterval {
        switch self {
        case .day: 24 * 60 * 60
        case .week: 7 * 24 * 60 * 60
        }
    }
}

// MARK: - Session detail

/// One charging or discharging run, carrying its own samples so the detail
/// view can plot the session at its natural time scale.
struct ChargeSession: Identifiable {
    let flow: PowerFlow
    let samples: [HistorySample]

    var start: Date { samples.first?.date ?? .distantPast }
    var end: Date { samples.last?.date ?? .distantPast }
    var durationHours: Double { end.timeIntervalSince(start) / 3600 }
    var startSoc: Int { samples.first?.socPercent ?? 0 }
    var endSoc: Int { samples.last?.socPercent ?? 0 }
    var averageCurrentMa: Int {
        guard !samples.isEmpty else { return 0 }
        return samples.map(\.currentMa).reduce(0, +) / samples.count
    }
    var id: String { "\(flow.title)-\(start.timeIntervalSinceReferenceDate)" }

    /// Groups the dated samples into charging/discharging runs. Balancing counts
    /// as part of a charge (it is the tail of one), short idle blips (< 5 min)
    /// don't split a session, and gaps > 20 min always do.
    static func detect(in samples: [HistorySample]) -> [ChargeSession] {
        var sessions: [ChargeSession] = []
        var flow: PowerFlow?
        var run: [HistorySample] = []
        var trailingIdleCount = 0
        var idleSince: Date?
        var previousDate: Date?

        func sessionFlow(_ sample: HistorySample) -> PowerFlow? {
            switch sample.state {
            case .charging, .balancing: .charging
            case .discharging: .discharging
            default: nil
            }
        }

        func close() {
            if trailingIdleCount > 0 {
                run.removeLast(trailingIdleCount)
            }
            if let flow, isValid(run, flow) {
                sessions.append(ChargeSession(flow: flow, samples: run))
            }
            flow = nil
            run = []
            trailingIdleCount = 0
            idleSince = nil
        }

        func isValid(_ run: [HistorySample], _ flow: PowerFlow) -> Bool {
            guard run.count >= 5,
                  let first = run.first?.date,
                  let last = run.last?.date,
                  last.timeIntervalSince(first) >= 10 * 60 else { return false }
            // Require real charge/discharge activity so a lone balancing run
            // doesn't show up as a charge session.
            let primary: PackState = flow == .charging ? .charging : .discharging
            return run.contains { $0.state == primary }
        }

        for sample in samples {
            guard let date = sample.date else { continue }
            if let previousDate, date.timeIntervalSince(previousDate) > 20 * 60 {
                close()
            }
            let sampleFlow = sessionFlow(sample)
            if flow == nil {
                if let sampleFlow {
                    flow = sampleFlow
                    run = [sample]
                }
            } else if sampleFlow == flow {
                run.append(sample)
                trailingIdleCount = 0
                idleSince = nil
            } else if sampleFlow == nil {
                if idleSince == nil { idleSince = date }
                if date.timeIntervalSince(idleSince ?? date) > 5 * 60 {
                    close()
                } else {
                    run.append(sample)
                    trailingIdleCount += 1
                }
            } else {
                close()
                flow = sampleFlow
                run = [sample]
            }
            previousDate = date
        }
        close()
        return sessions
    }
}

struct SessionDetailView: View {
    let session: ChargeSession

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                summaryCard
                chartCard("Current", systemImage: "bolt.fill") {
                    currentChart
                }
                chartCard("Cell Voltages", systemImage: "battery.100percent") {
                    cellsChart
                }
                chartCard("Temperature", systemImage: "thermometer.medium") {
                    temperatureChart
                }
            }
            .padding()
        }
        .navigationTitle(session.flow == .charging ? "Charge Session" : "Discharge Session")
        .navigationBarTitleDisplayMode(.inline)
        .background(Color(.systemGroupedBackground).ignoresSafeArea())
    }

    private var summaryCard: some View {
        SectionCard(title: nil, systemImage: nil) {
            HStack(spacing: 12) {
                summaryItem(Format.duration(session.durationHours), "Duration")
                Divider()
                summaryItem("\(session.startSoc)% → \(session.endSoc)%", "Charge")
                Divider()
                summaryItem(Format.current(Int16(clamping: session.averageCurrentMa)), "Avg Current")
            }
        }
    }

    /// For a charge session this is the CC/CV curve: the constant-current
    /// plateau followed by the taper as the cells approach the stop voltage.
    private var currentChart: some View {
        Chart(points(\.currentMa)) { point in
            LineMark(
                x: .value("Time", point.date),
                y: .value("Current", point.value),
                series: .value("Segment", point.segmentKey)
            )
            .foregroundStyle(Theme.flowColor(session.flow))
            .interpolationMethod(.monotone)
        }
        .chartXScale(domain: session.start...session.end)
        .chartYAxisLabel("mA")
        .frame(height: 200)
    }

    private var cellsChart: some View {
        Chart(ChartData.cellLines(session.samples)) { point in
            LineMark(
                x: .value("Time", point.date),
                y: .value("Voltage", point.value),
                series: .value("Segment", point.segmentKey)
            )
            .foregroundStyle(by: .value("Cell", point.series))
            .interpolationMethod(.monotone)
        }
        .chartForegroundStyleScale(ChartData.cellColors)
        .chartXScale(domain: session.start...session.end)
        .chartYScale(domain: ChartData.voltageDomain(ChartData.cellLines(session.samples)))
        .chartYAxisLabel("V")
        .frame(height: 200)
    }

    private var temperatureChart: some View {
        let temperaturePoints = points { $0.temperatureCentiC }.map {
            ChartData.Point(date: $0.date, value: $0.value / 100, series: $0.series, segment: $0.segment)
        }
        return Chart(temperaturePoints) { point in
            LineMark(
                x: .value("Time", point.date),
                y: .value("Temperature", point.value),
                series: .value("Segment", point.segmentKey)
            )
            .foregroundStyle(.orange)
            .interpolationMethod(.monotone)
        }
        .chartXScale(domain: session.start...session.end)
        .chartYScale(domain: ChartData.paddedDomain(temperaturePoints, padding: 2))
        .chartYAxisLabel("°C")
        .frame(height: 160)
    }

    private func points(_ value: (HistorySample) -> Int) -> [ChartData.Point] {
        ChartData.line(
            session.samples.compactMap { sample in
                sample.date.map { ($0, Double(value(sample))) }
            },
            targetCount: 400
        )
    }

    private func chartCard<ChartContent: View>(
        _ title: String,
        systemImage: String,
        @ViewBuilder chart: () -> ChartContent
    ) -> some View {
        SectionCard(title: title, systemImage: systemImage) {
            chart()
        }
    }

    private func summaryItem(_ value: String, _ title: String) -> some View {
        VStack(spacing: 4) {
            Text(value)
                .font(.headline.monospacedDigit())
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - Chart data preparation

/// Shared preparation for all history charts: per-bucket averaging (instead of
/// picking every Nth sample, which aliases noise into zig-zag) and splitting
/// line series at recording gaps so idle periods render as blank space instead
/// of long straight connectors.
enum ChartData {
    struct Point: Identifiable {
        let date: Date
        let value: Double
        let series: String
        let segment: Int
        var id: String { "\(series)-\(segment)-\(date.timeIntervalSinceReferenceDate)" }
        var segmentKey: String { "\(series)-\(segment)" }
    }

    static let cellColors: KeyValuePairs<String, Color> = [
        "Cell 1": .blue,
        "Cell 2": .green,
        "Cell 5": .orange,
    ]

    static func line(
        _ raw: [(Date, Double)],
        series: String = "value",
        targetCount: Int = 240
    ) -> [Point] {
        guard let first = raw.first?.0, let last = raw.last?.0 else { return [] }
        let span = last.timeIntervalSince(first)
        let bucketSec = max(60, span / Double(targetCount))

        var averaged: [(Date, Double)]
        if raw.count <= targetCount {
            averaged = raw
        } else {
            var buckets: [Int: (timeSum: TimeInterval, valueSum: Double, count: Int)] = [:]
            for (date, value) in raw {
                let index = Int(date.timeIntervalSince(first) / bucketSec)
                var bucket = buckets[index] ?? (0, 0, 0)
                bucket.timeSum += date.timeIntervalSince(first)
                bucket.valueSum += value
                bucket.count += 1
                buckets[index] = bucket
            }
            averaged = buckets
                .sorted { $0.key < $1.key }
                .map { _, bucket in
                    (
                        first.addingTimeInterval(bucket.timeSum / Double(bucket.count)),
                        bucket.valueSum / Double(bucket.count)
                    )
                }
        }

        // A gap only counts as a recording break when it clearly exceeds the
        // bucket spacing; otherwise coarse buckets would split everywhere.
        let gapLimit = max(10 * 60, bucketSec * 3)
        var result: [Point] = []
        var segment = 0
        var previous: Date?
        for (date, value) in averaged {
            if let previous, date.timeIntervalSince(previous) > gapLimit {
                segment += 1
            }
            result.append(Point(date: date, value: value, series: series, segment: segment))
            previous = date
        }
        return result
    }

    static func cellLines(_ samples: [HistorySample], targetCount: Int = 400) -> [Point] {
        let dated = samples.compactMap { sample in
            sample.date.map { (date: $0, sample: sample) }
        }
        let cells: [(String, (HistorySample) -> Int)] = [
            ("Cell 1", { $0.cell1Mv }),
            ("Cell 2", { $0.cell2Mv }),
            ("Cell 5", { $0.cell5Mv }),
        ]
        return cells.flatMap { name, value in
            line(
                dated.map { ($0.date, Double(value($0.sample)) / 1000) },
                series: name,
                targetCount: targetCount
            )
        }
    }

    static func voltageDomain(_ points: [Point]) -> ClosedRange<Double> {
        paddedDomain(points, padding: 0.05, fallback: 3.0...4.2)
    }

    static func paddedDomain(
        _ points: [Point],
        padding: Double,
        fallback: ClosedRange<Double> = 0...1
    ) -> ClosedRange<Double> {
        let values = points.map(\.value)
        guard let minimum = values.min(), let maximum = values.max(), minimum <= maximum else {
            return fallback
        }
        return (minimum - padding)...(maximum + padding)
    }
}
