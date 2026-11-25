// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginEditor.h"

using namespace oto;
using namespace oto::ui;

OtoEditor::OtoEditor (OtoProcessor& p)
    : AudioProcessorEditor (&p), proc (p), keyboard (p.keyboardState)
{
    {
        const auto ui = oto::loadUiSettings();
        oto::ui::isDark() = ui.dark;
        oto::ui::accentHue() = ui.hue;
        oto::ui::animationsEnabled() = ui.animations;
        startW = ui.width; startH = ui.height;
        // the keyboard always starts hidden. A height saved by the previous version with the keyboard shown is converted
        if (ui.keyboardKnown && ui.keyboard)
            startH = juce::jmax (360, (int) std::lround ((double) startH * kBaseH / (double) (kBaseH + kKbH + kKbGap)));
    }

    setWantsKeyboardFocus (true);
    addAndMakeVisible (canvas);
    canvas.onPaint = [this] (juce::Graphics& g) { paintCanvas (g); };

    for (int i = 0; i < kMaxOsc; ++i)
    {
        pages[(size_t) i] = std::make_unique<OscPage> (proc, i);
        canvas.addChildComponent (*pages[(size_t) i]);
    }

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &bar, &openBtn, &optionsBtn, &themeBtn, &kbBtn, &keyboard })
        canvas.addAndMakeVisible (c);
    canvas.addChildComponent (panel);
    canvas.addChildComponent (menu);

    panel.onHue = [this] (float h) { oto::ui::accentHue() = h; saveUi(); repaint(); };
    panel.onDark = [this] (bool d) { oto::ui::isDark() = d; saveUi(); repaint(); };
    panel.onAnim = [this] (bool a) { oto::ui::animationsEnabled() = a; saveUi(); repaint(); };
    panel.sync();
    panel.onClose = [this] { panel.setVisible (false); };
    optionsBtn.onClick = [this] { togglePanel(); };
    juce::Desktop::getInstance().addGlobalMouseListener (this);

    bar.onSelect = [this] (int i) { menu.hide(); selectOsc (i); };
    bar.onContext = [this] (int i, juce::Point<int> p) { showOscMenu (i, p); };
    bar.onAdd = [this] { menu.hide(); doAdd(); };
    menu.onPick = [this] (int id)
    {
        enum { kCopy = 1, kPaste, kDuplicate, kDelete };
        switch (id)
        {
            case kCopy:      copyCurrent();      break;
            case kPaste:     pasteCurrent();     break;
            case kDuplicate: duplicateCurrent(); break;
            case kDelete:    deleteCurrent();    break;
            default: break;
        }
        grabKeyboardFocus();
    };
    watch[0].key = 'C'; watch[1].key = 'V'; watch[2].key = 'D'; watch[3].key = juce::KeyPress::deleteKey;

    openBtn.onClick = [this] { pages[(size_t) selected]->openFileChooser(); };
    themeBtn.onClick = [this] { toggleTheme(); };
    kbBtn.onClick = [this] { toggleKeyboard(); };
    kbBtn.setActive (kbVisible);
    keyboard.setVisible (kbVisible);

    lastCount = proc.getOscCount();
    selectOsc (0);

    setLookAndFeel (&gripLnf);
    constrainer.setMinimumSize (720, 360);
    constrainer.setMaximumSize (3840, 2160);
    setConstrainer (&constrainer);
    setResizable (true, true);             // drag the bottom-right corner to resize
    setSize (startW, startH);
    startTimerHz (60);
}

OtoEditor::~OtoEditor()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
    stopTimer();
    saveUi();                              // remembers the window size
    setLookAndFeel (nullptr);
    setConstrainer (nullptr);
}

void OtoEditor::saveUi()
{
    // the height is stored as it would be without the keyboard, because the editor always starts without it
    const int h = kbVisible ? (int) std::lround ((double) getHeight() * kBaseH / (double) baseH()) : getHeight();
    oto::saveUiSettings ({ oto::ui::isDark(), oto::ui::accentHue(), oto::ui::animationsEnabled(), getWidth(), h, false, true });
}

void OtoEditor::togglePanel()
{
    if (panel.isVisible()) { panel.setVisible (false); return; }
    panel.setTopLeftPosition (canvas.getWidth() - 16 - panel.getWidth(), 60);
    panel.setVisible (true);
    panel.toFront (true);
}

