import PhotosUI
import SwiftUI
import UIKit

/// 相册选图自动拼接：按选择顺序拼接同一页面连续截取的截图
struct AlbumStitchView: View {
    var onResult: (StitchOutput) -> Void
    var onCancel: () -> Void

    @State private var pickerVisible = false
    @State private var processing = false
    @State private var errorMessage: String?

    var body: some View {
        VStack(spacing: 20) {
            Text("在任意应用中连续截取多张屏幕截图（相邻两张有部分重叠），\n然后按顺序选择它们。")
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)

            if processing {
                ProgressView("正在拼接…")
            } else {
                Button {
                    errorMessage = nil
                    pickerVisible = true
                } label: {
                    Label("选择截图（2~15 张）", systemImage: "square.on.square")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
            }

            if let errorMessage {
                Text(errorMessage)
                    .font(.footnote)
                    .foregroundStyle(.red)
            }

            Button("返回") { onCancel() }
                .buttonStyle(.bordered)
        }
        .padding(24)
        .navigationTitle("相册拼接")
        .sheet(isPresented: $pickerVisible) {
            PhotoPicker { images in
                process(images)
            }
        }
    }

    private func process(_ images: [UIImage]) {
        guard images.count >= 2 else {
            errorMessage = "请至少选择 2 张截图"
            return
        }
        processing = true
        DispatchQueue.global(qos: .userInitiated).async {
            let output = StitchSession.stitch(images: images)
            DispatchQueue.main.async {
                processing = false
                if let output {
                    onResult(output)
                } else {
                    errorMessage = "拼接失败：请确认截图来自同一页面、按顺序且相邻之间有重叠"
                }
            }
        }
    }
}

// MARK: - PHPicker 封装（保持选择顺序）

private struct PhotoPicker: UIViewControllerRepresentable {
    var onPicked: ([UIImage]) -> Void

    func makeUIViewController(context: Context) -> PHPickerViewController {
        var config = PHPickerConfiguration()
        config.filter = .images
        config.selectionLimit = 15
        let controller = PHPickerViewController(configuration: config)
        controller.delegate = context.coordinator
        return controller
    }

    func updateUIViewController(_ uiViewController: PHPickerViewController, context: Context) {}

    func makeCoordinator() -> Coordinator {
        Coordinator(onPicked: onPicked)
    }

    final class Coordinator: NSObject, PHPickerViewControllerDelegate {
        let onPicked: ([UIImage]) -> Void
        init(onPicked: @escaping ([UIImage]) -> Void) {
            self.onPicked = onPicked
        }

        func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
            picker.dismiss(animated: true)
            // results 数组保持用户选择顺序
            let providers = results.map(\.itemProvider)
            guard !providers.isEmpty else {
                onPicked([])
                return
            }
            var images = [UIImage?](repeating: nil, count: providers.count)
            let group = DispatchGroup()
            for (i, provider) in providers.enumerated() where provider.canLoadObject(ofClass: UIImage.self) {
                group.enter()
                provider.loadObject(ofClass: UIImage.self) { object, _ in
                    images[i] = object as? UIImage
                    group.leave()
                }
            }
            group.notify(queue: .main) { [onPicked] in
                onPicked(images.compactMap { $0 })
            }
        }
    }
}
