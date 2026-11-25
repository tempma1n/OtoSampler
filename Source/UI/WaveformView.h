// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Theme.h"
#include "../Dsp/SampleTypes.h"

namespace oto::ui
{
// Waveform with draggable Start / Loop Start / Loop End markers.
// Loop markers are shown snapped to pitch marks, exactly where the engine will loop.
class WaveformView : public juce::Component
{
public:
    WaveformView (juce::RangedAudioParameter& pStart, juce::RangedAudioParameter& pLoopStart,
                  juce::RangedAudioParameter& pLoopEnd, juce::RangedAudioParameter& pLoopOn)
        : aStart (pStart,     [this] (float v) { vStart = v; repaint(); }, nullptr),
          aLs    (pLoopStart, [this] (float v) { vLs = v; repaint(); }, nullptr),
          aLe    (pLoopEnd,   [this] (float v) { vLe = v; repaint(); }, nullptr),
          aOn    (pLoopOn,    [this] (float v) { loopOn = v > 0.5f; repaint(); }, nullptr)
    {
        aStart.sendInitialUpdate(); aLs.sendInitialUpdate(); aLe.sendInitialUpdate(); aOn.sendInitialUpdate();
    }

    std::function<void()> onClickEmpty;
    std::function<void (bool)> onPreview;      // left click on the waveform: down / up

    ~WaveformView() override { if (previewing && onPreview) onPreview (false); }

    void setData (std::shared_ptr<const oto::SampleData> d, const juce::String& name)
    {
        data = std::move (d); sampleName = name;
        rebuildPeaks(); repaint();
    }

    void resized() override { rebuildPeaks(); }

    void setPlayheads (const std::vector<float>& p)
    {
        if (p == playheads) return;
        playheads = p;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& P = pal();
        const auto b = getLocalBounds().toFloat();
        g.setColour (P.container);
        g.fillRoundedRectangle (b, 22.f);

        if (data == nullptr)
        {
            g.setColour (P.onSurfaceVariant);
            g.setFont (font (15.f, true));
            g.drawText (juce::String::fromUTF8 ("Drop an audio file here  /  click to open"), getLocalBounds(), juce::Justification::centred);
            g.setFont (font (12.5f));
            g.setColour (P.outline);
            g.drawText ("WAV  AIFF  FLAC  MP3  OGG", getLocalBounds().withTrimmedTop (40), juce::Justification::centred);
            return;
        }

        const auto in = inner();
        const float cy = in.getCentreY();

        // loop region
        const float xS = xOf (effStart()), xL = xOf (snap (vLs)), xE = xOf (snap (vLe));
        const float lx0 = juce::jmin (xL, xE), lx1 = juce::jmax (xL, xE);
        g.setColour (loopOn ? P.tertiaryContainer.withAlpha (isDark() ? 0.9f : 0.75f) : P.containerHigh.withAlpha (0.5f));
        g.fillRoundedRectangle (lx0, in.getY() - 4.f, lx1 - lx0, in.getHeight() + 8.f, 10.f);

        // bars
        const float barW = 2.f, pitch = 3.f;
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float x = in.getX() + (float) i * pitch;
            const float top = cy - peaks[i].second * in.getHeight() * 0.5f;
            const float bot = cy - peaks[i].first  * in.getHeight() * 0.5f;
            const bool before = x < xS;
            g.setColour (before ? P.outlineVariant : P.primary.withAlpha (0.9f));
            g.fillRoundedRectangle (x, top, barW, juce::jmax (2.f, bot - top), 1.f);
        }

        // markers
        auto drawLoopMarker = [&] (float x, bool isStart)
        {
            g.setColour (loopOn ? P.tertiary : P.outline);
            g.fillRect (x - 1.f, in.getY() - 4.f, 2.f, in.getHeight() + 8.f);
            juce::Path flag;
            const float dir = isStart ? 1.f : -1.f;
            flag.addRoundedRectangle (isStart ? x - 1.f : x - 15.f, in.getY() - 8.f, 16.f, 14.f, 4.f);
            juce::ignoreUnused (dir);
            g.fillPath (flag);
            g.setColour (loopOn ? juce::Colour (isDark() ? 0xff00201a : 0xffffffff) : P.container);
            g.setFont (font (10.f, true));
            g.drawText (isStart ? "L" : "R", juce::Rectangle<float> (isStart ? x - 1.f : x - 15.f, in.getY() - 8.f, 16.f, 14.f), juce::Justification::centred);
        };
        drawLoopMarker (juce::jmin (xL, xE), true);
        drawLoopMarker (juce::jmax (xL, xE), false);

