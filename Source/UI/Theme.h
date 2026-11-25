// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../Common.h"

namespace oto::ui
{
// Flat Material-You style tonal palette (warm paper + vermilion). No gradients, no shadows.
struct Palette
{
    juce::Colour bg, container, containerHigh, outline, outlineVariant,
                 onSurface, onSurfaceVariant, primary, onPrimary, primaryContainer, onPrimaryContainer,
                 tertiary, tertiaryContainer;
};

inline bool& isDark() { static bool d = false; return d; }
inline float& accentHue() { static float h = -1.f; return h; }   // -1 = default vermilion, else 0..1

inline Palette basePalette (bool dark)
{
    static const Palette light {
        juce::Colour (0xfffaf6ee), juce::Colour (0xffefe9dc), juce::Colour (0xffe2dac8),
        juce::Colour (0xff80776a), juce::Colour (0xffcdc3af),
        juce::Colour (0xff1f1b13), juce::Colour (0xff50473a),
        juce::Colour (0xffb23a1d), juce::Colour (0xffffffff), juce::Colour (0xffffd9cf), juce::Colour (0xff3a0a00),
        juce::Colour (0xff23695a), juce::Colour (0xffb4eedc) };
    static const Palette darkP {
        juce::Colour (0xff15130e), juce::Colour (0xff201d16), juce::Colour (0xff2c2920),
        juce::Colour (0xff9a917f), juce::Colour (0xff4d4639),
        juce::Colour (0xffece4d4), juce::Colour (0xffcfc6b2),
        juce::Colour (0xffffb5a1), juce::Colour (0xff5f1400), juce::Colour (0xff852611), juce::Colour (0xffffd9cf),
        juce::Colour (0xff93d4c3), juce::Colour (0xff0f4f43) };
    return dark ? darkP : light;
}

inline juce::Colour accentSwatch (float hue)
{
    if (hue < 0.f) return basePalette (false).primary;
    return juce::Colour::fromHSL (hue, 0.70f, 0.40f, 1.f);
}

inline Palette makePalette (bool dark, float hue)
{
    Palette p = basePalette (dark);
    if (hue < 0.f) return p;
    const float h = hue, t = std::fmod (hue + 0.45f, 1.f);
    using C = juce::Colour;
    if (! dark)
    {
        p.primary = C::fromHSL (h, 0.70f, 0.40f, 1.f);   p.onPrimary = C (0xffffffff);
        p.primaryContainer = C::fromHSL (h, 1.f, 0.90f, 1.f);  p.onPrimaryContainer = C::fromHSL (h, 1.f, 0.10f, 1.f);
        p.tertiary = C::fromHSL (t, 0.50f, 0.28f, 1.f);  p.tertiaryContainer = C::fromHSL (t, 0.60f, 0.85f, 1.f);
    }
    else
    {
        p.primary = C::fromHSL (h, 1.f, 0.82f, 1.f);     p.onPrimary = C::fromHSL (h, 1.f, 0.19f, 1.f);
        p.primaryContainer = C::fromHSL (h, 0.70f, 0.30f, 1.f); p.onPrimaryContainer = C::fromHSL (h, 1.f, 0.90f, 1.f);
        p.tertiary = C::fromHSL (t, 0.40f, 0.72f, 1.f);  p.tertiaryContainer = C::fromHSL (t, 0.60f, 0.22f, 1.f);
    }
    return p;
}

inline const Palette& pal()
{
    static Palette cache = basePalette (false);
    static bool cd = false; static float ch = -1.f;
    if (cd != isDark() || ch != accentHue()) { cd = isDark(); ch = accentHue(); cache = makePalette (cd, ch); }
    return cache;
}

inline juce::Font font (float h, bool bold = false)
{
    const auto opts = juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain);
#if JUCE_WINDOWS
    return juce::Font (opts.withName ("Segoe UI"));
#else
    return juce::Font (opts.withName (juce::Font::getDefaultSansSerifFontName()));
#endif
}

inline bool& animationsEnabled() { static bool a = true; return a; }
// Hover / press scale target: with animations off the controls stay at 1.0
inline float anim (float v) { return animationsEnabled() ? v : 1.f; }

// Underdamped spring -> the "squishy" overshoot
struct Spring
{
    float x = 0.f, v = 0.f;
    void step (float target, float k, float c, float dt) noexcept
    {
        if (! animationsEnabled()) { x = target; v = 0.f; return; }
        v += (k * (target - x) - c * v) * dt;
        x += v * dt;
    }
    bool settled (float target) const noexcept { return std::abs (v) < 2e-3f && std::abs (x - target) < 2e-3f; }
    void snap (float t) noexcept { x = t; v = 0.f; }
};

// 60 Hz animation driver that sleeps when everything has settled
class AnimatedBase : public juce::Component, protected juce::Timer
{
protected:
    void wake() { if (! isTimerRunning()) startTimerHz (60); }
    virtual bool stepAnimation (float dt) = 0;   // returns true while still moving
    void timerCallback() override
    {
        bool busy = false;
        for (int i = 0; i < 2; ++i) busy |= stepAnimation (1.f / 120.f);
        if (! busy) stopTimer();
        repaint();
    }
};
}