void OtoEditor::mouseDown (const juce::MouseEvent& e)
{
    // clicking anywhere in the editor (except in a text box) gives it the keyboard focus, so the shortcuts work
    if (isParentOf (e.eventComponent) && dynamic_cast<juce::TextEditor*> (e.eventComponent) == nullptr)
        if (! hasKeyboardFocus (false)) grabKeyboardFocus();

    if (menu.isVisible() && e.eventComponent != &menu && ! menu.isParentOf (e.eventComponent) && e.eventComponent != &bar)
        menu.hide();

    if (! panel.isVisible()) return;
    auto* c = e.eventComponent;
    if (c == &panel || panel.isParentOf (c) || c == &optionsBtn) return;
    panel.setVisible (false);
}

// ======================= OSC tabs and actions =======================
void OtoEditor::finishFade()
{
    if (fadeFrom < 0) return;
    pages[(size_t) fadeFrom]->setVisible (false);
    for (auto& pg : pages) pg->setAlpha (1.f);
    fadeFrom = -1;
    fadeT = 1.f;
}

// Cross-fade between two OSC pages (driven by the editor timer)
void OtoEditor::stepFade()
{
    if (fadeFrom < 0) return;
    fadeT += (1.f / 60.f) / 0.16f;
    if (fadeT >= 1.f) { finishFade(); return; }
    const float e = fadeT * fadeT * (3.f - 2.f * fadeT);   // smoothstep
    pages[(size_t) fadeFrom]->setAlpha (1.f - e);
    pages[(size_t) selected]->setAlpha (e);
}

void OtoEditor::selectOsc (int index)
{
    const int count = proc.getOscCount();
    finishFade();
    const int prev = selected;
    selected = juce::jlimit (0, count - 1, index);

    const bool fade = prev != selected && prev < count && oto::ui::animationsEnabled()
                      && pages[(size_t) prev]->isVisible() && isShowing();
    if (fade)
    {
        fadeFrom = prev;
        fadeT = 0.f;
        pages[(size_t) prev]->setAlpha (1.f);
        pages[(size_t) selected]->setAlpha (0.f);
        pages[(size_t) selected]->setVisible (true);
    }
    else
    {
        for (int i = 0; i < kMaxOsc; ++i)
            pages[(size_t) i]->setVisible (i == selected);
    }
    pages[(size_t) selected]->tick();
    shownName = proc.osc (selected).getSampleName();
    bar.setState (count, selected);
    canvas.repaint();
}

void OtoEditor::doAdd()
{
    const int n = proc.addOsc();
    if (n < 0) return;
    lastCount = proc.getOscCount();
    selectOsc (n);
}

void OtoEditor::flashStatus (const juce::String& text)
{
    status = text;
    statusUntil = juce::Time::getMillisecondCounter() + 1800;
    canvas.repaint (0, 56, canvas.getWidth(), 52);
    juce::Component::SafePointer<OtoEditor> safe (this);
    juce::Timer::callAfterDelay (1900, [safe]
    {
        if (safe != nullptr) safe->canvas.repaint (0, 56, safe->canvas.getWidth(), 52);
    });
}

// ---- actions on the shown OSC (menu and keyboard shortcuts) ----
void OtoEditor::copyCurrent()
{
    proc.copyOsc (selected);
    flashStatus ("Copied OSC " + juce::String (selected + 1));
}

void OtoEditor::pasteCurrent()
{
    if (! proc.hasClipboard()) { flashStatus ("Nothing copied yet"); return; }
    if (proc.pasteOsc (selected)) flashStatus ("Pasted into OSC " + juce::String (selected + 1));
}

void OtoEditor::duplicateCurrent()
{
    const int from = selected;
    const int n = proc.addOsc (from);
    if (n < 0) { flashStatus ("All 3 OSCs are in use"); return; }
    lastCount = proc.getOscCount();
    selectOsc (n);
    flashStatus ("OSC " + juce::String (from + 1) + " duplicated to OSC " + juce::String (n + 1));
}

void OtoEditor::deleteCurrent()
{
    const int idx = selected;
    if (! proc.removeOsc (idx)) { flashStatus ("The last OSC cannot be deleted"); return; }
    lastCount = proc.getOscCount();
    selectOsc (idx);
    flashStatus ("OSC " + juce::String (idx + 1) + " deleted");
}

// Ctrl+C copy, Ctrl+V paste, Ctrl+D duplicate, Delete key deletes (the OSC that is shown)
bool OtoEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    const juce::juce_wchar ch = k.getTextCharacter();
    auto is = [&] (char letter, int control)
    {
        return mods.isCommandDown() && ! mods.isShiftDown() && ! mods.isAltDown()
               && (code == (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) letter)
                   || code == (int) letter || (int) ch == control);
    };

    int idx = -1;
    if (is ('c', 3))       idx = 0;
    else if (is ('v', 22)) idx = 1;
    else if (is ('d', 4))  idx = 2;
