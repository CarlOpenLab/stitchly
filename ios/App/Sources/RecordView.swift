import ReplayKit
import SwiftUI

/// 录屏长截图：拉起系统 Broadcast，用户边滚动边录屏，
/// 停止后扩展把视频写入 App Group，本页轮询到新视频即抽帧拼接。
struct RecordView: View {
    var onResult: (StitchOutput) -> Void
    var onCancel: () -> Void

    @State private var stage: Stage = .guide
    @State private var statusText = ""

    private enum Stage {
        case guide      // 引导开始录屏
        case recording  // 录屏进行中（用户在其他 App 滚动）
        case processing // 抽帧 + 拼接
    }

    private let timer = Timer.publish(every: 1.5, on: .main, in: .common).autoconnect()

    var body: some View {
        VStack(spacing: 20) {
            switch stage {
            case .guide:
                guide
            case .recording:
                recording
            case .processing:
                ProgressView("正在抽帧并拼接…")
            }
        }
        .padding(24)
        .navigationTitle("录屏长截图")
        .onReceive(timer) { _ in
            guard stage != .processing else { return }
            if let url = AppGroupStore.pendingRecordings().first {
                stage = .processing
                process(url)
            } else if AppGroupStore.isRecording() {
                stage = .recording
            }
        }
    }

    private var guide: some View {
        VStack(spacing: 20) {
            Text(
                """
                1️⃣ 点击下方圆点按钮，选择「长截图录屏」开始直播

                2️⃣ 切到目标应用，缓慢匀速滚动页面

                3️⃣ 滚动完成后在控制中心停止录制，回到本页等待自动拼接
                """,
            )
            .font(.callout)
            .multilineTextAlignment(.leading)

            BroadcastPickerView()
                .frame(width: 64, height: 64)

            Button("返回") { onCancel() }
                .buttonStyle(.bordered)
        }
    }

    private var recording: some View {
        VStack(spacing: 16) {
            Circle()
                .fill(.red)
                .frame(width: 12, height: 12)
            Text("录制中…请去目标应用缓慢滚动页面")
                .font(.callout)
                .multilineTextAlignment(.center)
            Text("录完后在控制中心停止，回到本页等待自动处理")
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
    }

    /// 抽帧（逐帧落盘 JPEG）→ 流式计划 → 按文件合成
    private func process(_ videoURL: URL) {
        DispatchQueue.global(qos: .userInitiated).async {
            defer { AppGroupStore.markProcessed(videoURL) }

            let workDir = FileManager.default.temporaryDirectory
                .appendingPathComponent("longshot-\(UUID().uuidString)", isDirectory: true)
            try? FileManager.default.createDirectory(at: workDir, withIntermediateDirectories: true)
            defer { try? FileManager.default.removeItem(at: workDir) }

            var savedCount = 0
            var firstSize: (w: Int, h: Int)?
            var plan: StitchPlan?

            // 第一遍：抽帧落盘 + 建立计划（core 只保留缩小灰度图）
            FrameExtractor.extract(from: videoURL, fps: 8) { _, data, w, h in
                if plan == nil {
                    firstSize = (w, h)
                    plan = StitchPlan(width: w, height: h)
                }
                if let img = StitchSession.image(fromRGBA: data, width: w, height: h),
                   let jpeg = img.jpegData(compressionQuality: 0.85) {
                    let f = workDir.appendingPathComponent(String(format: "frame_%05d.jpg", savedCount))
                    try? jpeg.write(to: f)
                    savedCount += 1
                }
                return plan?.addFrame(rgba: data) ?? true
            }

            guard let plan, savedCount >= 2, let size = firstSize else {
                DispatchQueue.main.async { fail("录屏太短或画面无滚动，未生成长图") }
                return
            }

            // 第二遍：按需解码 JPEG 合成
            let output = plan.compose { index in
                let f = workDir.appendingPathComponent(String(format: "frame_%05d.jpg", index))
                guard let img = UIImage(contentsOfFile: f.path) else { return nil }
                return StitchSession.rgba(from: img, targetWidth: size.w)?.data
            }

            DispatchQueue.main.async {
                if let output {
                    onResult(output)
                } else {
                    fail("拼接失败，请重试（录屏时请缓慢匀速滚动）")
                }
            }
        }
    }

    private func fail(_ message: String) {
        statusText = message
        stage = .guide
    }
}

/// RPSystemBroadcastPickerView 封装（点击即弹出系统选择器）
struct BroadcastPickerView: UIViewRepresentable {
    func makeUIView(context: Context) -> RPSystemBroadcastPickerView {
        let view = RPSystemBroadcastPickerView(frame: .zero)
        view.preferredExtension = "com.longshot.app.broadcast"
        view.showsMicrophoneButton = false
        return view
    }

    func updateUIView(_ uiView: RPSystemBroadcastPickerView, context: Context) {}
}
