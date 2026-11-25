// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "WaveformView.h"
#include "Panels.h"

namespace oto::ui
{
// Height of the OSC tab bar that sits between the header and the waveform
inline constexpr int kBarH = 44;

// All controls of one OSC: waveform, knobs, switches and the cards behind them.
// The page covers the whole canvas (same coordinates as the editor) and only its children take mouse input.
class OscPage : public juce::Component
{
public:
    OscPage (OtoProcessor& p, int oscIndex)
        : proc (p), idx (oscIndex),
          wave (P (ids::start), P (ids::loopStart), P (ids::loopEnd), P (ids::loopOn)),
          kRoot (P (ids::root), "Root Key"), kPitch (P (ids::pitch), "Pitch", true), kFine (P (ids::fine), "Fine", true),
          kVolume (P (ids::volume), "Volume"), kStart (P (ids::start), "Start"), kVel (P (ids::velocity), "Velocity"),
          kA (P (ids::attack), "Attack"), kD (P (ids::decay), "Decay"), kS (P (ids::sustain), "Sustain"), kR (P (ids::release), "Release"),
          kXfade (P (ids::loopXfade), "Xfade"),
          kVibRate (P (ids::vibRate), "Rate"), kVibDepth (P (ids::vibDepth), "Depth"), kVibDelay (P (ids::vibDelay), "Delay"),
          kGlide (P (ids::glide), "Glide"), kBend (P (ids::bendRange), "Bend"),
          kDenoise (P (ids::denoise), "Denoise"),
          kHp (P (ids::preHp), "HP Cutoff"), kLp (P (ids::preLp), "LP Cutoff"),
          swNormalize (P (ids::normalize), "Normalize"), swFormant (P (ids::formantHold), "Formant Hold"),
          swSpeed (P (ids::speedHold), "Speed Hold"), swFlatten (P (ids::flatten), "Pitch Flatten"), swLoop (P (ids::loopOn)),
          swMono (P (ids::mono), "Mono"), swPre (P (ids::preOn)),
          filterView (P (ids::preOn), P (ids::preHp), P (ids::preLp))
    {
        setInterceptsMouseClicks (false, true);

        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &wave, &autoRootBtn,
                 &kRoot, &kPitch, &kFine, &kVolume, &kStart, &kVel, &kA, &kD, &kS, &kR,
                 &kXfade, &kVibRate, &kVibDepth, &kVibDelay, &kGlide, &kBend, &kDenoise, &kHp, &kLp,
                 &swNormalize, &swFormant, &swSpeed, &swFlatten, &swLoop, &swMono, &swPre, &filterView })
            addAndMakeVisible (c);

        kStart.limitValue = [this] (float v) { return juce::jmin (v, proc.osc (idx).getStartLimit()); };
        kPitch.shiftStep = 12.f;     // Shift + turn = one octave per step
        kPitch.dragPixels = 480.f;   // range is -48..+48: keep 5 pixels per semitone

