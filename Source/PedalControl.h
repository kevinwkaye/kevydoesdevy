#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <functional>
#include <vector>

// Footswitch and expression setup, adapted from Prism Patch's PedalControl.
// Nothing here names a brand: any MIDI footswitch works.
//
// Each footswitch can listen for several messages at once, so the plugin works out of the box
// with pedals that send different things:
//   A: CC 17 or CC 102 (any channel), or CC 20 on channel 16
//   B: CC 18 or CC 103 (any channel), or CC 21 on channel 16
//   C: CC 19 or CC 104 (any channel), or CC 22 on channel 16
//   D: CC 23 on channel 16
// Expression: CC 11, any channel. Teach my pedal and Learn replace any of them.
struct Binding
{
    enum Kind { CC = 0, Program, Note };
    int kind = CC;
    int channel = 0;     // 0 = any channel
    int number = 0;
    bool matches (const juce::MidiMessage& m, bool anyChannel) const;
    juce::String describe() const;
};

struct PedalSettings
{
    static constexpr int kSwitches = 4;
    std::array<std::vector<Binding>, kSwitches> fs;
    int expChannel = 0, expCC = 11;
    int heel = 0, toe = 127;     // calibrated ends; toe may be lower than heel
    int curve = 0;               // 0 linear, 1 slow start, 2 fast start
    juce::String footDevice;     // empty = find the footswitch automatically, "none" = off, else a device name
    struct CcMap { int channel = 0, cc = 0; juce::String param; };
    std::vector<CcMap> ccMaps;   // MIDI Learn on knobs and buttons

    PedalSettings() { resetFootswitches(); }
    void load();
    void save() const;
    void resetFootswitches();
    float expressionValue (int cc) const;
    static juce::File file();
};

// Turns press, release and one-shot trigger messages into taps, double taps, holds and combos.
// Some pedals send a message only when a switch is pressed (a trigger), so every hold job
// also has a double-tap. Runs on the message thread.
class FootswitchLogic
{
public:
    enum Event { Tap, DoubleTap, HoldStart, HoldEnd, Combo };
    // for Combo, sw is a bit mask of the two switches
    std::function<void (int sw, Event, double pressTime)> onEvent;

    static constexpr double kHoldSeconds = 0.45, kComboWindow = 0.11, kDoubleWindow = 0.42;

    void press (int sw, double now);
    void release (int sw, double now);
    void trigger (int sw, double now) { press (sw, now); release (sw, now); }
    void tick (double now);
    bool isDown (int sw) const { return down[(size_t) sw]; }

private:
    static constexpr int N = PedalSettings::kSwitches;
    std::array<bool, N> down {}, held {}, consumed {}, pending {};
    std::array<double, N> downAt {}, fireAt {}, lastTapAt { -10.0, -10.0, -10.0, -10.0 };
};

// Works out whether a CC switch is momentary (press and release) or sends one message per press,
// from what it actually sends. Momentary switches get real holds.
struct SwitchSense
{
    bool momentary = false;
    double lastHighAt = -10.0;
    bool lastWasHigh = false;
};

// Reads the footswitch's MIDI device directly, so every copy of the plugin hears the footswitches
// without routing a MIDI track to it. One per process, shared by all copies (from Prism Patch).
class DirectMidiInput : private juce::MidiInputCallback, private juce::Timer
{
public:
    DirectMidiInput();
    ~DirectMidiInput() override;

    using Sink = void (*) (const juce::MidiMessage&);
    void setSink (Sink s) { sink.store (s); }
    void setWanted (const juce::String& device);
    void refresh();

    bool connected() const { return isConnected.load(); }
    juce::String status() const;
    juce::StringArray openDevices() const;
    static juce::StringArray deviceNames();
    static bool looksLikeFootswitch (const juce::String& name);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override { refresh(); }

    std::atomic<Sink> sink { nullptr };
    std::atomic<bool> isConnected { false };
    mutable juce::CriticalSection lock;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    juce::StringArray busy;
    juce::String wanted;
};
