import AVFoundation
import CoreGraphics
import Foundation

/// 从录屏视频中按固定间隔抽取 RGBA 帧（内存友好：逐帧产出）
enum FrameExtractor {

    /// 抽帧回调返回 false 可提前终止
    static func extract(
        from url: URL,
        fps: Int = 8,
        onFrame: (Int, Data, Int, Int) -> Bool,
    ) {
        let asset = AVURLAsset(url: url)
        let seconds: Double
        if #available(iOS 16.0, *) {
            guard let d = try? asset.load(.duration) else { return }
            seconds = CMTimeGetSeconds(d)
        } else {
            seconds = CMTimeGetSeconds(asset.duration)
        }
        guard seconds.isFinite, seconds > 0 else { return }

        let generator = AVAssetImageGenerator(asset: asset)
        generator.appliesPreferredTrackTransform = true
        generator.requestedTimeToleranceBefore = .zero
        generator.requestedTimeToleranceAfter = CMTime(seconds: 0.05, preferredTimescale: 600)

        var index = 0
        var t = 0.0
        let step = 1.0 / Double(max(1, fps))
        while t < seconds {
            let time = CMTime(seconds: t, preferredTimescale: 600)
            if let cg = try? generator.copyCGImage(at: time, actualTime: nil),
               let (data, w, h) = rgbaData(from: cg) {
                if !onFrame(index, data, w, h) { break }
                index += 1
            }
            t += step
        }
    }

    /// CGImage -> RGBA8888 Data
    static func rgbaData(from image: CGImage) -> (Data, Int, Int)? {
        let w = image.width
        let h = image.height
        guard let ctx = CGContext(
            data: nil,
            width: w, height: h,
            bitsPerComponent: 8, bytesPerRow: w * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
                | CGBitmapInfo.byteOrder32Big.rawValue,
        ) else { return nil }
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
        guard let buf = ctx.data else { return nil }
        return (Data(bytes: buf, count: w * h * 4), w, h)
    }
}