        g.setColour (P.onSurface);
        g.fillRect (xS - 1.f, in.getY() - 4.f, 2.f, in.getHeight() + 8.f);
        juce::Path tri;
        tri.addTriangle (xS - 6.f, in.getBottom() + 12.f, xS + 6.f, in.getBottom() + 12.f, xS, in.getBottom() + 3.f);
        g.fillPath (tri);

        // playheads of all sounding voices
        for (float ph : playheads)
        {
            const float x = xOf (ph);
            g.setColour (P.onSurface.withAlpha (0.9f));
            g.fillRect (x - 1.f, in.getY() - 4.f, 2.f, in.getHeight() + 8.f);
            g.fillEllipse (x - 4.5f, cy - 4.5f, 9.f, 9.f);
            g.setColour (P.container);
            g.fillEllipse (x - 2.f, cy - 2.f, 4.f, 4.f);
        }

        // info
        const auto& s = *data->source;
        const double sec = (double) s.numSamples() / s.sampleRate;
        g.setColour (P.onSurfaceVariant);
        g.setFont (font (11.5f));
        g.drawText (juce::String (sec, 2) + " s   " + juce::String (s.sampleRate / 1000.0, 1) + " kHz   "
                    + (s.channels.size() > 1 ? "Stereo" : "Mono") + "   "
                    + juce::String ((int) std::count (s.map.voiced.begin(), s.map.voiced.end(), (uint8_t) 1)) + " pitch marks",
                    getLocalBounds().withTrimmedLeft (16).withTrimmedBottom (4), juce::Justification::bottomLeft);
#if JUCE_MAC
        g.drawText ("S start   L / R sustain loop   Cmd+click narrow loop + on   Click: set Start + play", getLocalBounds().withTrimmedRight (16).withTrimmedBottom (4), juce::Justification::bottomRight);
#else
        g.drawText ("S start   L / R sustain loop   Ctrl+click narrow loop + on   Click: set Start + play", getLocalBounds().withTrimmedRight (16).withTrimmedBottom (4), juce::Justification::bottomRight);
#endif
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (data != nullptr && hit (e.position.x) != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown()) return;
        if (data == nullptr) { if (onClickEmpty) onClickEmpty(); return; }
        if (e.mods.isLeftButtonDown() && (e.mods.isCtrlDown() || e.mods.isCommandDown()))
        {
            dragging = 0;
            applyNarrowestLoop (e.position.x);   // Ctrl + left click: narrowest loop around the clicked position
            return;
        }
        dragging = hit (e.position.x);
        if (dragging == 0)   // plain left click away from the markers: move Start here and play from here while held
        {
            const auto in = inner();
            aStart.setValueAsCompleteGesture (juce::jmin (juce::jlimit (0.f, 1.f, (e.position.x - in.getX()) / in.getWidth()), startLimit()));
            if (onPreview) { previewing = true; onPreview (true); }
            return;
        }
        if (dragging == 1)   // grabbing the Start marker (e.g. clicking again at the same place) also plays from Start
        {
            aStart.beginGesture();
            if (onPreview) { previewing = true; onPreview (true); }
        }
        if (dragging == 2) aLs.beginGesture();
        if (dragging == 3) aLe.beginGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == 0) return;
        const auto in = inner();
        float n = juce::jlimit (0.f, 1.f, (e.position.x - in.getX()) / in.getWidth());
        const float gap = 0.002f;
        if (dragging == 1) aStart.setValueAsPartOfGesture (juce::jmin (n, startLimit()));   // Start never passes the loop start
        if (dragging == 2) aLs.setValueAsPartOfGesture (juce::jmin (n, vLe - gap));
        if (dragging == 3) aLe.setValueAsPartOfGesture (juce::jmax (n, vLs + gap));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (previewing) { previewing = false; if (onPreview) onPreview (false); }
        if (dragging == 1) aStart.endGesture();
        if (dragging == 2) aLs.endGesture();
        if (dragging == 3) aLe.endGesture();
        dragging = 0;
    }

