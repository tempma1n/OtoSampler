// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Common.h"

namespace oto
{
// UI preferences shared by every instance and every project.
// Stored in the user's application data folder (Windows: %APPDATA%\\OtoSampler\\OtoSampler.settings, macOS: ~/Library/Application Support/OtoSampler/OtoSampler.settings).
struct UiSettings
{
    bool  dark = false;
    float hue = -1.f;          // -1 = default accent, otherwise 0..1
    bool  animations = true;
    int   width = 1180, height = 624;   // editor size
    bool  keyboard = false;             // height was saved with the keyboard shown (written by older versions only)
    bool  keyboardKnown = false;        // true when the settings file has the keyboard entry
};

inline juce::PropertiesFile::Options uiSettingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "OtoSampler";
    o.filenameSuffix = ".settings";
    o.folderName = "OtoSampler";
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers = false;
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    return o;
}

inline UiSettings loadUiSettings()
{
    UiSettings s;
    s.dark = juce::Desktop::getInstance().isDarkModeActive();   // first launch: follow the system
    juce::PropertiesFile pf (uiSettingsOptions());
    if (pf.containsKey ("dark"))       s.dark = pf.getBoolValue ("dark", s.dark);
    if (pf.containsKey ("accentHue"))  s.hue = (float) pf.getDoubleValue ("accentHue", -1.0);
    if (pf.containsKey ("animations")) s.animations = pf.getBoolValue ("animations", true);
    s.width  = juce::jlimit (720, 3840, pf.getIntValue ("width", s.width));
    s.height = juce::jlimit (360, 2160, pf.getIntValue ("height", s.height));
    s.keyboardKnown = pf.containsKey ("keyboard");
    if (s.keyboardKnown) s.keyboard = pf.getBoolValue ("keyboard", false);
    return s;
}

inline void saveUiSettings (const UiSettings& s)
{
    juce::PropertiesFile pf (uiSettingsOptions());
    pf.setValue ("dark", s.dark);
    pf.setValue ("accentHue", (double) s.hue);
    pf.setValue ("animations", s.animations);
    pf.setValue ("width", s.width);
    pf.setValue ("height", s.height);
    pf.setValue ("keyboard", s.keyboard);
    pf.saveIfNeeded();
}
}