        autoRootBtn.onClick = [this] { autoDetectRoot(); };
        wave.onClickEmpty = [this] { openFileChooser(); };
        wave.onPreview = [this] (bool on) { if (on) proc.osc (idx).startPreview(); else proc.osc (idx).stopPreview(); };
    }

    // Called by the editor timer while this page is shown
    void tick()
    {
        proc.osc (idx).getPlayheads (playheadBuf);
        wave.setPlayheads (playheadBuf);
        const int v = proc.osc (idx).getSampleVersion();
        if (v != lastVersion)
        {
            lastVersion = v;
            wave.setData (proc.osc (idx).getSampleData(), proc.osc (idx).getSampleName());
        }
    }

    int getRootNote()
    {
        auto& rp = P (ids::root);
        return (int) std::lround (rp.convertFrom0to1 (rp.getValue()));
    }

    void openFileChooser()
    {
        chooser = std::make_unique<juce::FileChooser> ("Select a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        juce::Component::SafePointer<OscPage> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr) return;
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) safe->proc.osc (safe->idx).loadSampleFromFile (f);
                              });
    }

    void loadFile (const juce::File& f) { proc.osc (idx).loadSampleFromFile (f); }

    void autoDetectRoot()
    {
        if (proc.osc (idx).getSampleData() == nullptr) { autoRootBtn.flash ("No sample"); return; }
        const int note = proc.osc (idx).detectRootNote();
        if (note < 0) { autoRootBtn.flash ("No pitch"); return; }

        auto& rp = P (ids::root);
        rp.beginChangeGesture();
        rp.setValueNotifyingHost (rp.convertTo0to1 ((float) note));
        rp.endChangeGesture();
        autoRootBtn.flash (rp.getText (rp.getValue(), 16));
    }

    // W: canvas width in canvas units. extra: height added on top of the base layout (goes to the waveform).
    void layoutContent (int W, int extra)
    {
        const int M = 16, G = 8;
        wave.setBounds (M, 64 + kBarH, W - 2 * M, 176 + extra);

        const int rowA = 248 + kBarH + extra, rowB = 408 + kBarH + extra, rowH = 152;
        const int kw = Knob::kWidth;
        const int avail = W - 2 * M;

        auto split = [&] (int y, const std::vector<int>& base, const std::vector<juce::Rectangle<int>*>& out)
        {
            int sum = 0;
            for (int b : base) sum += b;
            const int usable = avail - G * ((int) base.size() - 1);
            int x = M;
            for (size_t i = 0; i < base.size(); ++i)
            {
                const int w = (i + 1 == base.size()) ? (M + avail - x) : (int) std::lround ((double) base[i] * usable / sum);
                *out[i] = { x, y, w, rowH };
                x += w + G;
            }
        };
        split (rowA, { 504, 344, 284 }, { &cSample, &cEnv, &cPlay });
        split (rowB, { 170, 264, 184, 506 }, { &cLoop, &cVib, &cGlide, &cPre });

        auto placeKnobs = [&] (juce::Rectangle<int> card, std::initializer_list<Knob*> ks)
        {
            const int n = (int) ks.size();
            const int cell = (card.getWidth() - 24) / n;
            int i = 0;
            for (auto* k : ks) { k->setTopLeftPosition (card.getX() + 12 + i * cell + (cell - kw) / 2, card.getY() + 38); ++i; }
        };

        placeKnobs (cSample, { &kRoot, &kPitch, &kFine, &kVolume, &kStart, &kVel });
        placeKnobs (cEnv, { &kA, &kD, &kS, &kR });
        placeKnobs (cLoop, { &kXfade });
        placeKnobs (cVib, { &kVibRate, &kVibDepth, &kVibDelay });
        placeKnobs (cGlide, { &kGlide, &kBend });

        int y = cPlay.getY() + 40;
        for (auto* s : { &swFormant, &swSpeed, &swFlatten })
        {
            s->setBounds (cPlay.getX() + 16, y, 150, 32);
            y += 36;
        }
        kDenoise.setTopLeftPosition (cPlay.getRight() - 12 - Knob::kWidth, cPlay.getY() + 38);

        swNormalize.setBounds (cSample.getRight() - 12 - swNormalize.getIdealWidth(), cSample.getY() + 8, swNormalize.getIdealWidth(), 28);
        autoRootBtn.setBounds (swNormalize.getX() - 10 - 92, cSample.getY() + 8, 92, 28);
        swLoop.setBounds (cLoop.getRight() - 12 - 44, cLoop.getY() + 8, 44, 28);
        swMono.setBounds (cGlide.getRight() - 12 - swMono.getIdealWidth(), cGlide.getY() + 8, swMono.getIdealWidth(), 28);
        swPre.setBounds (cPre.getRight() - 12 - 44, cPre.getY() + 8, 44, 28);

        kHp.setTopLeftPosition (cPre.getX() + 12, cPre.getY() + 38);
        kLp.setTopLeftPosition (kHp.getRight(), cPre.getY() + 38);
        filterView.setBounds (kLp.getRight() + 8, cPre.getY() + 42, cPre.getRight() - 14 - (kLp.getRight() + 8), 96);
    }

    int cardsBottom() const { return cLoop.getBottom(); }

    void paint (juce::Graphics& g) override
    {
        const auto& Pl = pal();
        auto card = [&] (juce::Rectangle<int> r, const juce::String& title)
        {
            g.setColour (Pl.container);
            g.fillRoundedRectangle (r.toFloat(), 22.f);
            g.setColour (Pl.onSurfaceVariant);
            g.setFont (font (12.5f, true));
            g.drawText (title.toUpperCase(), r.getX() + 18, r.getY() + 12, r.getWidth() - 100, 20, juce::Justification::centredLeft);
        };
        card (cSample, "Sample");
        card (cEnv, "Amp Envelope");
        card (cPlay, "Playback");
        card (cLoop, "Sustain Loop");
        card (cVib, "Vibrato");
        card (cGlide, "Glide");
        card (cPre, "Pre-filter");
    }

private:
    juce::RangedAudioParameter& P (const char* id) { return *proc.apvts.getParameter (oscParamId (id, idx)); }

    OtoProcessor& proc;
    const int idx;

    WaveformView wave;
    TonalButton autoRootBtn { "Auto Root" };
    Knob kRoot, kPitch, kFine, kVolume, kStart, kVel;
    Knob kA, kD, kS, kR;
    Knob kXfade, kVibRate, kVibDepth, kVibDelay, kGlide, kBend, kHp, kLp, kDenoise;
    ToggleSwitch swNormalize, swFormant, swSpeed, swFlatten, swLoop, swMono, swPre;
    FilterView filterView;

    juce::Rectangle<int> cSample, cEnv, cPlay, cLoop, cVib, cGlide, cPre;
    int lastVersion = -1;
    std::vector<float> playheadBuf;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