private:
    // Narrowest valid loop containing the clicked position (also turns Sustain Loop on): one pitch period between two
    // neighbouring pitch marks (extended to more periods only if it would be shorter than 64 samples).
    void applyNarrowestLoop (float x)
    {
        const auto& m = data->source->map;
        const int n = (int) m.marks.size();
        const int N = data->numSamples();
        if (n < 2 || N <= 0) return;
        const auto in = inner();
        const double pos = (double) juce::jlimit (0.f, 1.f, (x - in.getX()) / in.getWidth()) * (double) N;

        int i = (int) (std::upper_bound (m.marks.begin(), m.marks.end(), (int) pos) - m.marks.begin()) - 1;
        i = juce::jlimit (0, n - 2, i);
        int j = i + 1;
        while (m.marks[(size_t) j] - m.marks[(size_t) i] < 64 && j + 1 < n) ++j;
        while (m.marks[(size_t) j] - m.marks[(size_t) i] < 64 && i > 0) --i;
        if (m.marks[(size_t) j] - m.marks[(size_t) i] < 64) return;

        aLs.setValueAsCompleteGesture ((float) ((double) m.marks[(size_t) i] / (double) N));
        aLe.setValueAsCompleteGesture ((float) ((double) m.marks[(size_t) j] / (double) N));
        aOn.setValueAsCompleteGesture (1.f);      // and switch Sustain Loop on
    }

    // Start is limited to the loop start point while Sustain Loop is on
    float startLimit() const
    {
        if (data == nullptr) return 1.f;
        return oto::maxStartNorm (data->source->map, data->numSamples(), loopOn, vLs, vLe);
    }
    float effStart() const { return juce::jmin (vStart, startLimit()); }

    juce::Rectangle<float> inner() const { return getLocalBounds().toFloat().withTrimmedLeft (16).withTrimmedRight (16).withTrimmedTop (22).withTrimmedBottom (34); }
    float xOf (float norm) const { const auto in = inner(); return in.getX() + norm * in.getWidth(); }
    float snap (float norm) const
    {
        if (data == nullptr || data->numSamples() == 0) return norm;
        const double N = (double) data->numSamples();
        return (float) (oto::snapToMark (data->source->map, (double) norm * N) / N);
    }
    int hit (float x) const
    {
        const float xs[3] = { xOf (effStart()), xOf (snap (vLs)), xOf (snap (vLe)) };
        int best = 0; float bd = 10.f;
        // loop markers take priority over start when they overlap
        const int order[3] = { 1, 2, 0 };
        for (int k : order) { const float d = std::abs (x - xs[k]); if (d < bd) { bd = d; best = k + 1; } }
        return best;
    }
    void rebuildPeaks()
    {
        peaks.clear();
        if (data == nullptr || data->numSamples() == 0 || getWidth() < 40) return;
        const auto in = inner();
        const int cols = juce::jmax (1, (int) (in.getWidth() / 3.f));
        const int N = data->numSamples();
        peaks.resize ((size_t) cols);
        float mx = 1e-6f;
        for (int c = 0; c < cols; ++c)
        {
            const int s0 = (int) ((int64_t) c * N / cols), s1 = juce::jmax (s0 + 1, (int) ((int64_t) (c + 1) * N / cols));
            const int stride = juce::jmax (1, (s1 - s0) / 256);
            float lo = 0.f, hi = 0.f;
            for (auto& ch : data->channels)
                for (int i = s0; i < s1; i += stride) { lo = juce::jmin (lo, ch[(size_t) i]); hi = juce::jmax (hi, ch[(size_t) i]); }
            peaks[(size_t) c] = { lo, hi };
            mx = juce::jmax (mx, juce::jmax (-lo, hi));
        }
        for (auto& p : peaks) { p.first /= mx; p.second /= mx; }
    }

    std::shared_ptr<const oto::SampleData> data;
    juce::String sampleName;
    std::vector<std::pair<float, float>> peaks;   // (min, max) normalised
    float vStart = 0.f, vLs = 0.35f, vLe = 0.65f;
    bool loopOn = false;
    int dragging = 0;
    bool previewing = false;
    std::vector<float> playheads;
    juce::ParameterAttachment aStart, aLs, aLe, aOn;
};
}
