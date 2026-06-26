import SwiftUI

@main
struct PowerbankApp: App {
    @StateObject private var ble = PowerbankBLEManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(ble)
        }
    }
}
