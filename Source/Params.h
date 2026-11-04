#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>

// Every parameter in one list, so the host, presets, MIDI Learn and footswitches all see the same set.
namespace ParamIDs
{
// per delay module: prefix "a" or "b"
inline juce::String d (const char* module, const char* name) { return juce::String (module) + name; }

#define KDD_ID(x) inline constexpr const char* x = #x;
KDD_ID (aOn) KDD_ID (aType) KDD_ID (aTime) KDD_ID (aFeedback) KDD_ID (aMix) KDD_ID (aTone) KDD_ID (aLowCut) KDD_ID (aDuck)
KDD_ID (aVoice) KDD_ID (aToneMode) KDD_ID (aSubdiv) KDD_ID (aPing) KDD_ID (aInfinite)
KDD_ID (bOn) KDD_ID (bType) KDD_ID (bTime) KDD_ID (bFeedback) KDD_ID (bMix) KDD_ID (bTone) KDD_ID (bLowCut) KDD_ID (bDuck)
KDD_ID (bVoice) KDD_ID (bToneMode) KDD_ID (bSubdiv) KDD_ID (bPing) KDD_ID (bInfinite)
KDD_ID (rOn) KDD_ID (rType) KDD_ID (rDecay) KDD_ID (rPreDelay) KDD_ID (rMix) KDD_ID (rShimmer) KDD_ID (rTone) KDD_ID (rLowCut)
KDD_ID (rVoice) KDD_ID (rToneMode) KDD_ID (rFreeze)
KDD_ID (route) KDD_ID (loopPos) KDD_ID (loopLevel) KDD_ID (trails) KDD_ID (merge) KDD_ID (killDry)
KDD_ID (eqOn) KDD_ID (eqDry) KDD_ID (eqLow) KDD_ID (eqMid) KDD_ID (eqHigh)
KDD_ID (bpm) KDD_ID (syncHost) KDD_ID (bypassAll) KDD_ID (expTarget)
#undef KDD_ID
} // namespace ParamIDs