#if JUCE_MAC
    else if (! mods.isCommandDown() && (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)) idx = 3;
#else
    else if (! mods.isCommandDown() && k == juce::KeyPress::deleteKey) idx = 3;
#endif
    if (idx < 0) return false;

    keyHandledAt = juce::Time::getMillisecondCounter();
    if (idx == heldIdx) return true;     // key repeat while the key is held: do it only once (Delete in particular)
    heldIdx = idx;
    switch (idx)
    {
        case 0: copyCurrent();      break;
        case 1: pasteCurrent();     break;
        case 2: duplicateCurrent(); break;
        default: deleteCurrent();   break;
    }
    return true;
}

// Reads the key state directly while the pointer is over the focused editor. Used when the host does not pass key
// events on. An action already done by keyPressed() is not repeated (keyHandledAt).
void OtoEditor::pollShortcuts()
{
    const auto now = juce::Time::getMillisecondCounter();
    auto* peer = getPeer();
    const bool active = peer != nullptr && peer->isFocused() && isMouseOver (true)
                        && ! menu.isVisible() && juce::Component::getCurrentlyModalComponent() == nullptr
                        && dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) == nullptr;
    const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();

    auto isWatchKeyDown = [&] (int i)
    {
        if (juce::KeyPress::isKeyCurrentlyDown (watch[(size_t) i].key))
            return true;
#if JUCE_MAC
        if (i == 3 && juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::backspaceKey))
            return true;
#endif
        return false;
    };

    if (heldIdx >= 0 && ! isWatchKeyDown (heldIdx))
        heldIdx = -1;                    // the key was released: the next press counts again

    for (size_t i = 0; i < watch.size(); ++i)
    {
        auto& w = watch[i];
        const bool isDelete = i == 3;
        const bool combo = isDelete ? ! mods.isCommandDown()
                                    : (mods.isCommandDown() && ! mods.isShiftDown() && ! mods.isAltDown());
        const bool down = active && combo && isWatchKeyDown ((int) i);
        if (down && ! w.down) { w.pending = true; w.edgeAt = now; }
        w.down = down;

        if (w.pending && now - w.edgeAt >= 60)       // wait a moment: a normal key event may arrive first
        {
            w.pending = false;
            if (keyHandledAt != 0 && (int) (keyHandledAt - w.edgeAt) >= -40) continue;
            switch (i)
            {
                case 0: copyCurrent();      break;
                case 1: pasteCurrent();     break;
                case 2: duplicateCurrent(); break;
                default: deleteCurrent();   break;
            }
        }
    }
}

// Right click on an OSC tab
void OtoEditor::showOscMenu (int index, juce::Point<int> posInBar)
{
    if (index < 0 || index >= proc.getOscCount()) return;
    selectOsc (index);

    const int count = proc.getOscCount();
    enum { kCopy = 1, kPaste, kDuplicate, kDelete };
#if JUCE_MAC
    const juce::String mod = "Cmd+";
#else
    const juce::String mod = "Ctrl+";
#endif
    using M = oto::ui::OscMenu;
    menu.show (canvas.getLocalBounds(), canvas.getLocalPoint (&bar, posInBar),
               { M::entry (kCopy, "Copy", mod + "C", true),
                 M::entry (kPaste, "Paste", mod + "V", proc.hasClipboard()),
                 M::divider(),
                 M::entry (kDuplicate, "Duplicate", mod + "D", count < kMaxOsc),
                 M::divider(),
                 M::entry (kDelete, "Delete", "Del", count > 1) });
}

void OtoEditor::timerCallback()
{
    const int count = proc.getOscCount();
    if (count != lastCount)
    {
        lastCount = count;
        selectOsc (selected);       // also handles a selection beyond the new count
    }

    stepFade();
    pollShortcuts();
    pages[(size_t) selected]->tick();

    keyboard.setRoot (pages[(size_t) selected]->getRootNote());

    const auto name = proc.osc (selected).getSampleName();
    if (name != shownName)
    {
        shownName = name;
        canvas.repaint (0, 0, canvas.getWidth(), 64);
    }
}

void OtoEditor::toggleKeyboard()
{
    const int oldBase = baseH();
    kbVisible = ! kbVisible;
    kbBtn.setActive (kbVisible);
    keyboard.setVisible (kbVisible);
    saveUi();
    // keep the scale of the cards: grow / shrink the window by the height of the keyboard
    const int newH = juce::jlimit (360, 2160, (int) std::lround ((double) getHeight() * baseH() / (double) oldBase));
    if (newH != getHeight()) setSize (getWidth(), newH);
    else resized();
}

