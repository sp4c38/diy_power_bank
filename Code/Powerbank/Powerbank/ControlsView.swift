import SwiftUI

struct ControlsView: View {
    @EnvironmentObject private var ble: PowerbankBLEManager
    @State private var developerMode = false
    @State private var pendingCommand: PowerbankCommand?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if !ble.canSendCommands {
                        notConnectedBanner
                    }

                    SectionCard(title: "Output", systemImage: "powerplug") {
                        commandRow(.outputOn, tint: .green)
                        Divider()
                        commandRow(.outputOff, tint: .red)
                    }

                    SectionCard(title: "Maintenance", systemImage: "wrench.and.screwdriver") {
                        commandRow(.clearFaults, tint: .blue)
                        Divider()
                        commandRow(.balanceOff, tint: .blue)
                        Divider()
                        commandRow(.rawDiagnostics, tint: .secondary)
                    }

                    SectionCard(title: "Power", systemImage: "moon.zzz") {
                        commandRow(.ship, tint: .indigo)
                    }

                    developerSection
                }
                .padding()
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
                    Text("Directly toggle the charge and discharge FETs. Use with care.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            if developerMode {
                Divider()
                commandRow(.chargeOn, tint: .green)
                Divider()
                commandRow(.chargeOff, tint: .orange)
                Divider()
                commandRow(.dischargeOn, tint: .green)
                Divider()
                commandRow(.dischargeOff, tint: .orange)
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
