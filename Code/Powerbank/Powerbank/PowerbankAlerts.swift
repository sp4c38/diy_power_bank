import Combine
import Foundation
import UserNotifications

@MainActor
final class PowerbankAlertManager: ObservableObject {
    @Published private(set) var authorizationStatus: UNAuthorizationStatus = .notDetermined
    @Published var chargingCompleteEnabled: Bool { didSet { savePreferences() } }
    @Published var lowCellEnabled: Bool { didSet { savePreferences() } }
    @Published var temperatureEnabled: Bool { didSet { savePreferences() } }
    @Published var balancingEnabled: Bool { didSet { savePreferences() } }
    @Published var faultsEnabled: Bool { didSet { savePreferences() } }
    @Published var liveActivitiesEnabled: Bool { didSet { savePreferences() } }

    private let center = UNUserNotificationCenter.current()
    private let defaults: UserDefaults
    private var initialized = false
    private var lowCellNotified = false
    private var highTemperatureStartedAt: Date?
    private var highTemperatureNotified = false
    private var previousChargeComplete = false
    private var previousBalanceTimedOut = false
    private var previousFaults: UInt16 = 0

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        chargingCompleteEnabled = defaults.preference(forKey: Keys.chargingComplete)
        lowCellEnabled = defaults.preference(forKey: Keys.lowCell)
        temperatureEnabled = defaults.preference(forKey: Keys.temperature)
        balancingEnabled = defaults.preference(forKey: Keys.balancing)
        faultsEnabled = defaults.preference(forKey: Keys.faults)
        liveActivitiesEnabled = defaults.preference(forKey: Keys.liveActivities)
        refreshAuthorizationStatus()
    }

    var shouldOfferPermission: Bool {
        authorizationStatus == .notDetermined
    }

    func requestPermission() {
        center.requestAuthorization(options: [.alert, .sound]) { [weak self] _, _ in
            Task { @MainActor in
                self?.refreshAuthorizationStatus()
            }
        }
    }

    func refreshAuthorizationStatus() {
        center.getNotificationSettings { [weak self] settings in
            Task { @MainActor in
                self?.authorizationStatus = settings.authorizationStatus
            }
        }
    }

    func process(_ telemetry: Telemetry) {
        if !initialized {
            initialized = true
            lowCellNotified = telemetry.minCellMv <= 3300
            highTemperatureNotified = telemetry.dieTempCentiC >= 5000
            previousChargeComplete = telemetry.chargeComplete
            previousBalanceTimedOut = telemetry.balanceTimedOut
            previousFaults = telemetry.faults
            return
        }

        if chargingCompleteEnabled && telemetry.chargeComplete && !previousChargeComplete {
            notify(
                identifier: "powerbank.charge-complete",
                title: "Powerbank charged",
                body: "Charging is complete at \(telemetry.socPercent)%."
            )
        }
        previousChargeComplete = telemetry.chargeComplete

        if telemetry.minCellMv >= 3400 {
            lowCellNotified = false
        } else if lowCellEnabled && telemetry.minCellMv <= 3300 && !lowCellNotified {
            lowCellNotified = true
            notify(
                identifier: "powerbank.low-cell",
                title: "Low battery cell",
                body: "The lowest cell is \(Format.volts(telemetry.minCellMv))."
            )
        }

        if telemetry.dieTempCentiC <= 4500 {
            highTemperatureStartedAt = nil
            highTemperatureNotified = false
        } else if telemetry.dieTempCentiC < 5000 {
            highTemperatureStartedAt = nil
        } else if telemetry.dieTempCentiC >= 5000 {
            if highTemperatureStartedAt == nil {
                highTemperatureStartedAt = telemetry.receivedAt
            }
            if temperatureEnabled,
               !highTemperatureNotified,
               let startedAt = highTemperatureStartedAt,
               telemetry.receivedAt.timeIntervalSince(startedAt) >= 30 {
                highTemperatureNotified = true
                notify(
                    identifier: "powerbank.temperature",
                    title: "Powerbank temperature is high",
                    body: "The monitor is at \(Format.temperature(telemetry.temperatureC))."
                )
            }
        }

        if balancingEnabled && telemetry.balanceTimedOut && !previousBalanceTimedOut {
            notify(
                identifier: "powerbank.balance-timeout",
                title: "Balancing needs attention",
                body: "Balancing stopped after 30 minutes with a \(telemetry.cellDeltaMv) mV cell difference."
            )
        }
        previousBalanceTimedOut = telemetry.balanceTimedOut

        let newFaults = telemetry.faults & ~previousFaults
        if faultsEnabled && newFaults != 0 {
            let names = PowerbankFault.decode(newFaults).map(\.title).joined(separator: ", ")
            notify(
                identifier: "powerbank.faults.\(newFaults)",
                title: "Powerbank fault",
                body: names.isEmpty ? "A new protection fault was reported." : names
            )
        }
        previousFaults = telemetry.faults
    }

    private func notify(identifier: String, title: String, body: String) {
        guard authorizationStatus == .authorized || authorizationStatus == .provisional else { return }
        let content = UNMutableNotificationContent()
        content.title = title
        content.body = body
        content.sound = .default
        center.add(UNNotificationRequest(identifier: identifier, content: content, trigger: nil))
    }

    private func savePreferences() {
        defaults.set(chargingCompleteEnabled, forKey: Keys.chargingComplete)
        defaults.set(lowCellEnabled, forKey: Keys.lowCell)
        defaults.set(temperatureEnabled, forKey: Keys.temperature)
        defaults.set(balancingEnabled, forKey: Keys.balancing)
        defaults.set(faultsEnabled, forKey: Keys.faults)
        defaults.set(liveActivitiesEnabled, forKey: Keys.liveActivities)
    }

    private enum Keys {
        static let chargingComplete = "Powerbank.alert.chargingComplete"
        static let lowCell = "Powerbank.alert.lowCell"
        static let temperature = "Powerbank.alert.temperature"
        static let balancing = "Powerbank.alert.balancing"
        static let faults = "Powerbank.alert.faults"
        static let liveActivities = "Powerbank.liveActivities"
    }
}

private extension UserDefaults {
    func preference(forKey key: String) -> Bool {
        object(forKey: key) == nil ? true : bool(forKey: key)
    }
}
