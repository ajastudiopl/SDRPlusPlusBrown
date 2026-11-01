import Foundation

struct SDRStatus: Decodable {
    let ready: Bool
    let httpListening: Bool
    let mainLoopStarted: Bool
}

struct PlaybackStatus: Decodable {
    let playing: Bool
}

struct DemodStatus: Decodable {
    let demod: String
}

struct DemodList: Decodable {
    struct Demod: Decodable, Identifiable {
        let name: String
        let id: Int
    }
    let demods: [Demod]
}

enum SDRClientError: LocalizedError {
    case invalidAddress
    case badResponse
    case server(String)

    var errorDescription: String? {
        switch self {
        case .invalidAddress: return "Podaj adres Maca w sieci lokalnej, np. 192.168.1.20."
        case .badResponse: return "Serwer zwrócił nieprawidłową odpowiedź."
        case .server(let message): return message
        }
    }
}

struct SDRClient {
    let host: String
    let port: Int

    private var baseURL: URL? {
        let trimmed = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, (1...65535).contains(port),
              !trimmed.contains("/"), !trimmed.contains(":"), !trimmed.contains(" ") else { return nil }
        return URL(string: "http://\(trimmed):\(port)")
    }

    private func request(_ path: String, method: String = "GET", body: Data? = nil) async throws -> Data {
        guard let baseURL, let url = URL(string: path, relativeTo: baseURL)?.absoluteURL else {
            throw SDRClientError.invalidAddress
        }
        var request = URLRequest(url: url)
        request.httpMethod = method
        request.httpBody = body
        request.timeoutInterval = 5
        if body != nil { request.setValue("application/json", forHTTPHeaderField: "Content-Type") }
        let (data, response) = try await URLSession.shared.data(for: request)
        guard let response = response as? HTTPURLResponse, response.statusCode == 200 else {
            throw SDRClientError.badResponse
        }
        if let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           let message = object["error"] as? String {
            throw SDRClientError.server(message)
        }
        return data
    }

    func status() async throws -> SDRStatus {
        let data = try await request("/status")
        return try JSONDecoder().decode(SDRStatus.self, from: data)
    }

    func playback() async throws -> PlaybackStatus {
        let data = try await request("/sdr/status")
        return try JSONDecoder().decode(PlaybackStatus.self, from: data)
    }

    func sources() async throws -> [String] {
        let data = try await request("/proc/source/type:options")
        return try JSONDecoder().decode([String].self, from: data)
    }

    func selectedSource() async throws -> String {
        String(decoding: try await request("/proc/source/type"), as: UTF8.self)
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    func setSource(_ name: String) async throws {
        _ = try await request("/proc/source/type", method: "POST", body: Data(name.utf8))
    }

    func setPlaying(_ playing: Bool) async throws {
        _ = try await request(playing ? "/sdr/start" : "/sdr/stop")
    }

    private func radioCommand(_ command: String, args: String = "") async throws -> Data {
        let body = try JSONSerialization.data(withJSONObject: ["cmd": command, "args": args])
        return try await request("/module/Radio/command", method: "POST", body: body)
    }

    func demods() async throws -> [DemodList.Demod] {
        let data = try await radioCommand("list_demods")
        return try JSONDecoder().decode(DemodList.self, from: data).demods
    }

    func selectedDemod() async throws -> String {
        let data = try await radioCommand("get_demod")
        return try JSONDecoder().decode(DemodStatus.self, from: data).demod
    }

    func setDemod(_ name: String) async throws {
        _ = try await radioCommand("set_demod", args: name)
    }

    func tune(hertz: Int64) async throws {
        _ = try await radioCommand("set_freq", args: String(hertz))
    }
}
