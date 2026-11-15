// Render every Tongval item with the best installed Dutch voice (+ English glosses) for Spreekbuis.
// swift tools/render.swift build/items.json [--stories] [--force]
// Output: ~/Library/Application Support/Spreekbuis/voice/{nl,en}/<id>.wav + manifest.json (word-onset frames at 48 kHz)
import Foundation
import AVFoundation

let args = CommandLine.arguments
guard args.count > 1, let data = FileManager.default.contents(atPath: args[1]),
      let items = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] else {
    print("usage: render.swift items.json [--stories] [--force]"); exit(1)
}
let withStories = args.contains("--stories"), force = args.contains("--force")
let root = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
    .appendingPathComponent("Spreekbuis/voice", isDirectory: true)
for d in ["nl", "en"] { try? FileManager.default.createDirectory(at: root.appendingPathComponent(d), withIntermediateDirectories: true) }

func best(_ prefix: String, prefer: [String]) -> AVSpeechSynthesisVoice? {
    let vs = AVSpeechSynthesisVoice.speechVoices().filter { $0.language == prefix }
    return vs.max { a, b in
        let sa = a.quality.rawValue * 10 + (prefer.contains(a.name) ? 1 : 0)
        let sb = b.quality.rawValue * 10 + (prefer.contains(b.name) ? 1 : 0)
        return sa < sb
    }
}
guard let nlVoice = best("nl-NL", prefer: ["Xander", "Claire"]) else { print("no nl-NL voice installed"); exit(1) }
let enVoice = best("en-US", prefer: ["Samantha", "Ava", "Zoe"]) ?? AVSpeechSynthesisVoice(language: "en-US")!
print("Dutch voice: \(nlVoice.name) (quality \(nlVoice.quality.rawValue)); English: \(enVoice.name)")

let outFmt = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: 48000, channels: 1, interleaved: false)!

final class Renderer: NSObject, AVSpeechSynthesizerDelegate {
    let synth = AVSpeechSynthesizer()
    var frames: AVAudioFramePosition = 0
    var srcRate: Double = 22050
    var onsets: [Int] = []
    override init() { super.init(); synth.delegate = self }
    func speechSynthesizer(_ s: AVSpeechSynthesizer, willSpeakRangeOfSpeechString r: NSRange, utterance: AVSpeechUtterance) {
        onsets.append(Int(Double(frames) * 48000.0 / srcRate))
    }
    // returns (samples at 48k, onsets in 48k frames)
    func render(_ text: String, voice: AVSpeechSynthesisVoice) -> ([Float], [Int]) {
        let u = AVSpeechUtterance(string: text)
        u.voice = voice
        u.rate = AVSpeechUtteranceDefaultSpeechRate * 0.95
        frames = 0; onsets = []
        var chunks: [AVAudioPCMBuffer] = []
        var done = false
        synth.write(u) { buf in
            guard let pcm = buf as? AVAudioPCMBuffer else { return }
            if pcm.frameLength == 0 { done = true; return }
            self.srcRate = pcm.format.sampleRate
            chunks.append(pcm)
            self.frames += AVAudioFramePosition(pcm.frameLength)
        }
        let deadline = Date().addingTimeInterval(60)
        while !done && Date() < deadline { RunLoop.current.run(until: Date().addingTimeInterval(0.005)) }
        guard let first = chunks.first else { return ([], []) }
        // concatenate + convert to 48k float mono
        let total = chunks.reduce(0) { $0 + Int($1.frameLength) }
        let joined = AVAudioPCMBuffer(pcmFormat: first.format, frameCapacity: AVAudioFrameCount(total))!
        for c in chunks {
            let n = Int(c.frameLength), off = Int(joined.frameLength)
            if let s = c.int16ChannelData, let d = joined.int16ChannelData { d[0].advanced(by: off).update(from: s[0], count: n) }
            else if let s = c.floatChannelData, let d = joined.floatChannelData { d[0].advanced(by: off).update(from: s[0], count: n) }
            joined.frameLength += AVAudioFrameCount(n)
        }
        let conv = AVAudioConverter(from: first.format, to: outFmt)!
        conv.sampleRateConverterQuality = AVAudioQuality.max.rawValue
        let cap = AVAudioFrameCount(Double(total) * 48000 / first.format.sampleRate + 4096)
        let out = AVAudioPCMBuffer(pcmFormat: outFmt, frameCapacity: cap)!
        var fed = false
        var err: NSError?
        conv.convert(to: out, error: &err) { _, status in
            if fed { status.pointee = .endOfStream; return nil }
            fed = true; status.pointee = .haveData; return joined
        }
        let p = out.floatChannelData![0]
        var samples = Array(UnsafeBufferPointer(start: p, count: Int(out.frameLength)))
        // trim leading/trailing near-silence, keep onsets aligned
        let thr: Float = 0.002
        var a = 0; while a < samples.count && abs(samples[a]) < thr { a += 1 }
        var b = samples.count; while b > a && abs(samples[b - 1]) < thr { b -= 1 }
        a = max(0, a - 480); b = min(samples.count, b + 2400)
        if a < b { samples = Array(samples[a..<b]) }
        let ons = onsets.map { max(0, $0 - a) }
        return (samples, ons)
    }
}

