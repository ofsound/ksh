import SwiftUI
import UIKit

@available(iOS 16.0, *)
private struct StandaloneContent: UIViewControllerRepresentable {
    let controller: UIViewController

    func makeUIViewController(context: Context) -> UIViewController {
        controller
    }

    func updateUIViewController(_ controller: UIViewController, context: Context) {}
}

@available(iOS 16.0, *)
private final class StandaloneIPadWindow: UIHostingController<AnyView> {
    private let content: UIViewController

    init(content: UIViewController) {
        self.content = content
        super.init(rootView: AnyView(
            StandaloneContent(controller: content)
                .ignoresSafeArea()
                .persistentSystemOverlays(.hidden)
        ))
    }

    @MainActor @preconcurrency required dynamic init?(coder: NSCoder) {
        return nil
    }

    override var supportedInterfaceOrientations: UIInterfaceOrientationMask {
        content.supportedInterfaceOrientations
    }

    override var childForStatusBarStyle: UIViewController? { content }
    override var childForStatusBarHidden: UIViewController? { content }
}

@MainActor
@_cdecl("kshHideIPadSystemOverlays")
func hideIPadSystemOverlays(_ nativeView: UnsafeRawPointer) {
    guard #available(iOS 16.0, *),
          let window = Unmanaged<UIView>.fromOpaque(nativeView).takeUnretainedValue().window,
          window.traitCollection.userInterfaceIdiom == .pad,
          let content = window.rootViewController,
          !(content is StandaloneIPadWindow) else { return }

    // Keep JUCE's controller and view together so it still handles layout and input.
    window.rootViewController = StandaloneIPadWindow(content: content)
}
