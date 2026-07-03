import SwiftUI

struct ControlsView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager
    @EnvironmentObject private var alerts: PowerbankAlertManager
    @State private var developerMode = false
    @State private var pendingCommand: PowerbankCommand?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if !ble.canSendCommands {
                        notConnectedBanner
                            .transition(.move(edge: .top).combined(with: .opacity))
                    }

                    SectionCard(title: "Maintenance", systemImage: "wrench.and.screwdriver") {
                        commandRow(.clearFaults, tint: .blue)
                        Divider()
                        commandRow(.balanceOff, tint: .blue)
                    }

                    SectionCard(title: "Power", systemImage: "moon.zzz") {
                        commandRow(.ship, tint: .indigo)
                    }

                    if ble.connectionState.isConnected && alerts.shouldOfferPermission {
                        notificationPermissionCard
                            .transition(.move(edge: .top).combined(with: .opacity))
                    }

                    alertSection

                    developerSection
                }
                .padding()
                .animation(Theme.motion, value: ble.canSendCommands)
                .animation(Theme.motion, value: developerMode)
            }
            .navigationTitle("Controls")
            .background(Color(.systemGroupedBackground).ignoresSafeArea())
            .confirmationDialog(
                pendingCommand?.title ?? "",
                isPresented: confirmationBinding,
                titleVisibility: .visible
            ) {
                if let command = pendingCommand {
                    Button(command.title, role: command.isDestructive ? .destructive : nil) {
                        ble.send(command, confirmed: true)
                    }
                    Button("Cancel", role: .cancel) {}
                }
            } message: {
                if let command = pendingCommand {
                    Text(command.caption)
                }
            }
        }
    }

    private var developerSection: some View {
        SectionCard(title: "Developer", systemImage: "ladybug") {
            Toggle(isOn: $developerMode.animation()) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Developer overrides").font(.subheadline.weight(.medium))
                    Text("Advanced power and battery service actions. Use with care.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            if developerMode {
                Divider()
                    .transition(.opacity)
                commandRow(.chargeOn, tint: .green)
                    .transition(.move(edge: .top).combined(with: .opacity))
                Divider()
                    .transition(.opacity)
                commandRow(.chargeOff, tint: .orange)
                    .transition(.move(edge: .top).combined(with: .opacity))
                Divider()
                    .transition(.opacity)
                commandRow(.resetLearnedBattery, tint: .red)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
        }
    }

    private var alertSection: some View {
        SectionCard(title: "Alerts & Live Activity", systemImage: "bell.badge") {
            preferenceToggle("Charging complete", isOn: $alerts.chargingCompleteEnabled)
            Divider()
            preferenceToggle("Low cell", isOn: $alerts.lowCellEnabled)
            Divider()
            preferenceToggle("High temperature", isOn: $alerts.temperatureEnabled)
            Divider()
            preferenceToggle("Balancing limit", isOn: $alerts.balancingEnabled)
            Divider()
            preferenceToggle("Protection faults", isOn: $alerts.faultsEnabled)
            Divider()
            preferenceToggle("Live Activity", isOn: $alerts.liveActivitiesEnabled)
        }
    }

    private var notificationPermissionCard: some View {
        SectionCard(title: nil, systemImage: nil) {
            HStack(spacing: 12) {
                Image(systemName: "bell.badge.fill")
                    .font(.title3)
                    .foregroundStyle(.blue)
                VStack(alignment: .leading, spacing: 2) {
                    Text("Stay informed")
                        .font(.subheadline.weight(.semibold))
                    Text("Allow notifications for battery and safety changes.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button("Enable") {
                    alerts.requestPermission()
                }
                .buttonStyle(.borderedProminent)
                .buttonBorderShape(.capsule)
            }
        }
    }

    private var notConnectedBanner: some View {
        HStack(spacing: 12) {
            Image(systemName: "antenna.radiowaves.left.and.right.slash")
                .foregroundStyle(.orange)
            Text("Connect to the Powerbank to send commands.")
                .font(.subheadline)
            Spacer()
        }
        .padding(14)
        .background(.orange.opacity(0.12), in: RoundedRectangle(cornerRadius: 14))
    }

    private func commandRow(_ command: PowerbankCommand, tint: Color) -> some View {
        Button {
            trigger(command)
        } label: {
            HStack(spacing: 14) {
                Image(systemName: command.systemImage)
                    .font(.headline)
                    .foregroundStyle(ble.canSendCommands ? tint : .secondary)
                    .frame(width: 30)
                VStack(alignment: .leading, spacing: 2) {
                    Text(command.title)
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(.primary)
                    Text(command.caption)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.leading)
                }
                Spacer()
                Image(systemName: "chevron.right")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.tertiary)
            }
            .contentShape(Rectangle())
            .padding(.vertical, 4)
        }
        .buttonStyle(.plain)
        .disabled(!ble.canSendCommands)
        .opacity(ble.canSendCommands ? 1 : 0.5)
        .animation(Theme.motion, value: ble.canSendCommands)
    }

    private func preferenceToggle(_ title: String, isOn: Binding<Bool>) -> some View {
        Toggle(title, isOn: isOn)
            .font(.subheadline)
            .tint(.accentColor)
    }

    private func trigger(_ command: PowerbankCommand) {
        if command.requiresConfirmation || command.isDestructive {
            pendingCommand = command
        } else {
            ble.send(command)
        }
    }

    private var confirmationBinding: Binding<Bool> {
        Binding(
            get: { pendingCommand != nil },
            set: { if !$0 { pendingCommand = nil } }
        )
    }
}
