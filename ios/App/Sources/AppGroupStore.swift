import Foundation

/// 主 App 与 Broadcast 扩展之间通过 App Group 容器交换录屏文件
enum AppGroupStore {
    static let groupID = "group.com.stitchly.app"

    static var containerURL: URL? {
        FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: groupID)
    }

    /// 扩展把录制完成的视频写到该目录
    static var pendingDir: URL? {
        containerURL?.appendingPathComponent("pending", isDirectory: true)
    }

    static var processedDir: URL? {
        containerURL?.appendingPathComponent("processed", isDirectory: true)
    }

    static func ensureDirs() {
        if let d = pendingDir { try? FileManager.default.createDirectory(at: d, withIntermediateDirectories: true) }
        if let d = processedDir { try? FileManager.default.createDirectory(at: d, withIntermediateDirectories: true) }
    }

    /// 扩展正在录制的标记（broadcastStarted 创建，broadcastFinished 删除）
    static var recordingFlag: URL? {
        containerURL?.appendingPathComponent("recording.flag")
    }

    static func setRecordingFlag(_ on: Bool) {
        guard let flag = recordingFlag else { return }
        if on {
            try? "1".write(to: flag, atomically: true, encoding: .utf8)
        } else {
            try? FileManager.default.removeItem(at: flag)
        }
    }

    static func isRecording() -> Bool {
        guard let flag = recordingFlag else { return false }
        return FileManager.default.fileExists(atPath: flag.path)
    }

    /// 待处理的录屏文件（按创建时间排序）
    static func pendingRecordings() -> [URL] {
        guard let dir = pendingDir,
              let items = try? FileManager.default.contentsOfDirectory(
                  at: dir, includingPropertiesForKeys: [.creationDateKey],
              )
        else { return [] }
        return items
            .filter { $0.pathExtension.lowercased() == "mp4" }
            .sorted { a, b in
                let da = (try? a.resourceValues(forKeys: [.creationDateKey]))?.creationDate ?? .distantPast
                let db = (try? b.resourceValues(forKeys: [.creationDateKey]))?.creationDate ?? .distantPast
                return da < db
            }
    }

    /// 处理完成后移走（保留一份供排查，超过 3 个删旧的）
    static func markProcessed(_ url: URL) {
        guard let dest = processedDir else { return }
        ensureDirs()
        let target = dest.appendingPathComponent(url.lastPathComponent)
        try? FileManager.default.moveItem(at: url, to: target)
        let leftovers = (try? FileManager.default.contentsOfDirectory(at: dest, includingPropertiesForKeys: nil)) ?? []
        if leftovers.count > 3 {
            for f in leftovers.prefix(leftovers.count - 3) {
                try? FileManager.default.removeItem(at: f)
            }
        }
    }
}
