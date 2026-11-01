import SwiftUI

@MainActor
final class RemoteState: ObservableObject {
    @Published var connected = false
    @Published var busy = false
    @Published var playing = false
    @Published var sources: [String] = []
    @Published var source = ""
    @Published var demods: [DemodList.Demod] = []
    @Published var demod = ""
    @Published var message = ""
    @Published var lastTunedHz: Int64?

    func refresh(host: String, port: Int) async {
        let client = SDRClient(host: host, port: port)
        do {
            let status = try await client.status()
            guard status.ready && status.mainLoopStarted else {
                throw SDRClientError.server("SDR++ jeszcze się uruchamia.")
            }
            async let playback = client.playback()
            async let sources = client.sources()
            async let source = client.selectedSource()
            async let demods = client.demods()
            async let demod = client.selectedDemod()
            self.playing = try await playback.playing
            self.sources = try await sources
            self.source = try await source
            self.demods = try await demods
            self.demod = try await demod
            connected = true
            message = ""
        } catch {
            connected = false
            message = error.localizedDescription
        }
    }

    func perform(host: String, port: Int, action: @escaping (SDRClient) async throws -> Void) async {
        guard !busy else { return }
        busy = true
        defer { busy = false }
        do {
            try await action(SDRClient(host: host, port: port))
            await refresh(host: host, port: port)
        } catch {
            message = error.localizedDescription
        }
    }
}

struct ContentView: View {
    @AppStorage("macHost") private var host = "192.168.15.160"
    @AppStorage("macPort") private var portText = "8080"
    @StateObject private var state = RemoteState()
    @State private var frequencyMHz = ""
    @State private var stepKHz = 100.0

    private var clientHost: String { host.trimmingCharacters(in: .whitespacesAndNewlines) }
    private var port: Int { Int(portText) ?? 0 }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 22) {
                    connectionCard
                    if state.connected {
                        playbackCard
                        tuningCard
                        sourceCard
                        demodCard
                    }
                    if !state.message.isEmpty {
                        Text(state.message)
                            .foregroundStyle(.red)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    Text("Sterowanie działa w tej samej zaufanej sieci Wi-Fi. Mac musi mieć uruchomiony SDR++ z serwerem HTTP na podanym porcie.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: 760)
                .padding(24)
                .frame(maxWidth: .infinity)
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("SDR Remote")
            .task {
                while !Task.isCancelled {
                    if !state.busy { await state.refresh(host: clientHost, port: port) }
                    try? await Task.sleep(for: .seconds(5))
                }
            }
            .refreshable { await state.refresh(host: clientHost, port: port) }
        }
    }

    private var connectionCard: some View {
        card("Połączenie", systemImage: "network") {
            HStack {
                Circle()
                    .fill(state.connected ? .green : .red)
                    .frame(width: 10, height: 10)
                Text(state.connected ? "Połączono z SDR++" : "Brak połączenia")
                    .fontWeight(.medium)
            }
            HStack {
                TextField("Adres IP Maca", text: $host)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .keyboardType(.numbersAndPunctuation)
                    .textFieldStyle(.roundedBorder)
                TextField("Port", text: $portText)
                    .keyboardType(.numberPad)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 90)
            }
            Button("Połącz / odśwież") {
                Task { await state.refresh(host: clientHost, port: port) }
            }
            .buttonStyle(.borderedProminent)
        }
    }

    private var playbackCard: some View {
        card("Odbiór", systemImage: "waveform") {
            HStack {
                Label(state.playing ? "Odbiór włączony" : "Odbiór zatrzymany",
                      systemImage: state.playing ? "dot.radiowaves.left.and.right" : "pause.circle")
                Spacer()
                Button(state.playing ? "Zatrzymaj" : "Uruchom") {
                    let target = !state.playing
                    Task { await state.perform(host: clientHost, port: port) { try await $0.setPlaying(target) } }
                }
                .buttonStyle(.borderedProminent)
                .disabled(state.busy)
            }
        }
    }

    private var tuningCard: some View {
        card("Częstotliwość", systemImage: "dial.low") {
            if let value = state.lastTunedHz {
                Text("Ostatnio wysłano: \(Double(value) / 1_000_000, specifier: "%.6f") MHz")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            HStack {
                TextField("np. 145,500", text: $frequencyMHz)
                    .keyboardType(.decimalPad)
                    .textFieldStyle(.roundedBorder)
                    .font(.title2.monospacedDigit())
                Text("MHz")
                Button("Ustaw") { tuneEnteredFrequency() }
                    .buttonStyle(.borderedProminent)
                    .disabled(state.busy)
            }
            HStack {
                Button("−") { stepFrequency(-1) }
                Spacer()
                Picker("Krok", selection: $stepKHz) {
                    Text("1 kHz").tag(1.0)
                    Text("10 kHz").tag(10.0)
                    Text("100 kHz").tag(100.0)
                    Text("1 MHz").tag(1000.0)
                }
                .pickerStyle(.menu)
                Spacer()
                Button("+") { stepFrequency(1) }
            }
            .buttonStyle(.bordered)
            .disabled(state.busy)
            Text("Wartość początkową wpisz ręcznie. Obecne API Maca nie udostępnia odczytu bieżącej częstotliwości.")
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
    }

    private var sourceCard: some View {
        card("Źródło sygnału", systemImage: "antenna.radiowaves.left.and.right") {
            Picker("Źródło", selection: Binding(
                get: { state.source },
                set: { selected in
                    Task { await state.perform(host: clientHost, port: port) { try await $0.setSource(selected) } }
                }
            )) {
                ForEach(state.sources, id: \.self) { Text($0).tag($0) }
            }
            .pickerStyle(.menu)
            .disabled(state.busy)
        }
    }

    private var demodCard: some View {
        card("Demodulator", systemImage: "slider.horizontal.3") {
            Picker("Tryb", selection: Binding(
                get: { state.demod },
                set: { selected in
                    Task { await state.perform(host: clientHost, port: port) { try await $0.setDemod(selected) } }
                }
            )) {
                ForEach(state.demods) { Text($0.name).tag($0.name) }
            }
            .pickerStyle(.segmented)
            .disabled(state.busy)
        }
    }

    private func card<Content: View>(_ title: String, systemImage: String,
                                     @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            Label(title, systemImage: systemImage)
                .font(.headline)
            content()
        }
        .padding(20)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 20))
    }

    private func tuneEnteredFrequency() {
        let normalized = frequencyMHz.replacingOccurrences(of: ",", with: ".")
        guard let mhz = Double(normalized), mhz > 0, mhz < 10_000 else {
            state.message = "Wpisz poprawną częstotliwość w MHz."
            return
        }
        let hertz = Int64((mhz * 1_000_000).rounded())
        Task {
            await state.perform(host: clientHost, port: port) { try await $0.tune(hertz: hertz) }
            if state.message.isEmpty { state.lastTunedHz = hertz }
        }
    }

    private func stepFrequency(_ direction: Int) {
        let normalized = frequencyMHz.replacingOccurrences(of: ",", with: ".")
        guard let mhz = Double(normalized), mhz > 0 else {
            state.message = "Najpierw wpisz częstotliwość."
            return
        }
        let next = mhz + Double(direction) * stepKHz / 1000
        frequencyMHz = String(format: "%.6f", next)
        tuneEnteredFrequency()
    }
}
