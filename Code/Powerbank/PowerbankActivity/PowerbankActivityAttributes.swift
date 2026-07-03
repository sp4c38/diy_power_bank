import ActivityKit
import Foundation

struct PowerbankActivityAttributes: ActivityAttributes {
    struct ContentState: Codable, Hashable {
        let socPercent: Int
        let status: String
        let powerW: Double
        let etaMinutes: Int?
        let charging: Bool
        let stale: Bool
        let updatedAt: Date
    }

    let deviceName: String
}
