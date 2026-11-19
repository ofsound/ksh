#pragma once
#include <melatonin_blur/melatonin_blur.h>
#include "BinaryData.h"
#include "../engine/KshTypes.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace ksh::ui
{
    inline const juce::Path& clearGlyph()
    {
        static const auto glyph = []
        {
            juce::Path diagonals, outline;
            diagonals.startNewSubPath (6, 6);
            diagonals.lineTo (18, 18);
            diagonals.startNewSubPath (18, 6);
            diagonals.lineTo (6, 18);
            juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (outline, diagonals);
            return outline;
        }();
        return glyph;
    }

    inline juce::Path recordLockShape (bool locked)
    {
        juce::Path loop, shape, stroke;
        loop.startNewSubPath (9, 11);
        loop.lineTo (9, 8);
        loop.cubicTo (9, 4, 15, 4, 15, 8);
        loop.lineTo (15, locked ? 11.0f : 6.0f);
        juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroke, loop);
        shape.addRoundedRectangle (6, 11, 12, 10, 2);
        shape.addPath (stroke);
        return shape;
    }

    struct Palette
    {
        struct SurfaceColours
        {
            juce::Colour app, shell, surface, raised, border;
        };

    private:
        static inline const SurfaceColours defaultSurfaces {
            juce::Colour (0xff0b0d0e), juce::Colour (0xff121719), juce::Colour (0xff192124),
            juce::Colour (0xff223036), juce::Colour (0xff38535c)
        };
        static constexpr float controlToneStrength = 0.30f;

    public:
        struct BackgroundTone
        {
            int brightness;
            int tint;
            bool operator== (const BackgroundTone&) const = default;
        };
        static inline SurfaceColours backgrounds = defaultSurfaces, controls = defaultSurfaces;
        static inline const juce::Colour& app = backgrounds.app;
        static inline const juce::Colour& shell = backgrounds.shell;
        static inline const juce::Colour& surface = backgrounds.surface;
        static inline const juce::Colour& raised = backgrounds.raised;
        static inline const juce::Colour& border = backgrounds.border;
        static inline const juce::Colour text { 0xfff4fafb }, muted { 0xff95b4bb }, faint { 0xff6f8f97 }, info { 0xff67e8f9 }, danger { 0xfffb7185 };
        static inline const juce::Colour orangeAccent { 0xfff1954a }, blueAccent { 0xff40add1 };
        static inline juce::Colour accent { orangeAccent };
        static juce::ChangeBroadcaster& changes()
        {
            static juce::ChangeBroadcaster changes;
            return changes;
        }
        static BackgroundTone backgroundTone() { return backdropTone; }
        static juce::Colour shadeBackground (juce::Colour base, float strength = 1.0f)
        {
            if (backdropTone.brightness != 0)
            {
                const bool brighten = backdropTone.brightness > 0;
                const float amount = static_cast<float> (std::abs (backdropTone.brightness)) / 100.0f;
                const auto target = brighten ? juce::Colours::white : juce::Colours::black;
                base = base.interpolatedWith (target.withAlpha (base.getAlpha()), amount * strength * (brighten ? 0.14f : 0.72f));
            }
            if (backdropTone.tint != 0)
            {
                const bool warm = backdropTone.tint > 0;
                const auto gray = juce::Colour::fromHSL (warm ? 0.10f : 0.61f, warm ? 0.12f : 0.22f,
                                                       base.getLightness(), base.getFloatAlpha());
                base = base.interpolatedWith (gray, static_cast<float> (std::abs (backdropTone.tint)) / 100.0f * strength);
            }
            return base;
        }
        static juce::Colour shadeControl (juce::Colour base) { return shadeBackground (base, controlToneStrength); }
        static void setBackgroundTone (BackgroundTone tone)
        {
            tone.brightness = juce::jlimit (-100, 100, tone.brightness);
            tone.tint = juce::jlimit (-100, 100, tone.tint);
            if (backdropTone == tone)
                return;
            backdropTone = tone;
            const auto shadeSurfaces = [] (float strength)
            {
                return SurfaceColours { shadeBackground (defaultSurfaces.app, strength), shadeBackground (defaultSurfaces.shell, strength),
                                        shadeBackground (defaultSurfaces.surface, strength), shadeBackground (defaultSurfaces.raised, strength),
                                        shadeBackground (defaultSurfaces.border, strength) };
            };
            backgrounds = shadeSurfaces (1.0f);
            controls = shadeSurfaces (controlToneStrength);
            changes().sendSynchronousChangeMessage();
        }
        static bool blueAccentSelected() { return useBlueAccent; }
        using AccentColours = std::array<std::optional<juce::Colour>, 2>;
        static std::optional<juce::Colour> accentOverride (bool blue) { return accentColours[blue ? 1 : 0]; }
        static juce::Colour accentColour (bool blue)
        {
            return accentOverride (blue).value_or (blue ? blueAccent : orangeAccent);
        }
        static void setAccentPreferences (bool blue, const AccentColours& colours)
        {
            if (useBlueAccent == blue && accentColours == colours)
                return;
            useBlueAccent = blue;
            accentColours = colours;
            accent = accentColour (blue);
            changes().sendSynchronousChangeMessage();
        }
        static void setBlueAccent (bool blue)
        {
            setAccentPreferences (blue, accentColours);
        }
        static void setAccentColour (bool blue, std::optional<juce::Colour> colour)
        {
            auto colours = accentColours;
            colours[blue ? 1 : 0] = colour;
            setAccentPreferences (useBlueAccent, colours);
        }
        static inline const juce::Colour play { 0xff76d99b };
        static inline const juce::Colour recording { 0xffef4444 };
        using TrackColours = std::array<juce::Colour, Constants::maxChannels>;
        static const TrackColours& defaultTrackColours()
        {
            static const TrackColours colours { juce::Colour (0xffdb4047), orangeAccent, juce::Colour (0xffd1a866), blueAccent,
                                                juce::Colour (0xff52b89c), juce::Colour (0xff8cb852), juce::Colour (0xffebad24), juce::Colour (0xffa66e3b) };
            return colours;
        }
        static const TrackColours& trackColours() { return editableTrackColours(); }
        static void setTrackColours (const TrackColours& colours)
        {
            if (editableTrackColours() == colours)
                return;
            editableTrackColours() = colours;
            changes().sendSynchronousChangeMessage();
        }
        static juce::Colour channel (int ch, bool colors = true)
        {
            return colors ? trackColours()[static_cast<size_t> (juce::jlimit (0, Constants::maxChannels - 1, ch))] : accent;
        }

    private:
        static inline BackgroundTone backdropTone { 0, 0 };
        static TrackColours& editableTrackColours()
        {
            static auto colours = defaultTrackColours();
            return colours;
        }
        static inline bool useBlueAccent = false;
        static inline AccentColours accentColours {};
    };
    inline void paintLengthGrip (juce::Graphics& g, juce::Rectangle<float> grip, juce::Colour dots = Palette::text, float opacity = 1.0f)
    {
        if (grip.isEmpty())
            return;

        grip = grip.withSizeKeepingCentre (juce::jmin (16.0f, grip.getWidth()), grip.getHeight());

        g.setColour (juce::Colours::black.withAlpha (0.18f * opacity));
        g.fillRoundedRectangle (grip.translated (0, 1.0f).expanded (0.5f), 4);
        juce::ColourGradient finish (Palette::shadeBackground (juce::Colour (0xff293034)), grip.getTopLeft(),
                                     Palette::shadeBackground (juce::Colour (0xff192024)), grip.getTopRight(), false);
        finish.addColour (0.18, Palette::shadeBackground (juce::Colour (0xff111719)));
        finish.addColour (0.48, Palette::shadeBackground (juce::Colour (0xff050708)));
        finish.addColour (0.82, Palette::shadeBackground (juce::Colour (0xff0a0e10)));
        finish.multiplyOpacity (opacity);
        g.setGradientFill (finish);
        g.fillRoundedRectangle (grip, 4);
        g.setColour (Palette::border.withAlpha (0.55f * opacity));
        g.drawRoundedRectangle (grip.reduced (0.5f), 3.5f, 1);
        g.setColour (Palette::text.withAlpha (0.16f * opacity));
        g.drawHorizontalLine (juce::roundToInt (grip.getY() + 1.5f), grip.getX() + 4, grip.getRight() - 4);

        const auto centre = grip.getCentre();
        const float spacing = juce::jmin (6.0f, grip.getHeight() / 5.0f);
        g.setColour (dots.withMultipliedAlpha (opacity));
        for (int dot = -2; dot <= 2; ++dot)
            g.fillEllipse (centre.x - 1.2f, centre.y + static_cast<float> (dot) * spacing - 1.2f, 2.4f, 2.4f);
    }
    inline constexpr float labelFontSize = 16.0f;
    inline constexpr float swingEyeArtworkHeight = 48.0f;

    inline juce::Font font (float height, bool bold = false)
    {
        static auto regular = juce::Typeface::createSystemTypefaceFor (BinaryData::GantariRegular_ttf, BinaryData::GantariRegular_ttfSize);
        static auto heavy = juce::Typeface::createSystemTypefaceFor (BinaryData::GantariSemibold_ttf, BinaryData::GantariSemibold_ttfSize);
        auto f = juce::Font (juce::FontOptions (bold ? heavy : regular).withHeight (height * 1.18f));
        return f;
    }
    inline int dropdownWidth (const juce::String& label)
    {
        return juce::jmax (juce::GlyphArrangement::getStringWidthInt (font (13), label),
                          juce::GlyphArrangement::getStringWidthInt (font (13, true), label)) + 40;
    }
    inline void text (juce::Graphics& g, const juce::String& value, juce::Rectangle<float> bounds, float size = 12.0f, juce::Colour colour = Palette::text, bool bold = false, juce::Justification align = juce::Justification::centred)
    {
        g.setColour (colour);
        g.setFont (font (size, bold));
        g.drawText (value, bounds, align, true);
    }
    inline void drawShortcutHint (juce::Graphics& g, const juce::String& hint, juce::Rectangle<float> bounds,
                                  int controlHeight, juce::Justification alignment, float opacity = 1.0f)
    {
        text (g, hint, bounds, controlHeight >= 60 ? 10.0f : 8.0f,
              Palette::text.withAlpha (0.62f * opacity), false, alignment);
    }

    /** tone maps every colour drawn, e.g. to fade the surface toward a known backdrop. */
    template <typename Tone>
    void surface (juce::Graphics& g, juce::Rectangle<float> r, bool active, bool hover, Tone&& tone, float radius = 5.0f,
                  const Palette::SurfaceColours& colours = Palette::backgrounds)
    {
        const auto top = colours.raised.interpolatedWith (colours.surface, 0.4f);
        const auto bottom = colours.app.interpolatedWith (colours.surface, 0.3f);
        g.setGradientFill (juce::ColourGradient (tone (hover ? top.brighter (0.08f) : top), r.getTopLeft(), tone (bottom), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (tone (active ? Palette::accent : colours.border.withAlpha (0.42f)));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (tone (Palette::text.withAlpha (0.025f)));
        g.drawLine (r.getX() + radius, r.getY() + 1, r.getRight() - radius, r.getY() + 1);
    }
    inline void surface (juce::Graphics& g, juce::Rectangle<float> r, bool active = false, bool hover = false)
    {
        surface (g, r, active, hover, [] (juce::Colour colour) { return colour; });
    }
    inline void controlSurface (juce::Graphics& g, juce::Rectangle<float> r, bool active = false, bool hover = false, float opacity = 1.0f)
    {
        surface (g, r, active, hover, [opacity] (juce::Colour colour) { return colour.withMultipliedAlpha (opacity); }, 5.0f, Palette::controls);
    }
    inline juce::Colour gridBackdropColour()
    {
        return Palette::shell.interpolatedWith (Palette::app, 0.28f);
    }
    inline void paintGridSlot (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour backdrop, bool beatStart, float radius = 5.0f)
    {
        surface (g, bounds, false, false, [backdrop, beatStart] (juce::Colour ink)
        {
            const auto base = ink.interpolatedWith (backdrop, 0.35f);
            return (beatStart ? base.darker (0.30f) : base).withAlpha (ink.getFloatAlpha());
        }, radius);
    }
    inline void paintGridHitFill (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour, int velocity, float radius = 5.0f)
    {
        const auto light = colour.brighter (0.1f);
        const auto bottom = colour.interpolatedWith (Palette::app, 0.1f);
        juce::ColourGradient fill (light, bounds.getTopLeft(), bottom, bounds.getBottomLeft(), false);
        if (velocity < 127)
        {
            // The unlit share of the cell becomes a hard-edged band above the velocity fill.
            const auto dark = colour.interpolatedWith (Palette::app, 0.48f);
            const double shade = 1.0 - velocity / 127.0;
            const double edge = juce::jmin (1.0, shade + 0.5 / juce::jmax (1.0, static_cast<double> (bounds.getHeight())));
            fill.clearColours();
            fill.addColour (0.0, dark);
            fill.addColour (shade, dark);
            fill.addColour (edge, light.interpolatedWith (bottom, static_cast<float> (edge)));
            fill.addColour (1.0, bottom);
        }
        g.setGradientFill (fill);
        g.fillRoundedRectangle (bounds, radius);
    }
    inline juce::Colour footerInk (bool active, bool enabled)
    {
        return (active ? Palette::accent : Palette::text).withMultipliedAlpha (enabled ? 1.0f : 0.4f);
    }
    inline constexpr float footerArtworkScale = 0.85f;
    inline juce::Rectangle<float> footerArtworkBounds (juce::Rectangle<float> bounds)
    {
        return bounds.reduced (5.0f);
    }
    inline juce::Rectangle<float> rowControlFace (juce::Rectangle<float> bounds)
    {
        const float room = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const float side = juce::jlimit (0.0f, 42.0f, room - 2.0f);
        return bounds.withSizeKeepingCentre (side, side);
    }
    inline void footerSurface (juce::Graphics& g, juce::Rectangle<float> bounds, bool active, bool hover, bool down, bool enabled)
    {
        const float alpha = enabled ? 1.0f : 0.4f;
        const auto base = hover && enabled ? Palette::controls.raised : Palette::controls.surface;
        g.setColour (base.interpolatedWith (Palette::accent, active ? (down ? 0.18f : 0.08f) : down ? 0.06f : 0.0f).withAlpha (alpha));
        g.fillRoundedRectangle (bounds, 5);
        const auto border = active ? Palette::accent.withAlpha (0.65f) : hover && enabled ? Palette::muted.withAlpha (0.85f) : Palette::controls.border.withAlpha (0.7f);
        g.setColour (border.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (bounds, 5, 1);
    }

    inline void paintPatternCopyOutline (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        bounds.reduce (1.5f, 1.5f);
        g.setColour (Palette::blueAccent.withAlpha (0.28f));
        g.drawRoundedRectangle (bounds, 4, 4);
        g.setColour (Palette::blueAccent.brighter (0.18f));
        g.drawRoundedRectangle (bounds, 4, 1.5f);
    }

    inline juce::Path gearGlyph()
    {
        juce::Path shape;
        const juce::Point<float> centre { 12, 12 };
        constexpr float pitch = juce::MathConstants<float>::twoPi / 8.0f;
        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const float angle = static_cast<float> (tooth) * pitch;
            const auto point = centre.getPointOnCircumference (7.5f, angle - 0.27f);
            if (tooth == 0)
                shape.startNewSubPath (point);
            else
                shape.lineTo (point);
            shape.lineTo (centre.getPointOnCircumference (10.0f, angle - 0.16f));
            shape.lineTo (centre.getPointOnCircumference (10.0f, angle + 0.16f));
            shape.lineTo (centre.getPointOnCircumference (7.5f, angle + 0.27f));
        }
        shape.closeSubPath();
        shape.addEllipse (8.5f, 8.5f, 7, 7);
        return shape;
    }

    namespace phoneMenu
    {
        enum Icon { none, folder, grid, timing, dice, gear, info, add, save, saveAs, previous, next,
                    trash, clock, lock, eye, palette, values, rows, keyboard, metronome, midi, link, xy };

        inline void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, juce::Colour ink)
        {
            juce::Path p;
            const auto line = [&p] (std::initializer_list<juce::Point<float>> points)
            {
                p.startNewSubPath (*points.begin());
                for (auto point = points.begin() + 1; point != points.end(); ++point)
                    p.lineTo (*point);
            };
            const auto dot = [&p] (float x, float y) { p.addEllipse (x - 0.6f, y - 0.6f, 1.2f, 1.2f); };

            switch (icon)
            {
                case folder: line ({ { 3, 19 }, { 3, 5 }, { 9, 5 }, { 12, 8 }, { 21, 8 }, { 21, 19 }, { 3, 19 } }); break;
                case grid:
                    for (float y : { 4.0f, 14.0f })
                        for (float x : { 4.0f, 14.0f })
                            p.addRoundedRectangle (x, y, 6, 6, 1);
                    break;
                case timing:
                    g.setColour (ink);
                    for (float x : { 4.0f, 14.0f, 20.0f })
                    {
                        g.fillEllipse (bounds.getX() + (x - 2.3f) * bounds.getWidth() / 24.0f,
                                       bounds.getY() + 14.5f * bounds.getHeight() / 24.0f,
                                       4.6f * bounds.getWidth() / 24.0f, 3.5f * bounds.getHeight() / 24.0f);
                        line ({ { x + 1.65f, 16 }, { x + 1.65f, 6 } });
                    }
                    break;
                case dice: p.addRoundedRectangle (4, 4, 16, 16, 3); dot (8, 16); dot (12, 12); dot (16, 8); break;
                case gear: p = gearGlyph(); break;
                case info: p.addEllipse (3, 3, 18, 18); dot (12, 8); line ({ { 12, 12 }, { 12, 16 } }); break;
                case add: line ({ { 5, 12 }, { 19, 12 } }); line ({ { 12, 5 }, { 12, 19 } }); break;
                case save:
                case saveAs:
                    line ({ { 4, 4 }, { 17, 4 }, { 20, 7 }, { 20, 20 }, { 4, 20 }, { 4, 4 } });
                    line ({ { 8, 4 }, { 8, 9 }, { 15, 9 }, { 15, 4 } });
                    line ({ { 8, 20 }, { 8, 14 }, { 16, 14 }, { 16, 20 } });
                    if (icon == saveAs)
                        line ({ { 2, 8 }, { 2, 22 }, { 16, 22 } });
                    break;
                case previous: line ({ { 15, 5 }, { 8, 12 }, { 15, 19 } }); break;
                case next: line ({ { 9, 5 }, { 16, 12 }, { 9, 19 } }); break;
                case trash:
                    line ({ { 4, 7 }, { 20, 7 } }); line ({ { 9, 7 }, { 9, 4 }, { 15, 4 }, { 15, 7 } });
                    line ({ { 6, 7 }, { 7, 20 }, { 17, 20 }, { 18, 7 } });
                    line ({ { 10, 10 }, { 10, 17 } }); line ({ { 14, 10 }, { 14, 17 } }); break;
                case clock: p.addEllipse (3, 3, 18, 18); line ({ { 12, 7 }, { 12, 12 }, { 16, 14 } }); break;
                case lock:
                    p.addRoundedRectangle (5, 10, 14, 10, 2);
                    p.startNewSubPath (8, 10); p.lineTo (8, 7); p.cubicTo (8, 2, 16, 2, 16, 7); p.lineTo (16, 10);
                    dot (12, 15); break;
                case eye:
                    p.startNewSubPath (2, 12); p.quadraticTo (12, 0, 22, 12); p.quadraticTo (12, 24, 2, 12);
                    p.addEllipse (9, 9, 6, 6); break;
                case palette:
                    p.addEllipse (3, 3, 18, 18); dot (8, 9); dot (14, 7); dot (17, 12); dot (9, 16); break;
                case values:
                    line ({ { 5, 19 }, { 5, 14 } }); line ({ { 10, 19 }, { 10, 10 } });
                    line ({ { 15, 19 }, { 15, 12 } }); line ({ { 20, 19 }, { 20, 5 } }); break;
                case rows:
                    line ({ { 3, 6 }, { 11, 6 } }); line ({ { 3, 12 }, { 11, 12 } }); line ({ { 3, 18 }, { 11, 18 } });
                    line ({ { 14, 15 }, { 17, 18 }, { 20, 15 } }); line ({ { 17, 6 }, { 17, 18 } }); break;
                case keyboard:
                    p.addRoundedRectangle (2, 5, 20, 14, 2);
                    for (float x : { 6.0f, 10.0f, 14.0f, 18.0f })
                        dot (x, 9);
                    line ({ { 7, 14 }, { 17, 14 } }); break;
                case metronome:
                    line ({ { 3, 20 }, { 8, 4 }, { 15, 4 }, { 21, 20 }, { 3, 20 } });
                    line ({ { 11, 16 }, { 20, 5 } }); dot (18, 8); break;
                case midi:
                    p.addEllipse (3, 3, 18, 18); dot (6.5f, 11); dot (9, 7.5f); dot (12, 7); dot (15, 7.5f); dot (17.5f, 11);
                    line ({ { 10, 17 }, { 14, 17 } }); break;
                case link:
                    p.addRoundedRectangle (2, 8, 12, 8, 4); p.addRoundedRectangle (10, 8, 12, 8, 4); break;
                case xy:
                    p.addRoundedRectangle (3, 3, 18, 18, 2);
                    line ({ { 8, 6 }, { 8, 16 }, { 18, 16 } });
                    p.addEllipse (12.5f, 7.5f, 4, 4);
                    break;
                case none: return;
            }
            g.setColour (ink);
            g.strokePath (p, juce::PathStrokeType (1.65f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::scale (bounds.getWidth() / 24.0f, bounds.getHeight() / 24.0f).translated (bounds.getX(), bounds.getY()));
        }

        inline void card (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title = {})
        {
            g.setColour (Palette::shell);
            g.fillRoundedRectangle (bounds, 8);
            g.setColour (Palette::border.withAlpha (0.52f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 8, 1);
            if (title.isNotEmpty())
                text (g, title, bounds.reduced (14, 0).withTrimmedTop (9).withHeight (26),
                      15, Palette::text, true, juce::Justification::centredLeft);
        }
    }

    class LookAndFeel final : public juce::LookAndFeel_V4
    {
        static constexpr int popupLabelInset = 14;
        static constexpr float footerHeaderFontSize = 13.0f;

        static bool isFooterMenu (const juce::PopupMenu::Options& options)
        {
            const auto* anchor = options.getTopLevelTargetComponent();
            return anchor != nullptr && static_cast<bool> (anchor->getProperties()["footerControl"]);
        }

    public:
        LookAndFeel()
        {
            setColour (juce::TextEditor::textColourId, Palette::text);
            setColour (juce::PopupMenu::textColourId, Palette::text);
            refreshPalette();
        }
        juce::MouseCursor getMouseCursorFor (juce::Component& component) override
        {
            using Cursor = juce::MouseCursor;
            if (!component.isEnabled())
                return Cursor::NormalCursor;

            const auto cursor = juce::LookAndFeel_V4::getMouseCursorFor (component);
            if (cursor != Cursor::NormalCursor && cursor != Cursor::ParentCursor)
                return cursor;

            if (dynamic_cast<juce::Button*> (&component) != nullptr || dynamic_cast<juce::ComboBox*> (&component) != nullptr)
                return Cursor::PointingHandCursor;

            if (const auto* slider = dynamic_cast<juce::Slider*> (&component))
                return slider->isHorizontal() ? Cursor::LeftRightResizeCursor : Cursor::UpDownResizeCursor;

            return cursor;
        }
        void refreshPalette()
        {
            setColour (juce::TextEditor::backgroundColourId, Palette::controls.app);
            setColour (juce::TextEditor::outlineColourId, Palette::controls.border);
            setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
            setColour (juce::TextEditor::highlightColourId, Palette::accent.withAlpha (0.3f));
            setColour (juce::PopupMenu::backgroundColourId, Palette::shell);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::controls.raised);
            setColour (juce::PopupMenu::highlightedTextColourId, Palette::accent);
        }
        void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
        {
            if (!editor.getProperties().contains ("projectMetadata"))
            {
                juce::LookAndFeel_V4::fillTextEditorBackground (g, width, height, editor);
                return;
            }
            g.setColour (Palette::controls.app);
            g.fillRoundedRectangle (editor.getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        }
        void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
        {
            if (!editor.getProperties().contains ("projectMetadata"))
            {
                juce::LookAndFeel_V4::drawTextEditorOutline (g, width, height, editor);
                return;
            }
            g.setColour (editor.hasKeyboardFocus (true) ? Palette::accent : Palette::controls.border);
            g.drawRoundedRectangle (editor.getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.0f);
        }
        juce::Font getPopupMenuFont() override { return font (labelFontSize); }
        void preparePopupMenuWindow (juce::Component& window) override
        {
            // Rounded corners must expose the content underneath, rather than
            // the white backing JUCE paints for an opaque menu window.
            window.setOpaque (false);
        }
        int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options& options) override { return isFooterMenu (options) ? popupLabelInset : 6; }
        void getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& label, int standardHeight, int& width, int& height,
                                                            const juce::PopupMenu::Options& options) override
        {
            if (!isFooterMenu (options))
            {
                juce::LookAndFeel_V4::getIdealPopupMenuSectionHeaderSizeWithOptions (label, standardHeight, width, height, options);
                return;
            }
            width = juce::GlyphArrangement::getStringWidthInt (font (footerHeaderFontSize, true), label) + 2 * popupLabelInset;
            height = 30;
        }
        void drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& label,
                                                    const juce::PopupMenu::Options& options) override
        {
            if (!isFooterMenu (options))
            {
                juce::LookAndFeel_V4::drawPopupMenuSectionHeaderWithOptions (g, area, label, options);
                return;
            }
            // JUCE already insets the header component horizontally by the menu border.
            text (g, label, area.toFloat(), footerHeaderFontSize, Palette::muted, true, juce::Justification::centredLeft);
        }
        void getIdealPopupMenuItemSizeWithOptions (const juce::String& label, bool separator, int standardHeight, int& width, int& height, const juce::PopupMenu::Options& options) override
        {
            width = juce::GlyphArrangement::getStringWidthInt (font (labelFontSize, !isFooterMenu (options)), label) + 40;
            height = separator ? 8 : standardHeight > 0 ? standardHeight : 30;
        }
        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            const auto bounds = juce::Rectangle<float> (0, 0, static_cast<float> (width), static_cast<float> (height)).reduced (0.5f);
            g.setColour (Palette::shell);
            g.fillRoundedRectangle (bounds, 7.0f);
            g.setColour (Palette::border.withAlpha (0.8f));
            g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        }
        void drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool highlighted, const juce::PopupMenu::Item& item, const juce::PopupMenu::Options& options) override
        {
            const auto bounds = area.toFloat().reduced (6.0f, 1.0f);
            if (highlighted && item.isEnabled)
            {
                g.setColour (Palette::controls.raised);
                g.fillRoundedRectangle (bounds, 4.0f);
            }
            else if (item.isTicked)
            {
                g.setColour (Palette::accent.withAlpha (0.08f));
                g.fillRoundedRectangle (bounds, 4.0f);
            }
            text (g, item.text, bounds.withLeft (static_cast<float> (area.getX() + popupLabelInset)).withTrimmedRight (20), labelFontSize,
                  !item.isEnabled ? Palette::faint : item.isTicked ? Palette::accent : Palette::text,
                  item.isTicked && !isFooterMenu (options), juce::Justification::centredLeft);
            if (item.isTicked)
            {
                const float x = bounds.getRight() - 14, y = bounds.getCentreY();
                juce::Path tick;
                tick.startNewSubPath (x, y);
                tick.lineTo (x + 3, y + 3);
                tick.lineTo (x + 9, y - 4);
                g.setColour (Palette::accent);
                g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        juce::Font getTextButtonFont (juce::TextButton&, int height) override { return font (juce::jmin (height >= 60 ? 18.0f : 14.0f, static_cast<float> (height) * 0.45f), true); }
        static void paintToolbarButton (juce::Graphics& g, const juce::Path& outline, juce::Rectangle<float> bounds,
                                        bool highlighted, bool active, float opacity = 1.0f)
        {
            const auto top = highlighted ? Palette::controls.raised.brighter (0.12f) : Palette::controls.raised;
            g.setGradientFill (juce::ColourGradient (top.withAlpha (opacity), bounds.getTopLeft(),
                                                   Palette::controls.app.withAlpha (opacity), bounds.getBottomLeft(), false));
            g.fillPath (outline);
            if (active)
            {
                g.setColour (Palette::accent.withAlpha (0.24f * opacity));
                g.fillPath (outline);
            }
            g.setColour ((active ? Palette::accent : Palette::controls.border).withAlpha (opacity));
            g.strokePath (outline, juce::PathStrokeType (1));
        }
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
        {
            hover = hover && b.isEnabled();
            down = down && b.isEnabled();
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            if (b.isEnabled() && static_cast<bool> (b.getProperties()["clearPrompt"]))
            {
                g.setColour (Palette::recording.withAlpha (0.18f));
                g.fillRoundedRectangle (r, 4);
                g.setColour (Palette::recording);
                g.drawRoundedRectangle (r, 4, 1);
                return;
            }
            if (b.getProperties().contains ("phoneMenuIcon"))
            {
                const bool selected = b.getToggleState();
                const bool tab = static_cast<bool> (b.getProperties()["phoneMenuTab"]);
                const float activity = static_cast<float> (b.getProperties()["midiActivity"]);
                const auto base = hover || down ? Palette::controls.raised : Palette::controls.surface;
                g.setColour ((tab && !selected && !hover && !down ? Palette::controls.shell : base)
                                 .interpolatedWith (Palette::accent, selected ? 0.13f : activity * 0.18f)
                                 .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.55f));
                g.fillRoundedRectangle (r, 6);
                g.setColour ((selected || down || b.hasKeyboardFocus (false) ? Palette::accent : Palette::controls.border)
                                 .withAlpha (selected ? 0.85f : 0.55f));
                g.drawRoundedRectangle (r, 6, 1);
                return;
            }
            if (b.getProperties()["icon"].toString() == "eye")
                return;
            if (static_cast<bool> (b.getProperties()["rowNumber"]))
            {
                if (b.isEnabled() && (hover || down || b.hasKeyboardFocus (false)))
                {
                    g.setColour (b.findColour (juce::TextButton::textColourOffId).withAlpha (down ? 0.24f : 0.10f));
                    g.fillRoundedRectangle (r.reduced (1.0f, r.getHeight() * 0.18f), 5);
                }
                return;
            }
            const bool insetProjectArtwork = static_cast<bool> (b.getProperties()["insetProjectArtwork"]);
            if (insetProjectArtwork || static_cast<bool> (b.getProperties()["footerControl"]))
            {
                if (insetProjectArtwork || static_cast<bool> (b.getProperties()["insetFooterArtwork"]))
                    r = footerArtworkBounds (r);
                footerSurface (g, r, b.getToggleState(), hover || b.hasKeyboardFocus (true), down, b.isEnabled());
                const auto activity = static_cast<float> (b.getProperties()["midiActivity"]);
                if (activity > 0.0f)
                {
                    g.setColour (Palette::accent.withAlpha (activity * 0.18f));
                    g.fillRoundedRectangle (r.reduced (1.0f), 4);
                }
                return;
            }
            if (b.getProperties().contains ("dropdown"))
            {
                controlSurface (g, r, down || b.getToggleState(), hover);
                return;
            }
            if (b.getProperties().contains ("transport"))
            {
                controlSurface (g, r, down, b.isEnabled() && (hover || b.hasKeyboardFocus (false)));
                if (static_cast<bool> (b.getProperties()["launchPending"]))
                {
                    const auto phase = juce::Time::getMillisecondCounter() % 600u;
                    g.setColour (Palette::accent.withAlpha (phase < 300u ? 0.28f : 0.10f));
                    g.fillRoundedRectangle (r.reduced (1.0f), 5);
                }
                return;
            }
            const bool stepPage = b.getProperties().contains ("stepPage");
            if (stepPage || b.getProperties().contains ("selectMode") || b.getProperties().contains ("layerSegment") || b.getProperties().contains ("historySegment"))
            {
                const bool history = b.getProperties().contains ("historySegment");
                const bool first = history ? static_cast<int> (b.getProperties()["historySegment"]) == 0
                                           : b.getProperties().contains ("layerSegment") && static_cast<int> (b.getProperties()["layerSegment"]) == 0;
                const bool last = history ? static_cast<int> (b.getProperties()["historySegment"]) == 1
                                          : b.getProperties().contains ("selectMode");
                juce::Path outline;
                outline.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 7, 7,
                                             first || stepPage, last || stepPage, first || stepPage, last || stepPage);
                paintToolbarButton (g, outline, r, hover || down, b.getToggleState());
                return;
            }
            const auto icon = b.getProperties()["icon"].toString();
            if (static_cast<bool> (b.getProperties()["gridToolbarAction"]))
            {
                juce::Path outline;
                outline.addRoundedRectangle (r, 4);
                const bool padsOpen = icon == "pads" && b.getToggleState();
                paintToolbarButton (g, outline, r, hover || down, padsOpen || down, b.isEnabled() ? 1.0f : 0.4f);
                return;
            }
            if (icon.isNotEmpty() && icon != "keyboardPrevious" && icon != "keyboardNext")
            {
                if (static_cast<bool> (b.getProperties()["outlinedIcon"]))
                {
                    const bool highlighted = b.isEnabled() && (hover || down);
                    const float alpha = b.isEnabled() ? 1.0f : 0.4f;
                    g.setGradientFill (juce::ColourGradient ((highlighted ? Palette::controls.raised.brighter (0.3f) : Palette::controls.raised).withAlpha (alpha), r.getTopLeft(),
                                                           (highlighted ? Palette::controls.surface.brighter (0.18f) : Palette::controls.app).withAlpha (alpha), r.getBottomLeft(), false));
                    g.fillRoundedRectangle (r, 4);
                    const bool activeToggle = b.getToggleState() && icon == "pads";
                    if (activeToggle)
                    {
                        g.setColour (Palette::accent.withAlpha (0.10f * alpha));
                        g.fillRoundedRectangle (r, 4);
                    }
                    g.setColour (((down && b.isEnabled()) || activeToggle ? Palette::accent : highlighted ? Palette::muted : Palette::controls.border).withAlpha (alpha));
                    g.drawRoundedRectangle (r, 4, 1);
                    return;
                }
                if (icon == "recordLock")
                    return;
                if (hover || down)
                {
                    g.setColour (Palette::controls.raised);
                    g.fillRoundedRectangle (r, 4);
                }
                return;
            }
            if (b.getToggleState() || down)
            {
                g.setColour (Palette::accent);
                g.fillRoundedRectangle (r, 4);
            }
            else
            {
                g.setColour (hover ? Palette::controls.raised.brighter (0.13f) : Palette::controls.raised);
                g.fillRoundedRectangle (r, 4);
            }
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
        {
            if (b.getProperties().contains ("phoneMenuIcon"))
            {
                const auto icon = static_cast<phoneMenu::Icon> (static_cast<int> (b.getProperties()["phoneMenuIcon"]));
                const auto custom = b.getProperties()["phoneMenuLabel"].toString();
                const auto label = custom.isEmpty() ? b.getButtonText() : custom;
                const bool tab = static_cast<bool> (b.getProperties()["phoneMenuTab"]);
                const bool selected = b.getToggleState() || down;
                const auto ink = !b.isEnabled() ? Palette::faint : selected ? Palette::accent
                                          : icon == phoneMenu::trash ? Palette::danger : Palette::text;
                auto bounds = b.getLocalBounds().toFloat().reduced (12, 0);
                if (tab)
                    bounds = bounds.withSizeKeepingCentre (juce::GlyphArrangement::getStringWidth (font (14, true), label) + 26, bounds.getHeight());
                const auto iconBounds = bounds.removeFromLeft (18).withSizeKeepingCentre (18, 18);
                phoneMenu::drawIcon (g, icon, iconBounds, selected || !tab ? ink : Palette::muted);
                text (g, label, bounds.withTrimmedLeft (8), 14, ink, true, juce::Justification::centredLeft);
                return;
            }
            if (static_cast<bool> (b.getProperties()["rowNumber"]))
            {
                text (g, b.getButtonText(), b.getLocalBounds().toFloat(), b.getWidth() >= 40 ? 22.0f : 14.0f,
                      b.findColour (juce::TextButton::textColourOffId).withAlpha (b.isEnabled() ? 1.0f : 0.4f), true);
                return;
            }
            if (static_cast<bool> (b.getProperties()["patternNumber"]) || static_cast<bool> (b.getProperties()["patternNumberFont"]))
            {
                static const auto typeface = juce::Typeface::createSystemTypefaceFor (BinaryData::GantariExtrabold_ttf, BinaryData::GantariExtrabold_ttfSize);
                g.setColour (juce::Colours::white);
                g.setFont (juce::Font (juce::FontOptions (typeface).withHeight (b.getHeight() >= 60 ? 23.0f : 15.0f * 1.18f)));
                g.drawText (b.getButtonText(), b.getLocalBounds().toFloat(), juce::Justification::centred, true);
                return;
            }
            if (b.getProperties().contains ("dropdown"))
            {
                const auto bounds = b.getLocalBounds().toFloat();
                const bool patternNumber = static_cast<bool> (b.getProperties()["patternBankNumber"]);
                const auto label = patternNumber ? bounds.withTrimmedLeft (6).withTrimmedRight (22)
                                                 : bounds.withTrimmedLeft (14).withTrimmedRight (26);
                const float fontSize = patternNumber ? 20.5f : (b.getHeight() >= 60 ? 18.0f : 13.0f);
                text (g, b.getButtonText(), label, fontSize, b.isEnabled() ? Palette::text : Palette::faint,
                      true, patternNumber ? juce::Justification::centred : juce::Justification::centredLeft);
                const float x = bounds.getRight() - 14, y = bounds.getCentreY();
                const float direction = b.getToggleState() ? -1.0f : 1.0f;
                juce::Path chevron;
                chevron.startNewSubPath (x - 4, y - direction * 2);
                chevron.lineTo (x, y + direction * 2);
                chevron.lineTo (x + 4, y - direction * 2);
                g.setColour (b.isEnabled() ? Palette::text : Palette::faint);
                g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            const auto icon = b.getProperties()["icon"].toString();
            const bool footerControl = static_cast<bool> (b.getProperties()["footerControl"]);
            const bool insetFooterArtwork = static_cast<bool> (b.getProperties()["insetFooterArtwork"]);
            const bool stepPage = b.getProperties().contains ("stepPage");
            if (stepPage || b.getProperties().contains ("backToGrid"))
            {
                const auto ink = b.isEnabled() ? Palette::text : Palette::faint;
                const float fontSize = stepPage ? 11.0f : 18.0f;
                const float width = juce::GlyphArrangement::getStringWidth (font (fontSize, true), b.getButtonText());
                const float iconWidth = stepPage ? 8.0f : 26.0f;
                auto content = b.getLocalBounds().toFloat().withSizeKeepingCentre (width + iconWidth, static_cast<float> (b.getHeight()));
                const auto centre = (stepPage ? content.removeFromRight (iconWidth) : content.removeFromLeft (iconWidth)).getCentre();
                const float direction = stepPage ? 1.0f : -1.0f;
                const float halfHeight = stepPage ? 3.0f : 5.0f;
                const float halfWidth = halfHeight * 0.5f;
                juce::Path chevron;
                chevron.startNewSubPath (centre.x - direction * halfWidth, centre.y - halfHeight);
                chevron.lineTo (centre.x + direction * halfWidth, centre.y);
                chevron.lineTo (centre.x - direction * halfWidth, centre.y + halfHeight);
                g.setColour (ink);
                g.strokePath (chevron, juce::PathStrokeType (stepPage ? 1.5f : 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                text (g, b.getButtonText(), content, fontSize, ink, true);
                return;
            }
            if (icon.isNotEmpty())
            {
                const bool highlighted = b.getToggleState() || down;
                g.setColour (!b.isEnabled() ? Palette::faint.withAlpha (0.4f) : highlighted ? Palette::accent
                                                                                                           : Palette::text);
                if (footerControl)
                    g.setColour (footerInk (b.getToggleState(), b.isEnabled()));
                auto r = b.getLocalBounds().toFloat();
                if (icon == "eye")
                    r = r.withHeight (juce::jmin (r.getHeight(), swingEyeArtworkHeight));
                const bool historyIcon = icon == "undo" || icon == "redo";
                const bool outlinedIcon = static_cast<bool> (b.getProperties()["outlinedIcon"]);
                const bool patternEditIcon = outlinedIcon && (icon == "clear" || icon == "previous" || icon == "next");
                const bool rowClear = static_cast<bool> (b.getProperties()["rowClear"]);
                const float iconSize = insetFooterArtwork ? 30.0f
                                  : rowClear ? juce::jmin (20.0f, r.getHeight() * 0.65f)
                                  : icon == "eye" ? 20.0f
                                  : icon == "gear" ? 24.0f
                                  : icon == "recordLock" ? juce::jmin (40.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.9f)
                                  : b.getHeight() >= 60 ? (historyIcon ? 32.0f : 34.0f)
                                  : footerControl ? 22.0f
                                  : patternEditIcon ? juce::jmin (18.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.6f)
                                  : outlinedIcon ? juce::jmin (24.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.8f)
                                  : historyIcon ? juce::jmin (24.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.65f)
                                                : juce::jmin (30.0f, r.getWidth() * 0.84f);
                const float size = insetFooterArtwork ? iconSize * footerArtworkScale : iconSize;
                const float clearOffset = rowClear && r.getWidth() >= 40.0f ? 2.0f : 0.0f;
                g.addTransform (juce::AffineTransform::scale (size / 24).translated (r.getCentreX() - size / 2 + clearOffset, r.getCentreY() - size / 2));
                juce::Path path;
                if (icon == "eye")
                {
                    g.setColour (b.getToggleState() ? Palette::text : Palette::muted);
                    path.startNewSubPath (2, 12);
                    path.quadraticTo (12, 1, 22, 12);
                    path.quadraticTo (12, 23, 2, 12);
                    path.closeSubPath();
                    g.strokePath (path, juce::PathStrokeType (1.6f));
                    g.fillEllipse (9.5f, 9.5f, 5.0f, 5.0f);
                    if (!b.getToggleState())
                    {
                        g.setColour (Palette::controls.app);
                        g.drawLine (5, 20, 19, 4, 3.8f);
                        g.setColour (Palette::muted);
                        g.drawLine (5, 20, 19, 4, 1.6f);
                    }
                    return;
                }
                if (icon == "play")
                {
                    g.setColour (Palette::play);
                    path.addTriangle (7, 4.5f, 7, 19.5f, 20, 12);
                    g.fillPath (path);
                    return;
                }
                if (icon == "stop")
                {
                    g.setColour (Palette::text.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
                    g.fillRoundedRectangle (6, 6, 12, 12, 1.5f);
                    return;
                }
                if (icon == "clear")
                {
                    g.fillPath (clearGlyph());
                    return;
                }
                if (icon == "close")
                {
                    g.drawEllipse (2.5f, 2.5f, 19.0f, 19.0f, 1.5f);
                    g.fillPath (clearGlyph(), juce::AffineTransform::scale (0.52f, 0.52f, 12.0f, 12.0f));
                    return;
                }
                if (icon == "pads")
                {
                    for (int y = 0; y < 2; ++y)
                        for (int x = 0; x < 2; ++x)
                            g.fillRoundedRectangle (3.0f + 10.0f * static_cast<float> (x), 3.0f + 10.0f * static_cast<float> (y), 8.0f, 8.0f, 2.0f);
                    return;
                }
                if (icon == "metronome")
                {
                    path.startNewSubPath (8, 4);
                    path.lineTo (15, 4);
                    path.lineTo (20, 21);
                    path.lineTo (4, 21);
                    path.closeSubPath();
                    path.startNewSubPath (11, 17);
                    path.lineTo (19, 6);
                    g.strokePath (path, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.fillEllipse (17.0f, 4.0f, 4.0f, 4.0f);
                    return;
                }
                else if (icon == "record")
                {
                    const float pulse = b.getToggleState() && b.getProperties().contains ("recordPulse")
                                            ? static_cast<float> (b.getProperties()["recordPulse"]) : 1.0f;
                    const auto colour = b.getToggleState() ? Palette::recording : Palette::text;
                    g.setColour (colour.withAlpha (b.isEnabled() ? pulse : 0.3f));
                    g.fillEllipse (4, 4, 16, 16);
                    return;
                }
                else if (icon == "recordLock")
                {
                    const bool locked = b.getToggleState();
                    g.setColour (!b.isEnabled() ? Palette::faint : locked ? Palette::accent : Palette::text);
                    g.fillPath (recordLockShape (locked));
                    g.setColour (Palette::controls.app);
                    g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
                    g.drawText ("Q", juce::Rectangle<int> (6, 11, 12, 10), juce::Justification::centred);
                    return;
                }
                else if (icon == "projectPrevious" || icon == "projectNext" || icon == "keyboardPrevious" || icon == "keyboardNext")
                {
                    const bool next = icon == "projectNext" || icon == "keyboardNext";
                    const float tail = next ? 9.0f : 15.0f;
                    path.startNewSubPath (tail, 6);
                    path.lineTo (next ? 15.0f : 9.0f, 12);
                    path.lineTo (tail, 18);
                    g.strokePath (path, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    return;
                }
                else if (icon == "previous" || icon == "next")
                {
                    const bool next = icon == "next";
                    const float base = outlinedIcon ? (next ? 4.0f : 20.0f) : (next ? 6.0f : 18.0f);
                    path.addTriangle (base, 4, base, 20, next ? 20.0f : 4.0f, 12);
                    g.fillPath (path);
                    return;
                }
                else if (icon == "modifiers")
                {
                    path.addRoundedRectangle (3, 4, 18, 16, 3);
                    path.startNewSubPath (12, 7.5f);
                    path.lineTo (16, 12);
                    path.lineTo (14, 12);
                    path.lineTo (14, 16.5f);
                    path.lineTo (10, 16.5f);
                    path.lineTo (10, 12);
                    path.lineTo (8, 12);
                    path.closeSubPath();
                }
                else if (icon == "gear")
                {
                    path = gearGlyph();
                }
                else
                {
                    path.startNewSubPath (6, 7);
                    path.cubicTo (9, 3, 15, 3, 18, 7);
                    path.cubicTo (22, 12, 18, 20, 12, 20);
                    path.cubicTo (9, 20, 7, 19, 5.5f, 17);
                    path.startNewSubPath (6, 2.5f);
                    path.lineTo (6, 7);
                    path.lineTo (10.5f, 7);
                    const auto centre = path.getBounds().getCentre();
                    path.applyTransform (juce::AffineTransform::translation (12 - centre.x, 12 - centre.y));
                    if (icon == "redo")
                        path.applyTransform (juce::AffineTransform::scale (-1, 1).translated (24, 0));
                }
                g.strokePath (path, juce::PathStrokeType (footerControl ? 1.8f : historyIcon ? 2.4f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            const bool insetProjectArtwork = static_cast<bool> (b.getProperties()["insetProjectArtwork"]);
            if (footerControl || insetProjectArtwork)
            {
                const auto bounds = b.getLocalBounds().toFloat();
                const auto ink = footerInk (b.getToggleState(), b.isEnabled());
                const float fontSize = b.getHeight() >= 60 ? 18.0f : 12.0f;
                if (insetFooterArtwork || insetProjectArtwork)
                    text (g, b.getButtonText(), footerArtworkBounds (bounds), fontSize * footerArtworkScale, ink, true);
                else
                    text (g, b.getButtonText(), bounds, fontSize, ink, true);
                return;
            }
            if (b.getProperties().contains ("selectMode") || b.getProperties().contains ("layerSegment"))
            {
                const auto bounds = b.getLocalBounds().toFloat();
                text (g, b.getButtonText(), bounds, b.getHeight() >= 60 ? 18.0f : 14.0f, Palette::text, true);
                const auto hint = b.getProperties()["shortcutHint"].toString();
                if (hint.isNotEmpty())
                    drawShortcutHint (g, hint, bounds.reduced (5, 3), b.getHeight(), juce::Justification::bottomRight);
                return;
            }

            if (b.getProperties().contains ("transport"))
            {
                const auto ink = b.isEnabled() ? (down ? Palette::accent : Palette::text) : Palette::faint;
                text (g, b.getButtonText(), b.getLocalBounds().toFloat(), 12.0f, ink, true);
                return;
            }
            const auto bounds = b.getLocalBounds().toFloat();
            const float fontSize = b.getButtonText() == "M" ? 30.0f : b.getHeight() >= 60 ? 18.0f : 12.0f;
            text (g, b.getButtonText(), bounds, fontSize,
                  !b.isEnabled() ? Palette::faint : (b.getToggleState() || down ? Palette::controls.app : Palette::text), true);
        }
    };

    /** One hover-help destination for every TooltipClient in an editor's tree. */
    class FooterHelp : public juce::Label
    {
    public:
        explicit FooterHelp (juce::Component& editor) : owner (editor)
        {
            setTitle ("Control help");
            setFont (ksh::ui::font (18));
            setColour (juce::Label::textColourId, juce::Colours::white);
            setJustificationType (juce::Justification::centredLeft);
            setBorderSize (juce::BorderSize<int> (0));
            setMinimumHorizontalScale (1.0f);
            setInterceptsMouseClicks (false, false);
            owner.addMouseListener (this, true);
        }
        ~FooterHelp() override { owner.removeMouseListener (this); }
        void setHelpEnabled (bool enabled)
        {
            helpEnabled = enabled;
            if (!enabled)
                setText ({}, juce::dontSendNotification);
        }
        bool isHelpEnabled() const { return helpEnabled; }
        void showHelpFor (juce::Component* component)
        {
            juce::String help;
            if (helpEnabled && component != nullptr && owner.isParentOf (component))
            {
                const bool disabled = !component->isEnabled();
                for (auto* target = component; target != &owner; target = target->getParentComponent())
                {
                    if (disabled && static_cast<bool> (target->getProperties()["hideDisabledHelp"]))
                    {
                        help.clear();
                        break;
                    }
                    if (help.isEmpty())
                        if (auto* client = dynamic_cast<juce::TooltipClient*> (target))
                            help = client->getTooltip();
                    if (!disabled && help.isNotEmpty())
                        break;
                }
            }
            setText (help, juce::dontSendNotification);
        }
        void refresh() { showHelpFor (juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse()); }
        void mouseEnter (const juce::MouseEvent& e) override { mouseMove (e); }
        void mouseMove (const juce::MouseEvent& e) override { if (e.source.canHover()) showHelpFor (e.eventComponent); }
        void mouseDown (const juce::MouseEvent& e) override { showHelpFor (e.eventComponent); }
        void mouseExit (const juce::MouseEvent& e) override { showHelpFor (e.source.canHover() ? e.source.getComponentUnderMouse() : nullptr); }

    private:
        juce::Component& owner;
        bool helpEnabled = true;
    };

    class SettingsButton : public juce::TextButton, private juce::Timer
    {
    public:
        std::function<void()> showSettings;
        bool openSettingsOnHold = false;
        void mouseDown (const juce::MouseEvent& event) override
        {
            stopTimer();
            settingsOpened = false;
            if (!isEnabled())
                return;
            if (event.mods.isPopupMenu() && showSettings)
            {
                openSettings();
                return;
            }
            juce::TextButton::mouseDown (event);
            if (openSettingsOnHold && showSettings && event.mods.isLeftButtonDown())
                startTimer (450);
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (event.getDistanceFromDragStart() > 6 || !getLocalBounds().toFloat().contains (event.position))
                stopTimer();
            if (!settingsOpened)
                juce::TextButton::mouseDrag (event);
        }
        void mouseUp (const juce::MouseEvent& event) override
        {
            stopTimer();
            if (settingsOpened)
                setState (buttonNormal);
            juce::TextButton::mouseUp (event);
            settingsOpened = false;
        }
        void visibilityChanged() override
        {
            if (!isVisible())
                stopTimer();
            juce::TextButton::visibilityChanged();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (isEnabled() && key.getKeyCode() == juce::KeyPress::F10Key && key.getModifiers().isShiftDown() && showSettings)
            {
                showSettings();
                return true;
            }
            return juce::TextButton::keyPressed (key);
        }
    private:
        bool settingsOpened = false;
        void openSettings()
        {
            settingsOpened = true;
            setState (buttonNormal);
            showSettings();
        }
        void timerCallback() override
        {
            stopTimer();
            if (isEnabled() && isShowing() && isDown() && showSettings)
                openSettings();
        }
    };

    inline const juce::MouseCursor& horizontalResizeCursor()
    {
        static const auto cursor = []
        {
            juce::Image image (juce::Image::ARGB, 36, 24, true);
            juce::Graphics g (image);
            juce::Path arrows;
            arrows.startNewSubPath (6, 12);
            arrows.lineTo (30, 12);
            arrows.startNewSubPath (12, 6);
            arrows.lineTo (6, 12);
            arrows.lineTo (12, 18);
            arrows.startNewSubPath (24, 6);
            arrows.lineTo (30, 12);
            arrows.lineTo (24, 18);
            g.setColour (juce::Colours::black);
            g.strokePath (arrows, juce::PathStrokeType (6, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colours::white);
            g.strokePath (arrows, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return juce::MouseCursor (image, 9, 6, 2.0f);
        }();
        return cursor;
    }

    class ClearFlash final : private juce::Timer
    {
    public:
        explicit ClearFlash (juce::TextButton& target) : button (target) {}

        bool confirm()
        {
            if (isTimerRunning() && juce::Time::getMillisecondCounterHiRes() - startedAt < durationMs)
            {
                cancel();
                return true;
            }
            start();
            return false;
        }

        void start()
        {
            startedAt = juce::Time::getMillisecondCounterHiRes();
            setHighlight (true);
            startTimer (85);
        }

        void cancel()
        {
            stopTimer();
            setHighlight (false);
        }

    private:
        void timerCallback() override
        {
            const double elapsed = juce::Time::getMillisecondCounterHiRes() - startedAt;
            if (elapsed >= durationMs)
                cancel();
            else
                setHighlight (static_cast<int> (elapsed / 85.0) % 2 == 0);
        }

        void setHighlight (bool active)
        {
            if (button.getProperties().set ("clearPrompt", active))
                button.repaint();
        }

        static constexpr double durationMs = 350.0;
        juce::TextButton& button;
        double startedAt = 0.0;
    };

    /** Buttons with continuous gestures still use the native button accessibility handler. */
    class GestureButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        std::function<void (const juce::MouseEvent&)> pressed, dragged, released;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (pressed)
                pressed (e);
            else
                juce::TextButton::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragged)
                dragged (e);
            else
                juce::TextButton::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (released)
                released (e);
            else
                juce::TextButton::mouseUp (e);
        }
    };
    class RowPlaybackButton : public GestureButton
    {
    public:
        explicit RowPlaybackButton (const juce::String& label) : GestureButton (label)
        {
            setMouseClickGrabsKeyboardFocus (false);
        }

        void paintButton (juce::Graphics& g, bool hover, bool down) override
        {
            const auto face = rowControlFace (getLocalBounds().toFloat());
            if (face.isEmpty())
                return;
            const bool enabled = isEnabled();
            const bool solo = enabled && static_cast<bool> (getProperties()["soloActive"]);
            const bool lit = enabled && (getToggleState() || solo);
            controlSurface (g, face.reduced (0.5f), false, enabled && (hover || down), enabled ? 1.0f : 0.4f);
            if (enabled && hasKeyboardFocus (false))
            {
                g.setColour (Palette::muted.withAlpha (0.75f));
                g.drawRoundedRectangle (face.reduced (1.0f), 5, 1);
            }
            juce::Path lineArt;
            if (solo)
            {
                juce::GlyphArrangement glyph;
                glyph.addFittedText (font (juce::jmin (20.0f, face.getHeight() * 0.6f), true), "S", face.getX(), face.getY(),
                                     face.getWidth(), face.getHeight(), juce::Justification::centred, 1);
                glyph.createPath (lineArt);
            }
            else
            {
                const auto path = iconPath (getProperties()["icon"].toString());
                juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (lineArt, path);
                const float size = face.getWidth() * 0.62f;
                lineArt.applyTransform (juce::AffineTransform::scale (size / 24.0f).translated (face.getCentreX() - size / 2.0f,
                                                                                                face.getCentreY() - size / 2.0f));
            }
            if (lit)
            {
                glyphGlow.setColor (Palette::accent.withAlpha (0.72f));
                glyphGlow.render (g, lineArt);
            }
            g.setColour (!enabled ? Palette::faint.withAlpha (0.4f) : lit ? Palette::accent : Palette::text);
            g.fillPath (lineArt);
        }

    private:
        static juce::Path iconPath (const juce::String& icon)
        {
            juce::Path path;
            if (icon == "power")
            {
                path.addCentredArc (12, 12, 8, 8, 0, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
                path.startNewSubPath (12, 2);
                path.lineTo (12, 11);
            }
            else if (icon == "ping")
            {
                path.startNewSubPath (4, 11);
                path.lineTo (4, 10);
                path.quadraticTo (4, 7, 7, 7);
                path.lineTo (20, 7);
                path.startNewSubPath (17, 4);
                path.lineTo (20, 7);
                path.lineTo (17, 10);
                path.startNewSubPath (20, 13);
                path.lineTo (20, 14);
                path.quadraticTo (20, 17, 17, 17);
                path.lineTo (4, 17);
                path.startNewSubPath (7, 14);
                path.lineTo (4, 17);
                path.lineTo (7, 20);
            }
            else
            {
                path.startNewSubPath (4, 12);
                path.lineTo (20, 12);
                path.startNewSubPath (15.5f, 7.5f);
                path.lineTo (20, 12);
                path.lineTo (15.5f, 16.5f);
                if (icon == "reverse")
                    path.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi, 12, 12));
            }
            return path;
        }

        melatonin::DropShadow glyphGlow { juce::Colours::transparentBlack, 3 };
    };

    class PadVelocitySwitch : public juce::TextButton
    {
    public:
        PadVelocitySwitch() : juce::TextButton ("VEL")
        {
            setClickingTogglesState (true);
            setMouseClickGrabsKeyboardFocus (false);
        }

        void mouseDown (const juce::MouseEvent& event) override
        {
            if (event.mods.isLeftButtonDown() && !event.mods.isPopupMenu())
                juce::TextButton::mouseDown (event);
        }

        void paintButton (juce::Graphics& g, bool hover, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
            if (bounds.isEmpty())
                return;

            const float scale = juce::jmin (1.0f, bounds.getWidth() / 38.0f, bounds.getHeight() / 62.0f);
            const auto bezel = bounds.withSizeKeepingCentre (38.0f * scale, 62.0f * scale);
            const auto face = bezel.reduced (3.0f * scale);
            const bool enabled = isEnabled();
            const bool lit = enabled && getToggleState();
            const float alpha = enabled ? 1.0f : 0.4f;
            g.setColour (Palette::controls.app.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (bezel, 6.0f * scale);
            g.setColour ((hasKeyboardFocus (false) ? Palette::muted : Palette::controls.border).withMultipliedAlpha (0.75f * alpha));
            g.drawRoundedRectangle (bezel.reduced (0.5f), 6.0f * scale, 1.0f);
            g.setColour (Palette::controls.shell.interpolatedWith (Palette::controls.raised, enabled && (hover || down) ? 0.55f : 0.22f).withAlpha (alpha));
            g.fillRoundedRectangle (face, 4.0f * scale);

            const auto lamp = face.withSizeKeepingCentre (11.0f * scale, 11.0f * scale).translated (0, -10.0f * scale);
            g.setColour (Palette::shadeControl (juce::Colours::black).withAlpha (0.85f * alpha));
            g.fillEllipse (lamp.expanded (2.5f * scale));
            if (lit)
            {
                juce::Path lens;
                lens.addEllipse (lamp);
                lampGlow.setColor (Palette::accent.withAlpha (0.85f));
                lampGlow.render (g, lens);
            }
            const auto lensColour = lit ? Palette::accent : Palette::controls.border.darker (0.55f);
            g.setGradientFill (juce::ColourGradient (lensColour.brighter (lit ? 0.45f : 0.15f).withAlpha (alpha), lamp.getTopLeft(),
                                                     lensColour.withAlpha (alpha), lamp.getBottomRight(), false));
            g.fillEllipse (lamp);
            const auto label = face.withTop (face.getCentreY() + 2.0f * scale).withTrimmedBottom (3.0f * scale);
            ksh::ui::text (g, getButtonText(), label, juce::jmax (8.0f, 12.0f * scale), Palette::muted.withAlpha (alpha), true);
        }

    private:
        melatonin::DropShadow lampGlow { juce::Colours::transparentBlack, 7 };
    };
    /** A one-shot performance pad, also used for track number buttons. */
    class RowTriggerButton : public juce::TextButton
    {
    public:
        RowTriggerButton()
        {
            setTriggeredOnMouseDown (true);
            setMouseClickGrabsKeyboardFocus (false);
        }
        bool positionSensitive = false;
        bool allowColourPickerHold = false;
        std::function<void()> showColourPicker;
        int pressedVelocity() const { return velocity; }
        void mouseDown (const juce::MouseEvent& event) override
        {
            cancelColourHold();
            if (!isEnabled())
                return;
            if (event.mods.isPopupMenu())
            {
                if (showColourPicker)
                    showColourPicker();
                return;
            }
            if (!event.mods.isLeftButtonDown())
                return;
            if (allowColourPickerHold && showColourPicker && !event.source.isMouse())
            {
                colourHoldPending = true;
                colourHoldTimer.startTimer (500);
                return;
            }
            const float height = static_cast<float> (juce::jmax (1, getHeight() - 1));
            const int hitVelocity = positionSensitive ? 1 + juce::roundToInt (126.0f * juce::jlimit (0.0f, 1.0f, 1.0f - event.position.y / height)) : CellDefaults::velocity;
            const juce::ScopedValueSetter<int> pressed (velocity, hitVelocity);
            juce::TextButton::mouseDown (event);
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (colourHoldPending && event.getDistanceFromDragStart() > 10)
                cancelColourHold();
            juce::TextButton::mouseDrag (event);
        }
        void mouseUp (const juce::MouseEvent& event) override
        {
            const bool tap = colourHoldPending && isEnabled() && getLocalBounds().contains (event.getPosition());
            cancelColourHold();
            juce::TextButton::mouseUp (event);
            if (tap && onClick)
                onClick();
        }
        void visibilityChanged() override
        {
            if (!isShowing())
                cancelColourHold();
            juce::TextButton::visibilityChanged();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (isEnabled() && key.isKeyCode (juce::KeyPress::F10Key) && key.getModifiers().isShiftDown() && showColourPicker)
            {
                showColourPicker();
                return true;
            }
            return juce::TextButton::keyPressed (key);
        }
        void setAppearance (juce::Colour nextColour, const juce::String& keyboard, const juce::String& note)
        {
            if (colour == nextColour && keyLabel == keyboard && noteLabel == note)
                return;
            colour = nextColour;
            keyLabel = keyboard;
            noteLabel = note;
            repaint();
        }
        void flash (int hitVelocity)
        {
            intensity = static_cast<float> (hitVelocity) / 127.0f;
            flashUntilMs = juce::Time::getMillisecondCounterHiRes() + 100.0;
            flashing = true;
            repaint();
        }
        void updateFlash (double now)
        {
            if (flashing && now >= flashUntilMs)
            {
                flashing = false;
                repaint();
            }
        }
        void paintButton (juce::Graphics& g, bool hover, bool down) override
        {
            if (static_cast<bool> (getProperties()["rowNumber"]))
            {
                juce::TextButton::paintButton (g, hover, down);
                return;
            }
            const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
            if (bounds.isEmpty())
                return;
            const bool hit = isEnabled() && flashing;
            const float light = hit ? intensity : 0.0f;
            const float alpha = isEnabled() ? 1.0f : 0.34f;
            const float radius = juce::jmin (10.0f, bounds.getWidth() * 0.13f);
            const auto face = bounds.reduced (2).withTrimmedBottom (3).translated (0, hit || down ? 2.0f : 0.0f);
            juce::Path silhouette;
            silhouette.addRoundedRectangle (face, radius);
            g.setColour (Palette::shadeControl (juce::Colours::black).withAlpha (alpha * 0.85f));
            g.fillRoundedRectangle (bounds, radius);
            if (hit)
            {
                glow.setColor (colour.withAlpha (0.9f * light), 0);
                glow.setColor (colour.withAlpha (0.65f * light), 1);
                glow.render (g, silhouette);
            }
            const auto tint = [tintColour = colour, alpha] (float shade, float saturation)
            {
                return Palette::shadeControl (juce::Colour::greyLevel (shade)).interpolatedWith (tintColour, saturation).withAlpha (alpha);
            };
            juce::ColourGradient finish (tint (0.32f, 0.25f + 0.61f * light), face.getTopLeft(),
                                         tint (0.06f, 0.16f + 0.60f * light), face.getBottomLeft(), false);
            finish.addColour (0.17, tint (hover || hit ? 0.26f : 0.20f, 0.18f + 0.78f * light));
            finish.addColour (0.85, tint (0.11f, 0.12f + 0.73f * light));
            g.setGradientFill (finish);
            g.fillPath (silhouette);
            g.setColour (colour.withAlpha (alpha * (hasKeyboardFocus (true) ? 1.0f : 0.46f + 0.54f * light)));
            g.strokePath (silhouette, juce::PathStrokeType (1));
            g.setColour (juce::Colours::white.withAlpha (alpha * (0.12f + 0.2f * light)));
            g.drawHorizontalLine (juce::roundToInt (face.getY() + 2), face.getX() + radius, face.getRight() - radius);
            drawShortcutHint (g, keyLabel + "  " + noteLabel, face.withTrimmedBottom (7),
                              getHeight(), juce::Justification::centredBottom, alpha);
        }

    private:
        void cancelColourHold()
        {
            colourHoldPending = false;
            colourHoldTimer.stopTimer();
        }
        bool colourHoldPending = false;
        juce::TimedCallback colourHoldTimer { [this]
        {
            cancelColourHold();
            if (isShowing() && isEnabled() && showColourPicker)
                showColourPicker();
        } };
        int velocity = CellDefaults::velocity;
        float intensity = 0.0f;
        juce::Colour colour { Palette::accent };
        juce::String keyLabel, noteLabel;
        melatonin::DropShadow glow { { Palette::accent.withAlpha (0.9f), 14 }, { Palette::accent.withAlpha (0.65f), 4 } };
        double flashUntilMs = 0.0;
        bool flashing = false;
    };

    /** Native numeric control: drag, arrows, wheel, double-click to type, context reset. */
    class ValueControl : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        ValueControl()
        {
            setWantsKeyboardFocus (true);
            input.setJustification (juce::Justification::centred);
            input.setInputRestrictions (12, "-0123456789");
            input.onReturnKey = [this] { acceptInput(); };
            input.onEscapeKey = [this] { input.setVisible (false); grabKeyboardFocus(); };
            input.onFocusLost = [this] { if (input.isVisible()) acceptInput(); };
            addChildComponent (input);
        }
        int value = 0, minimum = 0, maximum = 100, resetValue = 0;
        float sensitivity = 4;
        float valueFontSize = 14, captionFontSize = 10;
        float suffixFontScale = 1.0f;
        juce::Colour captionColour { Palette::text };
        int captionGap = 0, captionInset = 0;
        int captionWidth = 0;
        int valueWidth = 0;
        juce::String suffix, caption;
        bool mixed = false, showAdjustmentHintAtZero = false, horizontalAdjustment = false, gestureOnly = false;
        bool horizontalDragOnTouch = false, insetRowArtwork = false;
        std::function<juce::String (int)> format;
        double displayScale = 1.0, displayOffset = 0.0;
        int displayDecimalPlaces = 0;
        double displayedValue (int v) const { return static_cast<double> (v) * displayScale + displayOffset; }
        int valueFromDisplay (double v) const { return juce::roundToInt ((v - displayOffset) / displayScale); }
        juce::String valueText (int v) const
        {
            auto result = juce::String (displayedValue (v), displayDecimalPlaces);
            return displayDecimalPlaces > 0 ? result.trimCharactersAtEnd ("0").trimCharactersAtEnd (".") : result;
        }
        std::function<void()> onBegin, onEnd, onClick, onDoubleClick;
        std::function<void()> onCancel;
        std::function<void()> onContextMenu;
        void cancelGesture()
        {
            const bool wasActive = dragging || wheeling || input.isVisible();
            dragging = wheeling = false;
            stopTimer();
            if (pendingWheel == this)
                pendingWheel = nullptr;
            input.setVisible (false);
            if (wasActive && onCancel)
                onCancel();
            repaint();
        }
        std::function<void (int)> onChange;
        void setLayerHighlighted (bool highlighted)
        {
            if (std::exchange (layerHighlighted, highlighted) != highlighted)
                repaint();
        }
        void setValue (int v)
        {
            if (!dragging && !wheeling && !input.isVisible() && value != v)
            {
                value = v;
                repaint();
            }
        }
        juce::MouseCursor getMouseCursor() override
        {
            using Cursor = juce::MouseCursor;
            if (!isEnabled())
                return Cursor::NormalCursor;
            return horizontalAdjustment ? Cursor::LeftRightResizeCursor : Cursor::UpDownResizeCursor;
        }
        juce::Rectangle<float> valueFaceBounds() const
        {
            auto r = getLocalBounds().toFloat();
            if (caption.isNotEmpty())
                r = captionWidth > 0 ? r.withTrimmedLeft (static_cast<float> (captionWidth))
                                     : r.withTrimmedTop (static_cast<float> (18 + captionGap));
            if (valueWidth > 0)
                r.setWidth (static_cast<float> (juce::jmin (valueWidth, getWidth())));
            if (static_cast<bool> (getProperties()["insetFooterArtwork"]))
                return footerArtworkBounds (r);
            return insetRowArtwork ? rowControlFace (r) : r;
        }
        void paint (juce::Graphics& g) override
        {
            if (caption.isNotEmpty())
            {
                const auto bounds = getLocalBounds().toFloat();
                const auto label = captionWidth > 0 ? bounds.withWidth (static_cast<float> (captionWidth))
                                                    : bounds.withHeight (18).withTrimmedLeft (static_cast<float> (captionInset));
                text (g, caption, label, captionFontSize, captionColour, true, juce::Justification::centredLeft);
            }
            const auto r = valueFaceBounds();
            const bool footerControl = static_cast<bool> (getProperties()["footerControl"]);
            const bool insetFooterArtwork = static_cast<bool> (getProperties()["insetFooterArtwork"]);
            if (footerControl)
                footerSurface (g, r.reduced (0.5f), false, isMouseOver() || hasKeyboardFocus (true), dragging || input.isVisible(), isEnabled());
            else if (layerHighlighted && isEnabled())
            {
                const auto bounds = r.reduced (0.5f);
                const auto top = Palette::controls.raised.interpolatedWith (Palette::accent, 0.27f);
                const auto bottom = Palette::controls.surface.interpolatedWith (Palette::accent, 0.12f);
                g.setGradientFill (juce::ColourGradient (top, bounds.getTopLeft(), bottom, bounds.getBottomLeft(), false));
                g.fillRoundedRectangle (bounds, 5.0f);
                g.setColour (Palette::accent.withAlpha (0.85f));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
            }
            else
                controlSurface (g, r.reduced (0.5f), dragging || input.isVisible(), isEnabled() && (isMouseOver() || hasKeyboardFocus (false)));
            if (gestureOnly || (showAdjustmentHintAtZero && value == 0 && !mixed))
            {
                const auto centre = r.getCentre();
                const float halfHeight = juce::jmin (9.0f, r.getHeight() * 0.25f);
                const float wing = halfHeight * 0.45f;
                juce::Path arrows;
                arrows.startNewSubPath (centre.x, centre.y - halfHeight);
                arrows.lineTo (centre.x, centre.y + halfHeight);
                for (const float direction : { -1.0f, 1.0f })
                {
                    const float tip = centre.y + direction * halfHeight;
                    const float base = tip - direction * wing;
                    arrows.startNewSubPath (centre.x - wing, base);
                    arrows.lineTo (centre.x, tip);
                    arrows.lineTo (centre.x + wing, base);
                }
                if (horizontalAdjustment || (horizontalDragOnTouch && draggingHorizontally))
                    arrows.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, centre.x, centre.y));
                g.setColour (isEnabled() ? Palette::text : Palette::faint);
                g.strokePath (arrows, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            const auto label = mixed ? juce::String::fromUTF8 ("—") : format ? format (value) : valueText (value) + suffix;
            // Reserve the whole numeric range for plain values, so suffixes stay
            // anchored even across 9/10 and 99/100. Formatted labels also use
            // equal-width digits without changing their nonnumeric text.
            const int slots = !mixed && !format && displayDecimalPlaces == 0 ? juce::jmax (2, juce::String (minimum).length(), juce::String (maximum).length()) : 0;
            const float displayFontSize = insetFooterArtwork ? valueFontSize * footerArtworkScale : valueFontSize;
            const float displaySuffixScale = slots > 0 && suffix.isNotEmpty() ? suffixFontScale : 1.0f;
            if (numericText != label || numericSlots != slots || std::abs (numericSuffixScale - displaySuffixScale) > 0.001f
                || std::abs (numericFontSize - displayFontSize) > 0.001f)
            {
                numericText = label;
                numericSlots = slots;
                numericFontSize = displayFontSize;
                numericSuffixScale = displaySuffixScale;
                const auto labelFont = font (displayFontSize, true);
                const auto suffixFont = font (displayFontSize * displaySuffixScale, true);
                float digitWidth = 0;
                for (const auto digit : juce::String ("0123456789"))
                    digitWidth = juce::jmax (digitWidth, juce::GlyphArrangement::getStringWidth (labelFont, juce::String::charToString (digit)));
                numericBaseline = (labelFont.getAscent() - labelFont.getDescent()) * 0.5f;
                numericGlyphs.clear();
                numericWidth = slots > 0 ? static_cast<float> (juce::jmax (0, slots - juce::String (value).length())) * digitWidth : 0.0f;
                for (int i = 0; i < label.length();)
                {
                    const auto character = label[i];
                    if ((character >= '0' && character <= '9') || (slots > 0 && character == '-'))
                    {
                        juce::GlyphArrangement digit;
                        digit.addLineOfText (labelFont, juce::String::charToString (character), 0, 0);
                        const auto bounds = digit.getBoundingBox (0, -1, true);
                        digit.moveRangeOfGlyphs (0, -1, numericWidth + (digitWidth - bounds.getWidth()) * 0.5f - bounds.getX(), 0);
                        numericGlyphs.addGlyphArrangement (digit);
                        numericWidth += digitWidth;
                        ++i;
                    }
                    else
                    {
                        const int runStart = i++;
                        while (i < label.length() && !(label[i] >= '0' && label[i] <= '9'))
                            ++i;
                        const auto run = label.substring (runStart, i);
                        const auto& runFont = runStart >= label.length() - suffix.length() ? suffixFont : labelFont;
                        numericGlyphs.addLineOfText (runFont, run, numericWidth, 0);
                        numericWidth += juce::GlyphArrangement::getStringWidth (runFont, run);
                    }
                }
            }
            g.setColour (footerControl ? footerInk (false, isEnabled()) : isEnabled() ? Palette::text : Palette::faint);
            const float labelScale = insetFooterArtwork && numericWidth > 0
                ? juce::jlimit (0.0f, 1.0f, (r.getWidth() - 4.0f) / numericWidth) : 1.0f;
            numericGlyphs.draw (g, juce::AffineTransform::scale (labelScale)
                .translated (r.getCentreX() - numericWidth * labelScale * 0.5f, r.getCentreY() + numericBaseline * labelScale));
        }
        void resized() override
        {
            const bool insetFooterArtwork = static_cast<bool> (getProperties()["insetFooterArtwork"]);
            input.setBounds (valueFaceBounds().toNearestInt());
            input.setFont (font (insetFooterArtwork ? valueFontSize * footerArtworkScale : valueFontSize));
        }
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            endWheelGesture();
            if (!isEnabled())
                return;
            if (e.mods.isPopupMenu())
            {
                if (onContextMenu)
                {
                    onContextMenu();
                    return;
                }
                if (onBegin)
                    onBegin();
                update (resetValue);
                if (onEnd)
                    onEnd();
                return;
            }
            if (isShowing())
                grabKeyboardFocus();
            start = value;
            draggingHorizontally = horizontalAdjustment || (horizontalDragOnTouch && e.source.isTouch());
            dragging = true;
            if (onBegin)
                onBegin();
            if (onContextMenu && e.source.isTouch())
                startTimer (450);
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging)
            {
                if (e.getDistanceFromDragStart() > 4)
                    stopTimer();
                const auto delta = draggingHorizontally ? e.getDistanceFromDragStartX() : -e.getDistanceFromDragStartY();
                if (std::abs (delta) > 4)
                    update (start + (delta < 0 ? -1 : 1) * static_cast<int> (std::floor ((static_cast<float> (std::abs (delta)) - 4) / sensitivity + 0.5f)));
                else if (draggingHorizontally || horizontalDragOnTouch)
                    update (start);
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (!dragging)
                return;
            stopTimer();
            dragging = false;
            if (onEnd)
                onEnd();
            if (e.getDistanceFromDragStart() < 3 && onClick)
                onClick();
            repaint();
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (onContextMenu && e.mods.isPopupMenu())
                return;
            if (onDoubleClick)
            {
                if (isEnabled() && e.mods.isLeftButtonDown() && !e.mods.isPopupMenu())
                {
                    endWheelGesture();
                    onDoubleClick();
                }
                return;
            }
            if (gestureOnly)
                return;
            endWheelGesture();
            dragging = false;
            if (onEnd)
                onEnd();
            input.setInputRestrictions (12, displayDecimalPlaces > 0 ? "-0123456789." : "-0123456789");
            input.setText (valueText (value), false);
            input.setVisible (true);
            input.grabKeyboardFocus();
            input.selectAll();
        }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            if (!isEnabled() || dragging || std::abs (w.deltaY) < 0.00001f)
                return;
            // A trackpad scroll arrives as many ticks: keep them in one undo step and host gesture.
            if (!wheeling)
            {
                if (onBegin)
                    onBegin();
                wheeling = true;
                pendingWheel = this;
            }
            update (value + (w.deltaY > 0 ? 1 : -1));
            startTimer (wheelGestureIdleMs);
        }
        /** Closes a pending scroll-wheel gesture now instead of after the idle timeout. */
        void endWheelGesture()
        {
            stopTimer();
            if (!wheeling)
                return;
            wheeling = false;
            if (pendingWheel == this)
                pendingWheel = nullptr;
            if (onEnd)
                onEnd();
        }
        /** Any new edit gesture must close an idle scroll first, or the two would share one undo step. */
        static void endPendingWheelGesture()
        {
            if (auto* control = pendingWheel.getComponent())
                control->endWheelGesture();
        }
        bool isWheelGestureActive() const { return wheeling; }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
        {
            class Value : public juce::AccessibilityRangedNumericValueInterface
            {
            public:
                explicit Value (ValueControl& c) : control (c) {}
                bool isReadOnly() const override { return !control.isEnabled(); }
                double getCurrentValue() const override { return control.displayedValue (control.value); }
                void setValue (double v) override
                {
                    if (control.isEnabled())
                    {
                        control.endWheelGesture();
                        if (control.onBegin)
                            control.onBegin();
                        control.update (control.valueFromDisplay (v));
                        if (control.onEnd)
                            control.onEnd();
                    }
                }
                AccessibleValueRange getRange() const override { return { { control.displayedValue (control.minimum), control.displayedValue (control.maximum) }, control.displayScale }; }

            private:
                ValueControl& control;
            };
            return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions {}, juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (isEnabled() && onContextMenu && k.getKeyCode() == juce::KeyPress::F10Key && k.getModifiers().isShiftDown())
            {
                endWheelGesture();
                onContextMenu();
                return true;
            }
            const int increaseKey = horizontalAdjustment ? juce::KeyPress::rightKey : juce::KeyPress::upKey;
            const int decreaseKey = horizontalAdjustment ? juce::KeyPress::leftKey : juce::KeyPress::downKey;
            const bool increase = k == increaseKey || (horizontalDragOnTouch && k == juce::KeyPress::rightKey);
            const bool decrease = k == decreaseKey || (horizontalDragOnTouch && k == juce::KeyPress::leftKey);
            if (increase || decrease)
            {
                endWheelGesture();
                if (onBegin)
                    onBegin();
                update (value + (increase ? 1 : -1));
                if (onEnd)
                    onEnd();
                return true;
            }
            return false;
        }

    private:
        static constexpr int wheelGestureIdleMs = 350;
        juce::TextEditor input;
        static inline juce::Component::SafePointer<ValueControl> pendingWheel;
        juce::GlyphArrangement numericGlyphs;
        juce::String numericText;
        int numericSlots = -1;
        float numericFontSize = 0, numericWidth = 0, numericBaseline = 0;
        float numericSuffixScale = 1.0f;
        bool layerHighlighted = false;
        bool dragging = false, wheeling = false;
       #if JUCE_IOS
        bool draggingHorizontally = true;
       #else
        bool draggingHorizontally = false;
       #endif
        int start = 0;
        void timerCallback() override
        {
            if (!dragging)
            {
                endWheelGesture();
                return;
            }
            stopTimer();
            if (!isShowing() || !isEnabled() || !onContextMenu)
                return;
            dragging = false;
            if (onEnd)
                onEnd();
            repaint();
            onContextMenu();
        }
        void update (int next)
        {
            next = juce::jlimit (minimum, maximum, next);
            if (value != next)
            {
                value = next;
                mixed = false;
                if (onChange)
                    onChange (next);
                repaint();
            }
        }
        void acceptInput()
        {
            const int next = valueFromDisplay (input.getText().getDoubleValue());
            input.setVisible (false);
            endWheelGesture();
            if (onBegin)
                onBegin();
            update (next);
            if (onEnd)
                onEnd();
        }
    };
}
