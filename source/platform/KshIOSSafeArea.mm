#include "KshIOSSafeArea.h"

#import <UIKit/UIKit.h>

juce::Rectangle<float> ksh::iosSafeArea (const juce::Component& editor)
{
    const auto localBounds = editor.getLocalBounds().toFloat();
    auto* peer = editor.getPeer();
    if (peer == nullptr)
        return localBounds;

    UIView* view = (__bridge UIView*) peer->getNativeHandle();
    if (view.window == nil)
        return localBounds;

    const auto safe = view.safeAreaLayoutGuide.layoutFrame;
    const auto origin = view.bounds.origin;
    const juce::Rectangle<float> peerSafeArea (static_cast<float> (safe.origin.x - origin.x),
                                              static_cast<float> (safe.origin.y - origin.y),
                                              static_cast<float> (safe.size.width),
                                              static_cast<float> (safe.size.height));
    const auto& peerComponent = peer->getComponent();
    const auto editorSafeArea = editor.getLocalArea (&peerComponent, peerSafeArea / peerComponent.getDesktopScaleFactor());
    return localBounds.getIntersection (editorSafeArea);
}
