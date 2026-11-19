#include "KshIOSFileDrop.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface KSHFileDropDelegate : NSObject <UIDropInteractionDelegate>
{
@public
    juce::Component::SafePointer<juce::Component> target;
    std::function<ksh::IOSFileDrop::FileReady()> begin;
}
@end

namespace
{
    UTType* droppedType (id<UIDropSession> session)
    {
        if (session.items.count != 1)
            return nil;
        auto* provider = session.items.firstObject.itemProvider;
        for (UTType* type in @[ UTTypeMIDI, UTTypeWAV ])
            if ([provider hasItemConformingToTypeIdentifier:type.identifier])
                return type;
        return nil;
    }
}

@implementation KSHFileDropDelegate
- (BOOL)dropInteraction:(UIDropInteraction*)interaction canHandleSession:(id<UIDropSession>)session
{
    (void) interaction;
    return target != nullptr && droppedType (session) != nil;
}

- (UIDropProposal*)dropInteraction:(UIDropInteraction*)interaction sessionDidUpdate:(id<UIDropSession>)session
{
    bool inside = false;
    if (auto* view = interaction.view)
        if (auto* component = target.getComponent())
            if (auto* peer = component->getPeer())
            {
                const auto point = [session locationInView:view];
                const auto local = component->getLocalPoint (&peer->getComponent(), juce::Point<int> (juce::roundToInt (point.x), juce::roundToInt (point.y)));
                inside = component->isShowing() && component->getLocalBounds().contains (local);
            }
    return [[UIDropProposal alloc] initWithDropOperation:inside ? UIDropOperationCopy : UIDropOperationCancel];
}

- (void)dropInteraction:(UIDropInteraction*)interaction performDrop:(id<UIDropSession>)session
{
    (void) interaction;
    auto* type = droppedType (session);
    if (target == nullptr || type == nil)
        return;

    const auto ready = begin();
    const juce::String extension = [type isEqual:UTTypeMIDI] ? ".mid" : ".wav";
    [session.items.firstObject.itemProvider loadFileRepresentationForTypeIdentifier:type.identifier
        completionHandler:^(NSURL* url, NSError* error)
        {
            std::shared_ptr<juce::TemporaryFile> staged;
            if (url != nil && error == nil)
            {
                // The item provider removes its copy as soon as this callback returns.
                auto copy = std::make_shared<juce::TemporaryFile> (extension);
                const juce::File source (juce::String::fromUTF8 (url.fileSystemRepresentation));
                if (source.copyFileTo (copy->getFile()))
                    staged = std::move (copy);
            }
            dispatch_async (dispatch_get_main_queue(), ^{ ready (staged); });
        }];
}
@end

namespace ksh
{
    struct IOSFileDrop::Native
    {
        KSHFileDropDelegate* delegate = [[KSHFileDropDelegate alloc] init];
        UIDropInteraction* interaction = [[UIDropInteraction alloc] initWithDelegate:delegate];
        __weak UIView* view = nil;

        void attach (UIView* next)
        {
            if (view == next)
                return;
            [view removeInteraction:interaction];
            view = next;
            [view addInteraction:interaction];
        }
    };

    IOSFileDrop::IOSFileDrop (juce::Component& component, std::function<FileReady()> begin)
        : ComponentMovementWatcher (&component), native (std::make_unique<Native>())
    {
        native->delegate->target = &component;
        native->delegate->begin = std::move (begin);
        componentPeerChanged();
    }

    IOSFileDrop::~IOSFileDrop() { native->attach (nil); }

    void IOSFileDrop::componentPeerChanged()
    {
        auto* component = getComponent();
        auto* peer = component != nullptr && component->isShowing() ? component->getPeer() : nullptr;
        native->attach (peer != nullptr ? (__bridge UIView*) peer->getNativeHandle() : nil);
    }
}
