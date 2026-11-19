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
    struct DropType
    {
        UTType* type = nil;
        const char* extension = "";
    };

    DropType droppedType (id<UIDropSession> session)
    {
        if (session.items.count != 1)
            return {};
        auto* provider = session.items.firstObject.itemProvider;
        auto* preset = [UTType typeWithFilenameExtension:@"sbb"];
        if (preset != nil && [provider hasItemConformingToTypeIdentifier:preset.identifier])
            return { preset, ".sbb" };
        if ([[provider.suggestedName.pathExtension lowercaseString] isEqualToString:@"sbb"]
            && [provider hasItemConformingToTypeIdentifier:UTTypeData.identifier])
            return { UTTypeData, ".sbb" };
        if ([provider hasItemConformingToTypeIdentifier:UTTypeMIDI.identifier])
            return { UTTypeMIDI, ".mid" };
        if ([provider hasItemConformingToTypeIdentifier:UTTypeWAV.identifier])
            return { UTTypeWAV, ".wav" };
        if ([provider hasItemConformingToTypeIdentifier:UTTypeAudio.identifier])
            return { UTTypeAudio, ".audio" };
        return {};
    }
}

@implementation KSHFileDropDelegate
- (BOOL)dropInteraction:(UIDropInteraction*)interaction canHandleSession:(id<UIDropSession>)session
{
    (void) interaction;
    return target != nullptr && droppedType (session).type != nil;
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
    const auto drop = droppedType (session);
    if (target == nullptr || drop.type == nil)
        return;

    const auto ready = begin();
    const juce::String extension = drop.extension;
    auto* provider = session.items.firstObject.itemProvider;
    const auto suggestedName = juce::String::fromUTF8 (provider.suggestedName.UTF8String);
    [provider loadFileRepresentationForTypeIdentifier:drop.type.identifier
        completionHandler:^(NSURL* url, NSError* error)
        {
            std::shared_ptr<juce::TemporaryFile> staged;
            auto originalName = suggestedName;
            if (url != nil && error == nil)
            {
                // The item provider removes its copy as soon as this callback returns.
                auto copy = std::make_shared<juce::TemporaryFile> (extension);
                const juce::File source (juce::String::fromUTF8 (url.fileSystemRepresentation));
                if (originalName.isEmpty())
                    originalName = source.getFileName();
                if (source.copyFileTo (copy->getFile()))
                    staged = std::move (copy);
            }
            dispatch_async (dispatch_get_main_queue(), ^{ ready (staged, originalName); });
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
