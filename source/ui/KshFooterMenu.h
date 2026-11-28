#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ksh::ui
{
    inline constexpr int popupMenuRowPadding = 14;
    inline constexpr auto anchoredPopupProperty = "anchoredPopup";

    struct AnchoredPopup final : juce::ReferenceCountedObject
    {
        juce::Component::SafePointer<juce::Component> window;
    };

    inline const juce::Component* openAnchoredPopup (const juce::Component& anchor)
    {
        const auto* popup = static_cast<const AnchoredPopup*> (anchor.getProperties()[anchoredPopupProperty].getObject());
        const auto* window = popup != nullptr ? popup->window.getComponent() : nullptr;
        return window != nullptr && window->isVisible() ? window : nullptr;
    }

    inline void showAnchoredPopup (juce::PopupMenu& menu, const juce::PopupMenu::Options& options,
                                   std::function<void (int)> onClose, bool consumeOutsideClick = false)
    {
        const juce::Component::SafePointer<juce::Component> anchor (options.getTargetComponent());
        if (anchor == nullptr || menu.getNumItems() == 0)
            return;

        // JUCE can dismiss the popup synchronously and deliver the same press below it.
        // Give that press a component with no action while the popup is open.
        std::shared_ptr<juce::Component> dismissShield;
        if (consumeOutsideClick)
            if (auto* parent = options.getParentComponent())
            {
                dismissShield = std::make_shared<juce::Component>();
                dismissShield->setBounds (parent->getLocalBounds());
                dismissShield->setAlwaysOnTop (true);
                parent->addAndMakeVisible (*dismissShield);
            }
        const juce::var popupState (new AnchoredPopup());
        const auto* previousModal = juce::Component::getCurrentlyModalComponent();
        menu.showMenuAsync (options, [anchor, popupState, dismissShield, completion = std::move (onClose)] (int result)
        {
            if (anchor != nullptr && anchor->getProperties()[anchoredPopupProperty].getObject() == popupState.getObject())
            {
                anchor->getProperties().remove (anchoredPopupProperty);
                anchor->repaint();
            }
            if (dismissShield != nullptr)
                dismissShield->setVisible (false);
            completion (result);
        });
        // JUCE creates the modal menu window synchronously before dispatching its result later.
        auto* window = juce::Component::getCurrentlyModalComponent();
        if (anchor != nullptr && window != nullptr && window != previousModal)
        {
            static_cast<AnchoredPopup*> (popupState.getObject())->window = window;
            anchor->getProperties().set (anchoredPopupProperty, popupState);
            anchor->repaint();
            window->repaint();
        }
    }

    inline juce::PopupMenu::Options footerMenuOptions (juce::Component& anchor)
    {
        return juce::PopupMenu::Options().withTargetComponent (anchor)
            .withParentComponent (anchor.getTopLevelComponent())
            .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards)
            .withStandardItemHeight (44)
            .withMinimumWidth (260)
            .withMaximumNumColumns (1);
    }
}
