import AVFoundation
import CoreMedia
import ReplayKit

/// Broadcast Upload 扩展：把系统屏幕流实时编码为 H.264 写入 App Group。
/// 扩展内存限额很小（约 50MB），必须流式写文件，严禁囤积原始帧。
class SampleHandler: RPBroadcastSampleHandler {

    private var writer: AVAssetWriter?
    private var input: AVAssetWriterInput?
    private var adaptor: AVAssetWriterInputPixelBufferAdaptor?
    private var outputURL: URL?

    override func broadcastStarted(withSetupInfo setupInfo: [String: NSObject]?) {
        AppGroupStore.ensureDirs()
        AppGroupStore.setRecordingFlag(true)
    }

    override func broadcastFinished() {
        AppGroupStore.setRecordingFlag(false)
        input?.markAsFinished()
        if let writer {
            writer.finishWriting { [weak self] in
                guard let self, let url = self.outputURL,
                      self.writer?.status == .completed else { return }
                // 通知主 App：该文件可被轮询到；如需强提醒可用 CFNotificationCenterPostNotification
            }
        }
    }

    override func processVideoSampleBuffer(_ sampleBuffer: CMSampleBuffer) {
        guard CMSampleBufferDataIsReady(sampleBuffer),
              let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer)
        else { return }

        // 首帧：按画面尺寸创建 writer
        if writer == nil {
            startWriter(with: sampleBuffer)
        }
        guard let writer, writer.status == .writing,
              let input, let adaptor
        else { return }

        if input.isReadyForMoreMediaData {
            adaptor.append(pixelBuffer, withPresentationTime: CMSampleBufferGetPresentationTimeStamp(sampleBuffer))
        }
        // 未就绪时丢弃该帧：录屏帧率高，丢弃不影响拼接
    }

    private func startWriter(with sampleBuffer: CMSampleBuffer) {
        guard let dir = AppGroupStore.pendingDir else { return }
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent("record-\(Int(Date().timeIntervalSince1970 * 1000)).mp4")
        outputURL = url

        guard let w = try? AVAssetWriter(url: url, fileType: .mp4) else { return }
        let desc = CMSampleBufferGetFormatDescription(sampleBuffer)!
        let dims = CMVideoFormatDescriptionGetDimensions(desc)
        let settings: [String: Any] = [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: Int(dims.width),
            AVVideoHeightKey: Int(dims.height),
        ]
        let newInput = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
        newInput.expectsMediaDataInRealTime = true
        let newAdaptor = AVAssetWriterInputPixelBufferAdaptor(
            assetWriterInput: newInput,
            sourcePixelBufferAttributes: [
                kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            ],
        )
        w.add(newInput)
        w.startWriting()
        guard CMSampleBufferGetPresentationTimeStamp(sampleBuffer).isNumeric else { return }
        w.startSession(atSourceTime: CMSampleBufferGetPresentationTimeStamp(sampleBuffer))

        writer = w
        input = newInput
        adaptor = newAdaptor
    }
}
