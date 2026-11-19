#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ksh::ui
{
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