namespace names
{
inline const juce::StringArray& typesA() { static juce::StringArray s { "DIGITAL", "ANALOG", "TAPE", "ECHO", "LIQUID", "RAINBOW", "CRYSTAL", "LOW-BIT", "FUZZY" }; return s; }
inline const juce::StringArray& typesB() { static juce::StringArray s { "DIGITAL", "ANALOG", "TAPE", "REAL", "DYNA", "GALAXY", "MOD", "TRI-MOD", "MOD-INV" }; return s; }
inline const juce::StringArray& typesR() { static juce::StringArray s { "ROOM", "HALL", "PLATE", "DIST-VERB", "FL-VERB", "FILTER", "REVERSE", "SPRING", "MOD" }; return s; }
inline const juce::StringArray& subdivs() { static juce::StringArray s { "FREE", "1/4", "1/8.", "1/8", "1/8T", "1/16", "1/2" }; return s; }
inline float subdivBeats (int i) { static const float f[7] = { 0.0f, 1.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f, 2.0f }; return f[juce::jlimit (0, 6, i)]; }
inline const juce::StringArray& routes() { static juce::StringArray s { "A > B > VERB", "A + B > VERB", "VERB > A + B", "A + B + VERB" }; return s; }
inline const juce::StringArray& loopPositions() { static juce::StringArray s { "PRE", "MID", "POST" }; return s; }

struct ExpTarget { const char* label; std::vector<const char*> ids; };
inline const std::vector<ExpTarget>& expTargets()
{
    static std::vector<ExpTarget> t {
        { "FEEDBACK A+B", { ParamIDs::aFeedback, ParamIDs::bFeedback } },
        { "FEEDBACK A", { ParamIDs::aFeedback } },
        { "FEEDBACK B", { ParamIDs::bFeedback } },
        { "TIME A", { ParamIDs::aTime } },
        { "TIME B", { ParamIDs::bTime } },
        { "MIX A", { ParamIDs::aMix } },
        { "MIX B", { ParamIDs::bMix } },
        { "REVERB MIX", { ParamIDs::rMix } },
        { "DECAY", { ParamIDs::rDecay } },
        { "SHIMMER", { ParamIDs::rShimmer } },
        { "LOOPER LEVEL", { ParamIDs::loopLevel } },
    };
    return t;
}
inline juce::StringArray expTargetNames() { juce::StringArray s; for (auto& t : expTargets()) s.add (t.label); return s; }
} // namespace names

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using namespace ParamIDs;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = [] (const String& id, const String& name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; }));
    };
    auto ranged = [] (const String& id, const String& name, float lo, float hi, float centre, float def, const String& unit)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                      AudioParameterFloatAttributes().withLabel (unit)
                                                          .withStringFromValueFunction ([unit] (float v, int) { return String (roundToInt (v)) + " " + unit; }));
    };
    auto onoff = [] (const String& id, const String& name, bool def) { return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def); };
    auto choice = [] (const String& id, const String& name, const StringArray& items, int def) { return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def); };

    const StringArray voices { "MK II", "MK I" }, toneModes { "LPF", "HPF" };
    struct D { const char* p; const char* n; int type; float time, fb, mix, tone, duck; bool on; int sub; bool ping; };
    for (auto m : { D { "a", "Delay A", 2, 420.0f, 0.45f, 0.4f, 0.55f, 0.15f, true, 0, false },
                    D { "b", "Delay B", 5, 380.0f, 0.4f, 0.32f, 0.6f, 0.3f, false, 2, true } })
    {
        const String p (m.p), n (m.n);
        layout.add (onoff (p + "On", n + " On", m.on));
        layout.add (choice (p + "Type", n + " Type", p == "a" ? names::typesA() : names::typesB(), m.type));
        layout.add (ranged (p + "Time", n + " Time", 10.0f, 2000.0f, 400.0f, m.time, "ms"));
        layout.add (pct (p + "Feedback", n + " Feedback", m.fb));
        layout.add (pct (p + "Mix", n + " Mix", m.mix));
        layout.add (pct (p + "Tone", n + " Tone", m.tone));
        layout.add (ranged (p + "LowCut", n + " Low Cut", 20.0f, 500.0f, 120.0f, 120.0f, "Hz"));
        layout.add (pct (p + "Duck", n + " Duck", m.duck));
        layout.add (choice (p + "Voice", n + " Voicing", voices, 0));
        layout.add (choice (p + "ToneMode", n + " Tone Mode", toneModes, 0));
        layout.add (choice (p + "Subdiv", n + " Subdivision", names::subdivs(), m.sub));
        layout.add (onoff (p + "Ping", n + " Ping-Pong", m.ping));
        layout.add (onoff (p + "Infinite", n + " Infinite", false));
    }
    layout.add (onoff (rOn, "Reverb On", true));
    layout.add (choice (rType, "Reverb Type", names::typesR(), 1));
    layout.add (pct (rDecay, "Decay", 0.55f));
    layout.add (ranged (rPreDelay, "Pre-Delay", 0.0f, 500.0f, 80.0f, 40.0f, "ms"));
    layout.add (pct (rMix, "Reverb Mix", 0.36f));
    layout.add (pct (rShimmer, "Shimmer", 0.0f));
    layout.add (pct (rTone, "Reverb Tone", 0.55f));
    layout.add (ranged (rLowCut, "Reverb Low Cut", 20.0f, 500.0f, 150.0f, 180.0f, "Hz"));
    layout.add (choice (rVoice, "Reverb Voicing", voices, 0));
    layout.add (choice (rToneMode, "Reverb Tone Mode", toneModes, 0));
    layout.add (onoff (rFreeze, "Freeze", false));

    layout.add (choice (route, "Routing", names::routes(), 0));
    layout.add (choice (loopPos, "Looper Position", names::loopPositions(), 2));
    layout.add (pct (loopLevel, "Looper Level", 0.75f));
    layout.add (onoff (trails, "Trails", true));
    layout.add (onoff (merge, "Merge", true));
    layout.add (onoff (killDry, "Kill Dry", false));
    layout.add (onoff (eqOn, "Global EQ", false));
    layout.add (onoff (eqDry, "EQ On Dry Too", false));
    for (auto [id, n] : { std::pair<const char*, const char*> { eqLow, "EQ Low" }, { eqMid, "EQ Mid" }, { eqHigh, "EQ High" } })
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, n, NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bpm, 1 }, "Tempo", NormalisableRange<float> (40.0f, 240.0f, 0.1f), 112.0f,
                                                       AudioParameterFloatAttributes().withLabel ("BPM")));
    layout.add (onoff (syncHost, "Sync To Host Tempo", false));
    layout.add (onoff (bypassAll, "Bypass All", false));
    layout.add (choice (expTarget, "Expression Target", names::expTargetNames(), 0));
    return layout;
}

