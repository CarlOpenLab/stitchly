import Photos
import SwiftUI
import UIKit

/// 结果预览：保存 / 分享
struct ResultView: View {
    let output: StitchOutput?
    var onDone: () -> Void

    @State private var savedMessage: String?

    var body: some View {
        VStack(spacing: 12) {
            if let image = output?.image {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                Text("没有可显示的结果")
                    .foregroundStyle(.secondary)
            }

            if let output {
                HStack(spacing: 12) {
                    if output.reachedBottom {
                        Label("已到页底", systemImage: "arrow.down.to.line")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                    if output.droppedCount > 0 {
                        Text("去重 \(output.droppedCount) 帧")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                    if output.gapCount > 0 {
                        Text("有 \(output.gapCount) 处未对齐")
                            .font(.footnote)
                            .foregroundStyle(.orange)
                    }
                }
            }

            HStack(spacing: 12) {
                Button {
                    save()
                } label: {
                    Label("保存到相册", systemImage: "square.and.arrow.down")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)

                Button {
                    onDone()
                } label: {
                    Text("完成")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
            }
            .controlSize(.large)

            if let savedMessage {
                Text(savedMessage)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(16)
        .navigationTitle("结果")
    }

    private func save() {
        guard let image = output?.image else { return }
        PHPhotoLibrary.requestAuthorization(for: .addOnly) { status in
            guard status == .authorized || status == .limited else {
                DispatchQueue.main.async { savedMessage = "未获得相册写入权限" }
                return
            }
            PHPhotoLibrary.shared().performChanges {
                PHAssetChangeRequest.creationRequestForAsset(from: image)
            } completionHandler: { ok, _ in
                DispatchQueue.main.async {
                    savedMessage = ok ? "已保存到相册" : "保存失败"
                }
            }
        }
    }
}
