import UIKit

/// 拼接结果
struct StitchOutput {
    let image: UIImage
    let reachedBottom: Bool
    let droppedCount: Int
    let gapCount: Int
}

/// Swift 侧对 core 的封装：RGBA <-> UIImage、一次性拼接、流式计划
enum StitchSession {

    /// RGBA8888 数据转 UIImage
    static func image(fromRGBA data: Data, width: Int, height: Int) -> UIImage? {
        guard width > 0, height > 0, data.count == width * height * 4 else { return nil }
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        var bytes = [UInt8](data)
        guard let ctx = CGContext(
            data: &bytes,
            width: width, height: height,
            bitsPerComponent: 8, bytesPerRow: width * 4,
            space: colorSpace,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
                | CGBitmapInfo.byteOrder32Big.rawValue,
        ) else { return nil }
        guard let cg = ctx.makeImage() else { return nil }
        return UIImage(cgImage: cg)
    }

    /// UIImage → RGBA8888 数据（绘制到指定尺寸，宽度上限防止内存暴涨）
    static func rgba(from image: UIImage, targetWidth: Int) -> (data: Data, width: Int, height: Int)? {
        guard targetWidth > 0 else { return nil }
        let scale = Double(targetWidth) / Double(image.size.width * image.scale)
        guard scale > 0 else { return nil }
        let targetHeight = max(1, Int((Double(image.size.height * image.scale) * scale).rounded()))
        guard let ctx = CGContext(
            data: nil,
            width: targetWidth, height: targetHeight,
            bitsPerComponent: 8, bytesPerRow: targetWidth * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
                | CGBitmapInfo.byteOrder32Big.rawValue,
        ) else { return nil }
        ctx.interpolationQuality = .medium
        guard let cg = image.cgImage else { return nil }
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: targetWidth, height: targetHeight))
        guard let data = ctx.data else { return nil }
        return (Data(bytes: data, count: targetWidth * targetHeight * 4), targetWidth, targetHeight)
    }

    /// 一次性拼接（相册模式）
    static func stitch(images: [UIImage], maxWidth: Int = 1080) -> StitchOutput? {
        guard images.count >= 2 else { return nil }
        // 统一到首图尺寸
        let first = images[0]
        guard let base = rgba(from: first, targetWidth: min(maxWidth, Int(first.size.width * first.scale)))
        else { return nil }
        var frames: [Data] = [base.data]
        for img in images.dropFirst() {
            guard let r = rgba(from: img, targetWidth: base.width) else { return nil }
            frames.append(r.data)
        }
        let result = LSStitcher.stitchImages(
            frames.map { $0 as NSData },
            width: Int32(base.width),
            height: Int32(base.height),
            options: nil,
        )
        return output(from: result)
    }

    static func output(from result: LSResultObj?) -> StitchOutput? {
        guard let result,
              let img = image(fromRGBA: result.pixels as Data,
                              width: Int(result.width), height: Int(result.height))
        else { return nil }
        return StitchOutput(
            image: img,
            reachedBottom: result.reachedBottom,
            droppedCount: Int(result.droppedCount),
            gapCount: Int(result.gapCount),
        )
    }
}

/// 流式拼接计划（录屏抽帧模式）
final class StitchPlan {
    private var plan: UnsafeMutableRawPointer?
    private(set) var frameCount = 0
    let width: Int
    let height: Int

    init?(width: Int, height: Int) {
        self.width = width
        self.height = height
        plan = LSStitcher.createPlan(
            withWidth: Int32(width), height: Int32(height), options: nil,
        )
        if plan == nil { return nil }
    }

    /// 加入一帧 RGBA（core 内部仅保留缩小灰度图，数据可立即释放）
    func addFrame(rgba: Data) -> Bool {
        guard let plan else { return false }
        let ok = LSStitcher.addFrameRGBA(rgba, to: plan)
        if ok { frameCount += 1 }
        return ok
    }

    var isLastFrameDuplicate: Bool {
        guard let plan else { return false }
        return LSStitcher.isLastFrameDuplicate(in: plan)
    }

    /// 对齐并合成；provider 按帧下标重新取回全分辨率 RGBA
    func compose(provider: @escaping (Int32) -> Data?) -> StitchOutput? {
        guard let plan else { return nil }
        let result = LSStitcher.composePlan(plan) { index in provider(index) }
        return StitchSession.output(from: result)
    }

    deinit {
        if let plan {
            LSStitcher.freePlan(plan)
        }
    }
}