// Factory presets: 8 banks of 3. Each lists only what differs from the defaults.
struct FactoryPreset { const char* name; const char* note; std::map<juce::String, float> values; };
inline const std::vector<FactoryPreset>& factoryPresets()
{
    using namespace ParamIDs;
    static std::vector<FactoryPreset> p {
        { "BILGE PUMP", "tape into hall, tight lows", { { aType, 2 }, { aTime, 420 }, { aFeedback, 0.45f }, { bOn, 0 }, { rType, 1 }, { rDecay, 0.55f } } },
        { "SEWER CHOIR", "galaxy wash with shimmer", { { aOn, 0 }, { bOn, 1 }, { bType, 5 }, { bSubdiv, 2 }, { bFeedback, 0.5f }, { bMix, 0.38f }, { rDecay, 0.8f }, { rShimmer, 0.45f }, { rMix, 0.45f } } },
        { "DROOL POOL", "dotted eighths, dyna duck", { { aType, 0 }, { aSubdiv, 2 }, { aFeedback, 0.35f }, { bOn, 1 }, { bType, 4 }, { bSubdiv, 1 }, { rType, 0 }, { rMix, 0.2f } } },

        { "GUTTER SURF", "drum echo into spring", { { aType, 3 }, { aTime, 300 }, { aFeedback, 0.4f }, { bOn, 0 }, { rType, 7 }, { rDecay, 0.5f }, { rMix, 0.45f } } },
        { "BLOODSHOT TIDE", "MK I grit, fuzzy repeats", { { aType, 8 }, { aVoice, 1 }, { aFeedback, 0.55f }, { aTime, 360 }, { bOn, 0 }, { rType, 2 }, { rVoice, 1 } } },
        { "PUKE TIDE", "liquid phaser, mod verb", { { aType, 4 }, { bOn, 1 }, { bType, 6 }, { bSubdiv, 3 }, { rType, 8 }, { rDecay, 0.6f } } },

        { "WALL OF SLUDGE", "the big Devy wall", { { aType, 2 }, { aSubdiv, 1 }, { aFeedback, 0.5f }, { bOn, 1 }, { bType, 1 }, { bSubdiv, 2 }, { bPing, 1 }, { route, 1 }, { rDecay, 0.85f }, { rShimmer, 0.3f }, { rMix, 0.5f } } },
        { "ROTTEN RAINBOW", "fifths climbing up", { { aType, 5 }, { aFeedback, 0.5f }, { aMix, 0.35f }, { bOn, 0 }, { rType, 1 } } },
        { "CRYSTAL SKULL", "octave repeats, plate", { { aType, 6 }, { aFeedback, 0.45f }, { bOn, 0 }, { rType, 2 }, { rShimmer, 0.25f } } },

        { "BACKWARDS BURP", "reverse delay, reverse verb", { { aOn, 0 }, { bOn, 1 }, { bType, 8 }, { bTime, 600 }, { bSubdiv, 0 }, { rType, 6 }, { rDecay, 0.45f } } },
        { "TRI-MOD MULLET", "80s chorus repeats", { { aOn, 0 }, { bOn, 1 }, { bType, 7 }, { bSubdiv, 2 }, { rType, 0 } } },
        { "LO-FI LUNCH", "bit crushed, flanged verb", { { aType, 7 }, { aFeedback, 0.5f }, { bOn, 0 }, { rType, 4 } } },

        { "STINK FILTER", "sweeping filter verb", { { aType, 0 }, { aMix, 0.25f }, { bOn, 0 }, { rType, 5 }, { rMix, 0.5f }, { rDecay, 0.7f } } },
        { "GRAVY SPRING", "spring on its own", { { aOn, 0 }, { bOn, 0 }, { rType, 7 }, { rMix, 0.5f } } },
        { "DIRTY BATHWATER", "analog into dist-verb", { { aType, 1 }, { bOn, 0 }, { rType, 3 }, { rMix, 0.3f } } },

        { "SLAP CHOP", "short slapback, small room", { { aType, 0 }, { aTime, 95 }, { aFeedback, 0.1f }, { aMix, 0.45f }, { bOn, 0 }, { rType, 0 }, { rMix, 0.25f } } },
        { "DUB SNOT", "dark tape, long feedback", { { aType, 2 }, { aSubdiv, 1 }, { aFeedback, 0.68f }, { aTone, 0.32f }, { bOn, 1 }, { bType, 1 }, { bSubdiv, 2 }, { rDecay, 0.6f } } },
        { "LOOP SOUP", "looper first, effects over it", { { aType, 0 }, { aFeedback, 0.3f }, { bOn, 0 }, { rType, 2 }, { loopPos, 0 } } },

        { "FROZEN GOB", "huge hall, ready to freeze", { { aOn, 0 }, { bOn, 1 }, { bType, 5 }, { rDecay, 0.95f }, { rMix, 0.5f } } },
        { "FLANGED FLAN", "flanged reverb wash", { { aOn, 0 }, { bOn, 0 }, { rType, 4 }, { rDecay, 0.75f }, { rMix, 0.5f } } },
        { "GOBLIN CHOIR", "mod verb, heavy shimmer", { { aOn, 0 }, { bOn, 0 }, { rType, 8 }, { rDecay, 0.85f }, { rShimmer, 0.7f }, { rMix, 0.5f } } },

        { "OG BILGE", "MK I: tape into hall", { { aType, 2 }, { aVoice, 1 }, { bOn, 0 }, { bVoice, 1 }, { rVoice, 1 }, { rType, 1 } } },
        { "OG SEWER", "MK I: galaxy and shimmer", { { aOn, 0 }, { aVoice, 1 }, { bOn, 1 }, { bType, 5 }, { bVoice, 1 }, { rVoice, 1 }, { rShimmer, 0.5f }, { rDecay, 0.8f } } },
        { "OG FUZZBALL", "MK I: fuzzy into dist-verb", { { aType, 8 }, { aVoice, 1 }, { aFeedback, 0.5f }, { bOn, 0 }, { bVoice, 1 }, { rType, 3 }, { rVoice, 1 } } },
    };
    return p;
}
