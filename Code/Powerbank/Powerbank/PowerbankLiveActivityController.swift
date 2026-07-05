import Foundation

#if os(iOS)
import ActivityKit

@MainActor
final class PowerbankLiveActivityController {
    private var activity: Activity<PowerbankActivityAttributes>?
    private var activeFlowStartedAt: Date?
    private var idleStartedAt: Date?
    private var disconnectTask: Task<Void, Never>?
    private var lastPublishedAt: Date?
    private var lastPublishedSoc: Int?
    private var lastPublishedStatus: String?
    private var etaMinutes: Int?

    init() {
        activity = Activity<PowerbankActivityAttributes>.activities.first
    }

    func process(_ telemetry: Telemetry, etaMinutes: Int?, enabled: Bool, deviceName: String) {
        disconnectTask?.cancel()
        disconnectTask = nil
        self.etaMinutes = etaMinutes

        guard enabled else {
            end()
            return
        }

        let meaningfulFlow = telemetry.currentMa > 20 || telemetry.currentMa < -50
        if meaningfulFlow {
            idleStartedAt = nil
            if activeFlowStartedAt == nil {
                activeFlowStartedAt = telemetry.receivedAt
            }
            if activity == nil,
               let startedAt = activeFlowStartedAt,
               telemetry.receivedAt.timeIntervalSince(startedAt) >= 15 {
                start(with: telemetry, deviceName: deviceName)
            } else {
                update(with: telemetry, stale: false)
            }
        } else {
            activeFlowStartedAt = nil
            if idleStartedAt == nil {
                idleStartedAt = telemetry.receivedAt
            }
            update(with: telemetry, stale: false)
            if let idleStartedAt,
               telemetry.receivedAt.timeIntervalSince(idleStartedAt) >= 120 {
                end(with: telemetry)
            }
        }
    }

    func disconnected(lastTelemetry: Telemetry?) {
        activeFlowStartedAt = nil
        idleStartedAt = nil
        etaMinutes = nil
        if let lastTelemetry {
            update(with: lastTelemetry, stale: true)
        }
        disconnectTask?.cancel()
        disconnectTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 300_000_000_000)
            guard !Task.isCancelled else { return }
            self?.end()
        }
    }

    private func start(with telemetry: Telemetry, deviceName: String) {
        guard ActivityAuthorizationInfo().areActivitiesEnabled else { return }
        do {
            activity = try Activity.request(
                attributes: PowerbankActivityAttributes(deviceName: deviceName),
                content: content(for: telemetry, stale: false),
                pushType: nil
            )
            rememberPublishedState(telemetry)
        } catch {
            activity = nil
        }
    }

    private func update(with telemetry: Telemetry, stale: Bool) {
        guard let activity else { return }
        let status = stale ? "Disconnected" : telemetry.state.title
        let materiallyChanged = lastPublishedSoc != Int(telemetry.socPercent) ||
            lastPublishedStatus != status
        let intervalElapsed = lastPublishedAt.map {
            telemetry.receivedAt.timeIntervalSince($0) >= 15
        } ?? true
        guard stale || materiallyChanged || intervalElapsed else { return }
        let content = content(for: telemetry, stale: stale)
        rememberPublishedState(telemetry, status: status)
        Task {
            await activity.update(content)
        }
    }

    private func end(with telemetry: Telemetry? = nil) {
        guard let activity else { return }
        self.activity = nil
        lastPublishedAt = nil
        lastPublishedSoc = nil
        lastPublishedStatus = nil
        disconnectTask?.cancel()
        disconnectTask = nil
        Task {
            let finalContent = telemetry.map { content(for: $0, stale: false) }
            await activity.end(finalContent, dismissalPolicy: .after(Date().addingTimeInterval(60)))
        }
    }

    private func content(for telemetry: Telemetry, stale: Bool) -> ActivityContent<PowerbankActivityAttributes.ContentState> {
        let state = PowerbankActivityAttributes.ContentState(
            socPercent: Int(telemetry.socPercent),
            status: stale ? "Disconnected" : telemetry.state.title,
            powerW: telemetry.powerW,
            etaMinutes: etaMinutes,
            charging: telemetry.flow == .charging,
            stale: stale,
            updatedAt: telemetry.receivedAt
        )
        return ActivityContent(
            state: state,
            staleDate: stale ? telemetry.receivedAt : telemetry.receivedAt.addingTimeInterval(10)
        )
    }

    private func rememberPublishedState(_ telemetry: Telemetry, status: String? = nil) {
        lastPublishedAt = telemetry.receivedAt
        lastPublishedSoc = Int(telemetry.socPercent)
        lastPublishedStatus = status ?? telemetry.state.title
    }
}
#else
@MainActor
final class PowerbankLiveActivityController {
    func process(_ telemetry: Telemetry, etaMinutes: Int?, enabled: Bool, deviceName: String) {}
    func disconnected(lastTelemetry: Telemetry?) {}
}
#endif
