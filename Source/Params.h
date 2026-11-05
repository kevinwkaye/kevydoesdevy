#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <string>
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

// Presets: 10 banks of 3. Each lists only what differs from the defaults.
// Banks 1 to 8 are the Ocean Machine II factory presets, from photos of the pedal's screen
// (reverb type, both delay types and times, and which sections are off; knob settings aren't
// shown there, so those keep the bass-friendly defaults). Bank 9 is bass picks, bank 10 MK I.
struct FactoryPreset { const char* name; juce::String note; std::map<juce::String, float> values; };
inline const std::vector<FactoryPreset>& factoryPresets()
{
    using namespace ParamIDs;
    // reverb type, Delay A type, Delay B type, Delay A ms, Delay B ms (Free time, all on)
    auto om = [] (const char* name, int r, int a, int b, float aMs, float bMs, bool reverbOn = true, bool delaysOn = true) -> FactoryPreset
    {
        static const char* rn[] = { "room", "hall", "plate", "dist-verb", "fl-verb", "filter", "reverse", "spring", "mod" };
        static const char* an[] = { "digital", "analog", "tape", "echo", "liquid", "rainbow", "crystal", "low-bit", "fuzzy" };
        static const char* bn[] = { "digital", "analog", "tape", "real", "dyna", "galaxy", "mod", "tri-mod", "mod-inv" };
        const juce::String note = juce::String (reverbOn ? rn[r] : "no verb") + ", " + (delaysOn ? juce::String (an[a]) + " + " + bn[b] : juce::String ("no delays"));
        return { name, note,
                 { { rType, (float) r }, { rOn, reverbOn ? 1.0f : 0.0f }, { aType, (float) a }, { bType, (float) b },
                   { aOn, delaysOn ? 1.0f : 0.0f }, { bOn, delaysOn ? 1.0f : 0.0f }, { aSubdiv, 0 }, { bSubdiv, 0 }, { aTime, aMs }, { bTime, bMs }, { bPing, 0 } } };
    };
    static std::vector<FactoryPreset> p = [&]
    {
        std::vector<FactoryPreset> v;
        v.reserve (30);
        // bank 1
        v.push_back (om ("DEVY 1A", 1, 1, 0, 859, 400));
        v.push_back (om ("DEVY 1B", 1, 2, 0, 940, 540));
        v.push_back (om ("DEVY 1C", 1, 8, 5, 940, 540));
        // bank 2
        v.push_back (om ("DEVY 2A", 1, 0, 4, 422, 839));
        v.push_back (om ("DEVY 2B", 5, 1, 4, 218, 424));
        v.push_back (om ("DEVY 2C", 2, 4, 2, 400, 540));
        // bank 3
        v.push_back (om ("DEVY 3A", 2, 2, 0, 400, 800));
        v.push_back (om ("DEVY 3B", 3, 4, 2, 400, 800));
        v.push_back (om ("DEVY 3C", 4, 0, 0, 465, 465, true, false));
        // bank 4
        v.push_back (om ("DEVY 4A", 6, 7, 3, 781, 520));
        v.push_back (om ("DEVY 4B", 1, 6, 1, 970, 124, false));
        v.push_back (om ("DEVY 4C", 1, 1, 8, 408, 817));
        // bank 5
        v.push_back (om ("DEVY 5A", 0, 2, 3, 176, 705));
        v.push_back (om ("DEVY 5B", 2, 3, 5, 176, 470));
        v.push_back (om ("DEVY 5C", 1, 2, 0, 623, 311));
        // bank 6
        v.push_back (om ("DEVY 6A", 8, 6, 3, 163, 478));
        v.push_back (om ("DEVY 6B", 5, 5, 1, 204, 520));
        v.push_back (om ("DEVY 6C", 0, 0, 0, 652, 869));
        // bank 7
        v.push_back (om ("DEVY 7A", 3, 8, 3, 672, 2000));
        v.push_back (om ("DEVY 7B", 6, 4, 2, 336, 1014));
        v.push_back (om ("DEVY 7C", 1, 4, 5, 1982, 668));
        // bank 8
        v.push_back (om ("DEVY 8A", 6, 3, 2, 1983, 670));
        v.push_back (om ("DEVY 8B", 0, 3, 2, 969, 672));
        v.push_back (om ("DEVY 8C", 3, 8, 5, 675, 2000));
        // bank 9: bass picks
        v.push_back ({ "BILGE PUMP", "tape into hall, tight lows", { { aType, 2 }, { aTime, 420 }, { aFeedback, 0.45f }, { bOn, 0 }, { rType, 1 }, { rDecay, 0.55f } } });
        v.push_back ({ "WALL OF SLUDGE", "the big Devy wall", { { aType, 2 }, { aSubdiv, 1 }, { aFeedback, 0.5f }, { bOn, 1 }, { bType, 1 }, { bSubdiv, 2 }, { bPing, 1 }, { route, 1 }, { rDecay, 0.85f }, { rShimmer, 0.3f }, { rMix, 0.5f } } });
        v.push_back ({ "DUB SNOT", "dark tape, long feedback", { { aType, 2 }, { aSubdiv, 1 }, { aFeedback, 0.68f }, { aTone, 0.32f }, { bOn, 1 }, { bType, 1 }, { bSubdiv, 2 }, { rDecay, 0.6f } } });
        // bank 10: MK I
        v.push_back ({ "OG BILGE", "MK I: tape into hall", { { aType, 2 }, { aVoice, 1 }, { bOn, 0 }, { bVoice, 1 }, { rVoice, 1 }, { rType, 1 } } });
        v.push_back ({ "OG SEWER", "MK I: galaxy and shimmer", { { aOn, 0 }, { aVoice, 1 }, { bOn, 1 }, { bType, 5 }, { bVoice, 1 }, { rVoice, 1 }, { rShimmer, 0.5f }, { rDecay, 0.8f } } });
        v.push_back ({ "OG FUZZBALL", "MK I: fuzzy into dist-verb", { { aType, 8 }, { aVoice, 1 }, { aFeedback, 0.5f }, { bOn, 0 }, { bVoice, 1 }, { rType, 3 }, { rVoice, 1 } } });
        return v;
    }();
    return p;
}