void OtoEditor::toggleTheme()
{
    oto::ui::isDark() = ! oto::ui::isDark();
    saveUi();
    panel.sync();
    repaint();
}

bool OtoEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

// a dropped file goes into the OSC that is shown
void OtoEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) { pages[(size_t) selected]->loadFile (juce::File (f)); break; }
}

void OtoEditor::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float sc = juce::jmax (0.1f, juce::jmin (W / (float) kBaseW, H / (float) baseH()));
    const int vw = (int) std::ceil (W / sc), vh = (int) std::ceil (H / sc);
    canvas.setTransform (juce::AffineTransform::scale (sc));
    canvas.setBounds (0, 0, vw, vh);
    layoutCanvas (vw, vh);
}

void OtoEditor::layoutCanvas (int W, int H)
{
    const int M = 16;
    const int extra = juce::jmax (0, H - baseH());
    themeBtn.setBounds (W - M - 44, 12, 44, 44);
    optionsBtn.setBounds (W - M - 44 - 44, 12, 44, 44);
    kbBtn.setBounds (W - M - 44 * 3, 12, 44, 44);
    openBtn.setBounds (W - M - 44 * 3 - 8 - 132, 12, 132, 44);

    // OSC bar: tabs on the left, hint / status text on the right (see paintCanvas)
    {
        const int m = oto::ui::OscBar::kMargin;
        bar.setBounds (M - m, 62 - 4 - m + 4, 400 + 2 * m, 40 + 2 * m);   // the margin is empty space for the springs
    }

    for (auto& pg : pages)
    {
        pg->setBounds (0, 0, W, H);
        pg->layoutContent (W, extra);
    }

    const int rowB = 408 + kBarH + extra, rowH = 152;
    keyboard.setBounds (M, rowB + rowH + kKbGap, W - 2 * M, kKbH);
}

void OtoEditor::paint (juce::Graphics& g)
{
    g.fillAll (oto::ui::pal().bg);
}

void OtoEditor::paintCanvas (juce::Graphics& g)
{
    const auto& Pl = oto::ui::pal();
    g.fillAll (Pl.bg);

    // header: badge + title + sample name of the shown OSC
    const auto badge = juce::Rectangle<float> (18.f, 14.f, 40.f, 40.f);
    g.setColour (Pl.primaryContainer);
    g.fillEllipse (badge);
    g.setColour (Pl.onPrimaryContainer);
    {
        const float hs[5] = { 8.f, 16.f, 22.f, 13.f, 6.f };
        for (int i = 0; i < 5; ++i)
            g.fillRoundedRectangle (badge.getX() + 8.f + 5.5f * (float) i, badge.getCentreY() - hs[i] * 0.5f, 3.2f, hs[i], 1.6f);
    }

    g.setColour (Pl.onSurface);
    g.setFont (font (25.f, true));
    g.drawText ("Oto Sampler", juce::Rectangle<int> (68, 10, 220, 48), juce::Justification::centredLeft);

    const int nameX = 290, nameR = openBtn.getX() - 12;
    g.setColour (Pl.onSurfaceVariant);
    g.setFont (font (13.5f));
    g.drawText (shownName.isEmpty() ? juce::String ("OSC " + juce::String (selected + 1) + ": no sample loaded")
                                    : "OSC " + juce::String (selected + 1) + ": " + shownName,
                juce::Rectangle<int> (nameX, 10, juce::jmax (10, nameR - nameX), 48), juce::Justification::centredRight, true);

    // right side of the OSC bar: short status after a menu action, otherwise a hint
    {
        const bool showStatus = status.isNotEmpty() && juce::Time::getMillisecondCounter() < statusUntil;
        const auto r = juce::Rectangle<int> (440, 62, juce::jmax (10, canvas.getWidth() - 440 - 20), 40);
        g.setFont (font (12.5f, showStatus));
        g.setColour (showStatus ? Pl.primary : Pl.outline);
#if JUCE_MAC
        const juce::String hint ("Right-click an OSC tab, or Cmd+C  Cmd+V  Cmd+D  Del");
#else
        const juce::String hint ("Right-click an OSC tab, or Ctrl+C  Ctrl+V  Ctrl+D  Del");
#endif
        g.drawText (showStatus ? status : hint,
                    r, juce::Justification::centredRight, true);
    }
}
