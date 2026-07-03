import Combine
import Foundation
import SwiftData

@Model
final class HistorySample {
    @Attribute(.unique) var uniqueKey: String
    var deviceID: String
    var sequence: Int64?
    var bootID: Int64?
    var timestamp: Date?
    var uptimeSec: Int64
    var socPercent: Int
    var stateRawValue: Int
    var flags: Int
    var faults: Int
    var cell1Mv: Int
    var cell2Mv: Int
    var cell5Mv: Int
    var packMv: Int
    var currentMa: Int
    var temperatureCentiC: Int

    init(
        uniqueKey: String,
        deviceID: String,
        sequence: Int64? = nil,
        bootID: Int64? = nil,
        timestamp: Date?,
        uptimeSec: Int64,
        socPercent: Int,
        stateRawValue: Int,
        flags: Int,
        faults: Int,
        cell1Mv: Int,
        cell2Mv: Int,
        cell5Mv: Int,
        packMv: Int,
        currentMa: Int,
        temperatureCentiC: Int
    ) {
        self.uniqueKey = uniqueKey
        self.deviceID = deviceID
        self.sequence = sequence
        self.bootID = bootID
        self.timestamp = timestamp
        self.uptimeSec = uptimeSec
        self.socPercent = socPercent
        self.stateRawValue = stateRawValue
        self.flags = flags
        self.faults = faults
        self.cell1Mv = cell1Mv
        self.cell2Mv = cell2Mv
        self.cell5Mv = cell5Mv
        self.packMv = packMv
        self.currentMa = currentMa
        self.temperatureCentiC = temperatureCentiC
    }

    var state: PackState { PackState(rawValue: UInt8(stateRawValue)) ?? .fault }
    var date: Date? { timestamp }
    var temperatureC: Double { Double(temperatureCentiC) / 100 }
    var powerW: Double { Double(packMv) / 1000 * abs(Double(currentMa)) / 1000 }
    var cellDeltaMv: Int {
        max(cell1Mv, max(cell2Mv, cell5Mv)) - min(cell1Mv, min(cell2Mv, cell5Mv))
    }
}

struct HistoryStatus: Equatable {
    let state: UInt8
    let oldestSequence: UInt32
    let latestSequence: UInt32
    let bootID: UInt32

    static func parse(_ data: Data) -> HistoryStatus? {
        var reader = BinaryReader(data)
        guard let state = reader.u8(),
              let oldest = reader.u32(),
              let latest = reader.u32(),
              let boot = reader.u32() else { return nil }
        return HistoryStatus(state: state, oldestSequence: oldest, latestSequence: latest, bootID: boot)
    }
}

struct FirmwareHistoryRecord {
    let sequence: UInt32
    let bootID: UInt32
    let epochSec: UInt32
    let uptimeSec: UInt32
    let flags: UInt16
    let faults: UInt16
    let cell1Mv: UInt16
    let cell2Mv: UInt16
    let cell5Mv: UInt16
    let packMv: UInt16
    let currentMa: Int16
    let temperatureCentiC: Int16
    let socPercent: UInt8
    let state: UInt8

    static func parse(_ data: Data) -> FirmwareHistoryRecord? {
        var reader = BinaryReader(data)
        guard let sequence = reader.u32(),
              let bootID = reader.u32(),
              let epochSec = reader.u32(),
              let uptimeSec = reader.u32(),
              let flags = reader.u16(),
              let faults = reader.u16(),
              let cell1 = reader.u16(),
              let cell2 = reader.u16(),
              let cell5 = reader.u16(),
              let pack = reader.u16(),
              let current = reader.i16(),
              let temperature = reader.i16(),
              let soc = reader.u8(),
              let state = reader.u8() else { return nil }
        return FirmwareHistoryRecord(
            sequence: sequence,
            bootID: bootID,
            epochSec: epochSec,
            uptimeSec: uptimeSec,
            flags: flags,
            faults: faults,
            cell1Mv: cell1,
            cell2Mv: cell2,
            cell5Mv: cell5,
            packMv: pack,
            currentMa: current,
            temperatureCentiC: temperature,
            socPercent: soc,
            state: state
        )
    }
}

struct FirmwareHistoryChunk {
    let sequence: UInt32
    let index: UInt8
    let count: UInt8
    let payload: Data

    static func parse(_ data: Data) -> FirmwareHistoryChunk? {
        var reader = BinaryReader(data)
        guard let sequence = reader.u32(),
              let index = reader.u8(),
              let count = reader.u8(),
              let payloadLength = reader.u8(),
              count > 0,
              index < count,
              payloadLength <= 13,
              reader.offset + Int(payloadLength) <= data.count else { return nil }
        let start = reader.offset
        let payload = data.subdata(in: start..<(start + Int(payloadLength)))
        return FirmwareHistoryChunk(sequence: sequence, index: index, count: count, payload: payload)
    }
}

struct BatteryHealth: Equatable {
    let confidence: UInt8
    let learnedCapacityMah: UInt16
    let totalDischargedMah: UInt32
    let totalEnergyWh: Double
    let equivalentCycles: Double
    let hotMinutes: UInt32
    let maximumTemperatureC: Double
    let averageIdleDeltaMv: UInt16
    let maximumIdleDeltaMv: UInt16
    let validCapacityCycles: UInt16

