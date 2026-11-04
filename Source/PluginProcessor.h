#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "PedalControl.h"
#include "engine/Engine.h"

class KddProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    KddProcessor();
    ~KddProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kevy does Devy"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- footswitch modes, the pedal's own: Play, Tap, Looper, Patch
    enum Mode { ModePlay = 0, ModeTap, ModeLooper, ModePatch };
    int getMode() const { return mode.load(); }
    void setMode (int m);
    static juce::String modeName (int m);
    // what each footswitch does in a mode: tap job and the extra (hold, double tap)
    static std::pair<juce::String, juce::String> jobFor (int mode, int sw);

    // ---- every footswitch job, for the on-screen buttons too
    void toggleModule (int module);              // 0 A, 1 B, 2 reverb
    void setInfinite (int module, bool on);      // reverb: freeze
    bool isInfinite (int module) const;
    void tapMaster (double when = -1.0);
    void tapDelay (int module, double when = -1.0);
    void looperCommand (kdd::Looper::Command c);
    void stepPreset (int direction);
    void toggleBypassAll();

    // ---- presets: 10 banks of 3 (banks 1 to 8 as on the pedal)
    static constexpr int kSlots = 30;
    int getSlot() const { return slot.load(); }
    juce::String slotName (int s) const;
    juce::String slotNote (int s) const;
    void loadSlot (int s);
    void saveSlot (int s);

    // ---- looper state for the panel
    kdd::Looper& looper() { return engine.looper; }

    // ---- skull and panel feedback
    float wetLevel (int module) const { return uiWet[(size_t) module].load(); }
    float inputLevel() const { return uiIn.load(); }
    int footDownMask() const { return uiFootDown.load(); }
    float expressionPosition() const { return uiExp.load(); }
    double lastFootAt() const { return lastFoot.load(); }
    juce::String latestMessage (int& stamp) const;
    void say (const juce::String& text);

    // ---- pedal setup
    PedalSettings& pedals() { return settings; }
    void savePedals();
    juce::String footInputStatus() const { return direct->status(); }
    void setFootDevice (const juce::String& device);
    void startTeach();
    int teachStep() const { return teach.load(); }   // 0 off, 1..3 waiting for A, B, C
    enum { LearnNone = -1, LearnExpression = 10, LearnParam = 20 };
    void startLearn (int what, const juce::String& paramId = {});
    void cancelLearn() { learnTarget.store (LearnNone); }
    int learning() const { return learnTarget.load(); }
    juce::String learningParam() const { return learnParamId; }
    juce::String midiLearnFor (const juce::String& paramId) const;
    void clearMidiLearn (const juce::String& paramId);
    void startCalibration();
    bool isCalibrating() const { return calibrating; }

    void handleDirectMidi (const juce::MidiMessage&);   // MIDI thread
    void takeFootswitches();                            // this copy answers the footswitches
    bool pedalsDirect() const { return direct->connected(); }
    bool isTeachingOrLearning() const { return teach.load() > 0 || learnTarget.load() != LearnNone; }

    float getParam (const char* id) const { return raw.at (id)->load(); }
    void setParamPlain (const juce::String& id, float plain);
    void setParamNorm (const juce::String& id, float norm);

private:
    void timerCallback() override;
    kdd::EngineParams readParams();
    void handleMidi (const juce::MidiMessage&, bool fromDevice);
    void onFootEvent (int sw, FootswitchLogic::Event, double pressTime);
    void footCC (int sw, int value, double t);
    void applyExpression (int cc);
    void initSlots();
    void captureSlot (juce::ValueTree& s);

    enum Kind { FootCCKind, FootTrigger, FootPress, FootRelease, ExpressionKind, Learned, CcMapKind, ProgramKind };
    struct Pending { int kind = 0, a = 0, b = 0; double t = 0.0; };
    void queue (int kind, int a, int b);

    std::map<juce::String, std::atomic<float>*> raw;
    kdd::Engine engine;
    double sampleRateNow = 48000.0;
    std::atomic<double> hostBpm { 0.0 };

    std::atomic<int> mode { ModePlay }, slot { 0 }, teach { 0 }, learnTarget { LearnNone };
    juce::String learnParamId;
    std::array<bool, 3> infLatched {};
    std::array<double, 3> lastTapAt { -10.0, -10.0, -10.0 };
    std::array<std::vector<double>, 3> tapIntervals;

    juce::ValueTree slots { "Slots" };

    // MIDI from the audio thread and from the footswitch device, applied on the message thread
    juce::AbstractFifo fifo { 512 }, directFifo { 512 };
    std::array<Pending, 512> pending, directPending;
    juce::SpinLock directWriteLock;
    juce::SharedResourcePointer<DirectMidiInput> direct;
    std::atomic<bool> hostFeedsPedals { false };

    PedalSettings settings;
    FootswitchLogic footswitches;
    std::array<SwitchSense, PedalSettings::kSwitches> sense;
    juce::Time settingsStamp;
    double nextSettingsCheck = 0.0;

    bool calibrating = false;
    double calibrateUntil = 0.0;
    int calLo = 127, calHi = 0, calFirst = -1, calLast = -1;

    std::array<std::atomic<float>, 3> uiWet {};
    std::atomic<float> uiIn { 0.0f }, uiExp { 0.0f };
    std::atomic<int> uiFootDown { 0 };
    std::atomic<double> lastFoot { 0.0 };
    juce::SpinLock sayLock;
    juce::String sayText;
    int sayStamp = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KddProcessor)
};
