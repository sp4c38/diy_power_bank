import SwiftData
import SwiftUI

@main
struct PowerbankApp: App {
    @StateObject private var historyStore: HistoryStore
    @StateObject private var alertManager: PowerbankAlertManager
    @StateObject private var ble: PowerbankBLEManager

    init() {
        let historyStore = HistoryStore()
        let alertManager = PowerbankAlertManager()
        let ble = PowerbankBLEManager(
            historyStore: historyStore,
            alertManager: alertManager
        )
        _historyStore = StateObject(wrappedValue: historyStore)
        _alertManager = StateObject(wrappedValue: alertManager)
        _ble = StateObject(wrappedValue: ble)
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(ble)
                .environmentObject(historyStore)
                .environmentObject(alertManager)
                .modelContainer(historyStore.container)
        }
    }
}