    var isLearned: Bool { confidence > 0 }
    var healthPercent: Int {
        min(100, max(0, Int((Double(learnedCapacityMah) / Telemetry.usableCapacityMah * 100).rounded())))
    }

    static func parse(_ data: Data) -> BatteryHealth? {
        var reader = BinaryReader(data)
        guard reader.u8() != nil,
              let confidence = reader.u8(),
              let capacity = reader.u16(),
              let discharged = reader.u32(),
              let energyTenths = reader.u32(),
              let cyclesTenths = reader.u32(),
              let hotMinutes = reader.u32(),
              let maximumTemp = reader.i16(),
              let averageDelta = reader.u16(),
              let maximumDelta = reader.u16(),
              let validCycles = reader.u16() else { return nil }
        return BatteryHealth(
            confidence: confidence,
            learnedCapacityMah: capacity,
            totalDischargedMah: discharged,
            totalEnergyWh: Double(energyTenths) / 10,
            equivalentCycles: Double(cyclesTenths) / 10,
            hotMinutes: hotMinutes,
            maximumTemperatureC: Double(maximumTemp) / 100,
            averageIdleDeltaMv: averageDelta,
            maximumIdleDeltaMv: maximumDelta,
            validCapacityCycles: validCycles
        )
    }
}

@MainActor
final class HistoryStore: ObservableObject {
    let container: ModelContainer
    @Published private(set) var revision = 0
    @Published private(set) var syncStatus: HistoryStatus?

    private var lastLiveMinute: [String: Int] = [:]
    private var anchoredBoots: Set<String> = []
    private let defaults: UserDefaults

    init(inMemory: Bool = false, defaults: UserDefaults = .standard) {
        self.defaults = defaults
        let configuration = ModelConfiguration(isStoredInMemoryOnly: inMemory)
        do {
            container = try ModelContainer(for: HistorySample.self, configurations: configuration)
        } catch {
            fatalError("Unable to create Powerbank history store: \(error)")
        }
        prune()
    }

    func samples(since date: Date) -> [HistorySample] {
        let descriptor = FetchDescriptor<HistorySample>()
        let fetched = (try? container.mainContext.fetch(descriptor)) ?? []
        return fetched
            .filter { sample in
                guard let timestamp = sample.timestamp else { return false }
                return timestamp >= date
            }
            .sorted { ($0.timestamp ?? .distantPast) < ($1.timestamp ?? .distantPast) }
    }

    func unknownTimeSamples() -> [HistorySample] {
        let descriptor = FetchDescriptor<HistorySample>()
        return ((try? container.mainContext.fetch(descriptor)) ?? [])
            .filter { $0.timestamp == nil }
            .sorted { ($0.sequence ?? 0) < ($1.sequence ?? 0) }
    }

    func recordLive(_ telemetry: Telemetry, deviceID: String) {
        let minute = Int(telemetry.receivedAt.timeIntervalSince1970 / 60)
        guard lastLiveMinute[deviceID] != minute else { return }
        lastLiveMinute[deviceID] = minute
        let sample = HistorySample(
            uniqueKey: "live-\(deviceID)-\(minute)",
            deviceID: deviceID,
            timestamp: telemetry.receivedAt,
            uptimeSec: Int64(telemetry.uptimeSec),
            socPercent: Int(telemetry.socPercent),
            stateRawValue: Int(telemetry.state.rawValue),
            flags: Int(telemetry.flags),
            faults: Int(telemetry.faults),
            cell1Mv: Int(telemetry.cell1Mv),
            cell2Mv: Int(telemetry.cell2Mv),
            cell5Mv: Int(telemetry.cell5Mv),
            packMv: Int(telemetry.packMv),
            currentMa: Int(telemetry.currentMa),
            temperatureCentiC: Int(telemetry.dieTempCentiC)
        )
        container.mainContext.insert(sample)
        save()
    }

    func importRecord(_ record: FirmwareHistoryRecord, deviceID: String) {
        let key = "firmware-\(deviceID)-\(record.bootID)-\(record.sequence)"
        let descriptor = FetchDescriptor<HistorySample>()
        let existing = ((try? container.mainContext.fetch(descriptor)) ?? [])
            .first { $0.uniqueKey == key }
        let timestamp = timestamp(for: record, deviceID: deviceID)
        if let existing {
            if existing.timestamp == nil, let timestamp {
                existing.timestamp = timestamp
                save()
            }
        } else {
            let sample = HistorySample(
                uniqueKey: key,
                deviceID: deviceID,
                sequence: Int64(record.sequence),
                bootID: Int64(record.bootID),
                timestamp: timestamp,
                uptimeSec: Int64(record.uptimeSec),
                socPercent: Int(record.socPercent),
                stateRawValue: Int(record.state),
                flags: Int(record.flags),
                faults: Int(record.faults),
                cell1Mv: Int(record.cell1Mv),
                cell2Mv: Int(record.cell2Mv),
                cell5Mv: Int(record.cell5Mv),
                packMv: Int(record.packMv),
                currentMa: Int(record.currentMa),
                temperatureCentiC: Int(record.temperatureCentiC)
            )
            container.mainContext.insert(sample)
            guard save() else { return }
        }
        setLastSequence(record.sequence, for: deviceID)
    }

