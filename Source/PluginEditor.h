#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// The panel is drawn on a fixed 1360 x 900 design and scaled to the window,
// so it looks the same at any size. Colours and fonts follow the approved mockup.
namespace ui
{
namespace col
{
const juce::Colour ink { 0xff141012 }, paper { 0xffffffff }, mustard { 0xfff2b705 }, ochre { 0xffdfa000 }, yellow { 0xffffd23f },
    pink { 0xffff4fa3 }, pinkDark { 0xffb3165f }, sky { 0xff5ec8f2 }, cobalt { 0xff2343c9 }, lime { 0xff8ee000 }, red { 0xffe2283b },
    ice { 0xffbfeeff }, bone { 0xfff1dfbc }, grey { 0xff5a4f52 }, ledOff { 0xff4a4446 };
}

struct Fonts
{
    juce::Typeface::Ptr titan, archivo, marker;
    Fonts();
    juce::Font display (float h) const { return juce::Font (juce::FontOptions (titan).withHeight (h)); }
    juce::Font label (float h) const { return juce::Font (juce::FontOptions (archivo).withHeight (h)); }
    juce::Font hand (float h) const { return juce::Font (juce::FontOptions (marker).withHeight (h)); }
};
const Fonts& fonts();

juce::Path wobblyRect (juce::Rectangle<float> r, int seed, float radius = 10.0f);
juce::Path wobblyCircle (juce::Point<float> c, float radius, int seed, float jitter = 1.2f);
void drawBox (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill, int seed, float shadow = 6.0f, float border = 4.0f, float radius = 11.0f);
void drawHatch (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float spacing = 9.0f);
void chunkyText (juce::Graphics&, const juce::String&, const juce::Font&, juce::Rectangle<float>, juce::Colour fill, juce::Colour extrude,
                 int depth, float stroke, juce::Justification = juce::Justification::centredLeft);

// A chunky hand-drawn button
class Chunky : public juce::Button
{
public:
    Chunky (const juce::String& text = {}, juce::Colour fill = col::paper);
    juce::Colour fill, onFill, textColour = col::ink, onText = col::ink;
    float fontSize = 15.0f, shadow = 4.0f, border = 3.5f;
    bool displayFont = false, led = false, ledDim = false;   // ledDim: the off beat of a tempo flash
    juce::Justification just = juce::Justification::centred;
    int seed = 1;
    std::function<void()> onRightClick;
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;
};

class KnobLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// Knob with its label and value tag; right-click for MIDI Learn
class Knob : public juce::Component, private juce::Timer
{
public:
    Knob (KddProcessor&, const juce::String& paramId, const juce::String& label, juce::Colour fill, juce::Colour ink);
    ~Knob() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) showMenu(); }
    std::function<juce::String()> valueText;
    std::function<void (juce::PopupMenu&)> extraMenu;

private:
    void timerCallback() override;
    void showMenu();
    KddProcessor& proc;
    juce::String id, label, shown;
    juce::Colour fill, ink;
    juce::Slider slider;
    KnobLook look;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    bool learning = false;
};

// A painted read-out that refreshes itself when its text changes
class Readout : public juce::Component, private juce::Timer
{
public:
    Readout (std::function<juce::String()> get, std::function<void (juce::Graphics&, const juce::String&, juce::Rectangle<float>)> draw, int hz = 15);
    void paint (juce::Graphics& g) override { draw (g, text, getLocalBounds().toFloat()); }
    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && e.mouseWasClicked()) onClick(); }

private:
    void timerCallback() override;
    std::function<juce::String()> get;
    std::function<void (juce::Graphics&, const juce::String&, juce::Rectangle<float>)> draw;
    juce::String text;
};

// The big-eyed skull: blinks, rolls its eyes with the reverb, chatters its jaw with the
// delay repeats, freezes over, and pulls a face when bypassed
class Skull : public juce::Component, private juce::Timer
{
public:
    explicit Skull (KddProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    KddProcessor& proc;
    juce::Image cranium, jaw, ballL, ballR, irisL, irisR;
    double t = 0.0, nextBlink = 3.0, blinkStart = -10.0, lastFoot = 0.0, bubbleUntil = 0.0;
    float jawAngle = 0.0f, eyePhase = 0.0f, tailAmt = 0.0f, jump = 0.0f;
    int sayStamp = -1;
    juce::String bubble;
};

class LoopBar : public juce::Component, private juce::Timer
{
public:
    explicit LoopBar (KddProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    KddProcessor& proc;
};

class ExpressionBar : public juce::Component, private juce::Timer
{
public:
    explicit ExpressionBar (KddProcessor& p) : proc (p) { startTimerHz (20); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    KddProcessor& proc;
};

class MainPage;
class PedalPage;

class KddEditor : public juce::AudioProcessorEditor
{
public:
    explicit KddEditor (KddProcessor&);
    ~KddEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    void showPedalPage (bool show);
    void mouseDown (const juce::MouseEvent&) override;

private:
    KddProcessor& proc;
    std::unique_ptr<MainPage> mainPage;
    std::unique_ptr<PedalPage> pedalPage;
    juce::Component holder;
};
} // namespace ui

using KddEditor = ui::KddEditor;
