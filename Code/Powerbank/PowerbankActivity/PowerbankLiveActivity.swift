import ActivityKit
import Foundation
import SwiftUI
import WidgetKit

@main
struct PowerbankActivityBundle: WidgetBundle {
    var body: some Widget {
        PowerbankLiveActivity()
    }
}

struct PowerbankLiveActivity: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: PowerbankActivityAttributes.self) { context in
            HStack(spacing: 14) {
                Image(systemName: context.state.charging ? "bolt.fill" : "arrow.up.circle.fill")
                    .font(.title2)
                    .foregroundStyle(context.state.charging ? .green : .blue)

                VStack(alignment: .leading, spacing: 3) {
                    Text(context.attributes.deviceName)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text(context.state.stale ? "Disconnected" : context.state.status)
                        .font(.headline)
                }

                Spacer()

                VStack(alignment: .trailing, spacing: 3) {
                    Text("\(context.state.socPercent)%")
                        .font(.title2.bold().monospacedDigit())
                    Text(detail(context.state))
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
            }
            .padding()
            .activityBackgroundTint(Color(.secondarySystemBackground))
            .activitySystemActionForegroundColor(.primary)
        } dynamicIsland: { context in
            DynamicIsland {
                DynamicIslandExpandedRegion(.leading) {
                    Label(
                        context.state.charging ? "Charging" : "Output",
                        systemImage: context.state.charging ? "bolt.fill" : "arrow.up.circle.fill"
                    )
                    .foregroundStyle(context.state.charging ? .green : .blue)
                }
                DynamicIslandExpandedRegion(.trailing) {
                    Text("\(context.state.socPercent)%")
                        .font(.headline.monospacedDigit())
                }
                DynamicIslandExpandedRegion(.bottom) {
                    HStack {
                        Text(context.state.status)
                        Spacer()
                        Text(detail(context.state))
                            .monospacedDigit()
                            .foregroundStyle(.secondary)
                    }
                    .font(.subheadline)
                }
            } compactLeading: {
                Image(systemName: context.state.charging ? "bolt.fill" : "arrow.up.circle.fill")
                    .foregroundStyle(context.state.charging ? .green : .blue)
            } compactTrailing: {
                Text("\(context.state.socPercent)%")
                    .monospacedDigit()
            } minimal: {
                Image(systemName: "battery.75percent")
            }
        }
    }

    private func detail(_ state: PowerbankActivityAttributes.ContentState) -> String {
        if state.stale {
            return "Last update"
        }
        if let etaMinutes = state.etaMinutes {
            let hours = etaMinutes / 60
            let minutes = etaMinutes % 60
            let power = String(format: "%.1f W", state.powerW)
            return hours > 0 ? "\(hours)h \(minutes)m · \(power)" : "\(minutes)m · \(power)"
        }
        return String(format: "%.1f W", state.powerW)
    }
}