    func updateSyncStatus(_ status: HistoryStatus) {
        syncStatus = status
    }

    @discardableResult
    func prepareForSync(_ status: HistoryStatus, deviceID: String) -> Bool {
        let resetSequence = status.latestSequence < lastSequence(for: deviceID)
        if resetSequence {
            defaults.set(0, forKey: sequenceKey(deviceID))
        }
        updateSyncStatus(status)
        return resetSequence
    }

    func setBootAnchor(deviceID: String, bootID: UInt32, uptimeSec: UInt32) {
        let anchorID = "\(deviceID)-\(bootID)"
        guard !anchoredBoots.contains(anchorID) else { return }
        anchoredBoots.insert(anchorID)
        let startEpoch = Date().timeIntervalSince1970 - Double(uptimeSec)
        defaults.set(startEpoch, forKey: bootAnchorKey(deviceID: deviceID, bootID: bootID))

        let descriptor = FetchDescriptor<HistorySample>()
        guard let samples = try? container.mainContext.fetch(descriptor) else { return }
        var changed = false
        for sample in samples {
            guard sample.deviceID == deviceID,
                  sample.bootID == Int64(bootID),
                  sample.timestamp == nil else { continue }
            sample.timestamp = Date(timeIntervalSince1970: startEpoch + Double(sample.uptimeSec))
            changed = true
        }
        if changed {
            save()
        }
    }

    func lastSequence(for deviceID: String) -> UInt32 {
        UInt32(clamping: defaults.integer(forKey: sequenceKey(deviceID)))
    }

    func reset(deviceID: String) {
        let descriptor = FetchDescriptor<HistorySample>()
        if let samples = try? container.mainContext.fetch(descriptor) {
            for sample in samples where sample.deviceID == deviceID {
                container.mainContext.delete(sample)
            }
        }
        defaults.set(0, forKey: sequenceKey(deviceID))
        lastLiveMinute[deviceID] = nil
        anchoredBoots = Set(anchoredBoots.filter { !$0.hasPrefix("\(deviceID)-") })
        syncStatus = nil
        save()
    }

    private func setLastSequence(_ sequence: UInt32, for deviceID: String) {
        guard sequence > lastSequence(for: deviceID) else { return }
        defaults.set(Int(sequence), forKey: sequenceKey(deviceID))
    }

    private func timestamp(for record: FirmwareHistoryRecord, deviceID: String) -> Date? {
        if record.epochSec > 0 {
            return Date(timeIntervalSince1970: TimeInterval(record.epochSec))
        }
        let key = bootAnchorKey(deviceID: deviceID, bootID: record.bootID)
        let anchor = defaults.double(forKey: key)
        guard anchor > 0 else { return nil }
        return Date(timeIntervalSince1970: anchor + Double(record.uptimeSec))
    }

    private func prune() {
        let cutoff = Date().addingTimeInterval(-30 * 24 * 60 * 60)
        let descriptor = FetchDescriptor<HistorySample>()
        guard let samples = try? container.mainContext.fetch(descriptor) else { return }
        for sample in samples {
            if let timestamp = sample.timestamp, timestamp < cutoff {
                container.mainContext.delete(sample)
            }
        }
        let unknown = samples
            .filter { $0.timestamp == nil }
            .sorted { ($0.sequence ?? 0) > ($1.sequence ?? 0) }
        for sample in unknown.dropFirst(2000) {
            container.mainContext.delete(sample)
        }
        save()
    }

    @discardableResult
    private func save() -> Bool {
        do {
            try container.mainContext.save()
            revision &+= 1
            return true
        } catch {
            container.mainContext.rollback()
            return false
        }
    }

    private func sequenceKey(_ deviceID: String) -> String {
        "Powerbank.history.lastSequence.\(deviceID)"
    }

    private func bootAnchorKey(deviceID: String, bootID: UInt32) -> String {
        "Powerbank.history.bootAnchor.\(deviceID).\(bootID)"
    }
}

private struct BinaryReader {
    let data: Data
    var offset = 0

    init(_ data: Data) {
        self.data = data
    }

    mutating func u8() -> UInt8? {
        guard offset + 1 <= data.count else { return nil }
        defer { offset += 1 }
        return data[offset]
    }

    mutating func u16() -> UInt16? {
        guard offset + 2 <= data.count else { return nil }
        defer { offset += 2 }
        return UInt16(data[offset]) | UInt16(data[offset + 1]) << 8
    }

    mutating func i16() -> Int16? {
        u16().map(Int16.init(bitPattern:))
    }

    mutating func u32() -> UInt32? {
        guard offset + 4 <= data.count else { return nil }
        defer { offset += 4 }
        return UInt32(data[offset]) |
            UInt32(data[offset + 1]) << 8 |
            UInt32(data[offset + 2]) << 16 |
            UInt32(data[offset + 3]) << 24
    }
}
