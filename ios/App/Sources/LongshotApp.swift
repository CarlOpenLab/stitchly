import SwiftUI

@main
struct LongshotApp: App {
    var body: some Scene {
        WindowGroup {
            AppRoot()
        }
    }
}

struct AppRoot: View {
    private enum Route {
        case home, album, record, result
    }

    @State private var route: Route = .home
    @State private var output: StitchOutput?

    var body: some View {
        NavigationStack {
            switch route {
            case .home:
                HomeView(
                    onAlbum: { route = .album },
                    onRecord: { route = .record },
                )
            case .album:
                AlbumStitchView { result in
                    output = result
                    route = .result
                } onCancel: {
                    route = .home
                }
            case .record:
                RecordView { result in
                    output = result
                    route = .result
                } onCancel: {
                    route = .home
                }
            case .result:
                ResultView(output: output) {
                    route = .home
                }
            }
        }
    }
}
