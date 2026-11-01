import SwiftUI

struct HomeView: View {
    var onAlbum: () -> Void
    var onRecord: () -> Void

    var body: some View {
        VStack(spacing: 16) {
            Spacer()
            Text("长截图")
                .font(.largeTitle.bold())
            Text("相册拼接 / 录屏抽帧，任意应用都能截长图")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer().frame(height: 24)

            Button {
                onRecord()
            } label: {
                Label("录屏长截图", systemImage: "record.circle")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)

            Button {
                onAlbum()
            } label: {
                Label("相册截图拼接", systemImage: "photo.on.rectangle.angled")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .controlSize(.large)

            Spacer()
        }
        .padding(24)
        .navigationTitle("长截图")
    }
}