func writeWav(_ s: [Float], to url: URL) {
    let settings: [String: Any] = [AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: 48000, AVNumberOfChannelsKey: 1,
                                   AVLinearPCMBitDepthKey: 16, AVLinearPCMIsFloatKey: false, AVLinearPCMIsBigEndianKey: false]
    guard let f = try? AVAudioFile(forWriting: url, settings: settings, commonFormat: .pcmFormatFloat32, interleaved: false),
          let b = AVAudioPCMBuffer(pcmFormat: outFmt, frameCapacity: AVAudioFrameCount(max(1, s.count))) else { return }
    b.frameLength = AVAudioFrameCount(s.count)
    s.withUnsafeBufferPointer { b.floatChannelData![0].update(from: $0.baseAddress!, count: s.count) }
    try? f.write(from: b)
}

func fileId(_ key: String) -> String {
    // stable short id: FNV-1a 64
    var h: UInt64 = 0xcbf29ce484222325
    for c in key.utf8 { h ^= UInt64(c); h = h &* 0x100000001b3 }
    return String(format: "%016llx", h)
}

let manifestURL = root.appendingPathComponent("manifest.json")
var old: [String: [String: Any]] = [:]
if !force, let d = FileManager.default.contents(atPath: manifestURL.path),
   let j = try? JSONSerialization.jsonObject(with: d) as? [String: Any], (j["voice"] as? String) == nlVoice.identifier,
   let its = j["items"] as? [[String: Any]] {
    for i in its { if let k = i["key"] as? String { old[k] = i } }
}
let r = Renderer()
var outItems: [[String: Any]] = []
var n = 0
for it in items {
    guard let key = it["key"] as? String, let nl = it["nl"] as? String, let en = it["en"] as? String else { continue }
    if (it["kind"] as? String) == "t" && !withStories { continue }
    n += 1
    let id = fileId(key)
    let nlURL = root.appendingPathComponent("nl/\(id).wav"), enURL = root.appendingPathComponent("en/\(id).wav")
    if let o = old[key], FileManager.default.fileExists(atPath: nlURL.path), FileManager.default.fileExists(atPath: enURL.path) {
        outItems.append(o); continue
    }
    let (s, ons) = r.render(nl, voice: nlVoice)
    if s.isEmpty { print("  (no audio) \(nl)"); continue }
    writeWav(s, to: nlURL)
    let (e, _) = r.render(en, voice: enVoice)
    writeWav(e, to: enURL)
    var rec = it
    rec["file"] = "nl/\(id).wav"; rec["en_file"] = "en/\(id).wav"; rec["onsets"] = ons; rec["frames"] = s.count
    outItems.append(rec)
    if n % 50 == 0 { print("  \(n) rendered…") }
}
let manifest: [String: Any] = ["version": 1, "rate": 48000, "voice": nlVoice.identifier, "voiceName": nlVoice.name,
                               "enVoice": enVoice.name, "items": outItems]
let md = try! JSONSerialization.data(withJSONObject: manifest, options: [.sortedKeys])
try! md.write(to: manifestURL)
print("done: \(outItems.count) items → \(root.path)")
