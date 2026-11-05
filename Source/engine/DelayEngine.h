#pragma once

// One Ocean Machine delay module (Delay A or Delay B): nine types each, two seconds of
// time, feedback up to controlled self-oscillation, Tone as low-pass or high-pass,
// a low cut inside the loop for bass, ducking, infinite hold and MK I / MK II voicing.
// The module returns wet signal only; the caller adds it to the dry.

#include "Blocks.h"
#include <array>

namespace kdd
{
enum class DelayType
{
    Digital = 0, Analog, Tape,
    // Delay A only
    Echo, Liquid, Rainbow, Crystal, LowBit, Fuzzy,
    // Delay B only
    Real, Dyna, Galaxy, Mod, TriMod, ModInv
};

// The pedal's rotary positions: A and B share the first three
inline DelayType delayTypeFor (int module, int position)
{
    static constexpr DelayType a[9] = { DelayType::Digital, DelayType::Analog, DelayType::Tape, DelayType::Echo, DelayType::Liquid,
                                        DelayType::Rainbow, DelayType::Crystal, DelayType::LowBit, DelayType::Fuzzy };
    static constexpr DelayType b[9] = { DelayType::Digital, DelayType::Analog, DelayType::Tape, DelayType::Real, DelayType::Dyna,
                                        DelayType::Galaxy, DelayType::Mod, DelayType::TriMod, DelayType::ModInv };
    position = std::min (8, std::max (0, position));
    return module == 0 ? a[position] : b[position];
}

struct DelayParams
{
    bool on = true;            // input open; when off the repeats ring out (trails)
    int position = 0;          // 0..8 on the module's rotary
    float timeMs = 400.0f;
    float feedback = 0.4f;     // knob 0..1
    float tone = 0.6f;         // knob 0..1
    int toneMode = 0;          // 0 low-pass, 1 high-pass
    float lowCutHz = 120.0f;
    float duck = 0.0f;         // 0..1
    bool mk1 = false;
    bool infinite = false;
    bool pingPong = false;
};

class DelayEngine
{
public:
    void prepare (float sampleRate, int moduleIndex)
    {
        fs = sampleRate;
        module = moduleIndex;
        const int maxSamples = (int) (2.3f * fs) + 64;
        for (auto& l : line) l.init (nextPow2 (maxSamples));
        for (int c = 0; c < 2; ++c)
        {
            shifter[(size_t) c].prepare (fs, 0.1f);
            reverse[(size_t) c].prepare ((int) (2.1f * fs));
            phaser[(size_t) c].prepare (fs);
            grit[(size_t) c].prepare (fs);
            dc[(size_t) c].prepare (fs);
        }
        duckEnv.prepare (fs, 4.0f, 260.0f);
        swellEnv.prepare (fs, 300.0f, 900.0f);
        timeSm.setTime (0.08f, fs);
        tapeSm.setTime (0.45f, fs);
        inGate.setTime (0.02f, fs);
        fbSm.setTime (0.03f, fs);
        wetEnv.prepare (fs, 2.0f, 60.0f);
        for (auto& h : pitchHp) h.b.highpass (150.0f, 0.7071f, fs);
        reset();
    }

    void reset()
    {
        for (auto& l : line) l.clear();
        for (int c = 0; c < 2; ++c)
        {
            shifter[(size_t) c].clear(); reverse[(size_t) c].clear(); phaser[(size_t) c].reset();
            grit[(size_t) c].reset(); dc[(size_t) c].reset(); tone[(size_t) c].reset(); lowCut[(size_t) c].reset();
            sat[(size_t) c].reset(); crush[(size_t) c].reset(); inCut[(size_t) c].reset();
            fbL[(size_t) c] = 0.0f;
        }
        duckEnv.reset(); swellEnv.reset(); wetEnv.reset();
        lfo = 0.0f; lfo2 = 0.0f;
    }

    // Clears the repeats, used when the type changes with Merge off
    void clearTails() { for (auto& l : line) l.clear(); for (auto& f : fbL) f = 0.0f; }

    // Process one stereo sample. dryForDuck is the player's dry signal, so ducking follows the playing.
    void process (float inL, float inR, float dryForDuck, const DelayParams& p, float& outL, float& outR)
    {
        const DelayType type = delayTypeFor (module, p.position);
        const bool slowTime = type == DelayType::Tape || type == DelayType::Analog || type == DelayType::Echo;
        const float target = clampf (p.timeMs, 10.0f, 2000.0f) * 0.001f * fs;
        float tSamples = slowTime ? tapeSm.next (target) : timeSm.next (target);
        if (slowTime) timeSm.next (target); else tapeSm.next (target);

        const float gate = inGate.next (p.on && ! p.infinite ? 1.0f : 0.0f);
        float fb = fbSm.next (p.infinite ? 1.0f : clampf (p.feedback, 0.0f, 1.0f) * 1.08f);

        // modulation
        lfo += (type == DelayType::Tape ? 0.6f : 0.8f) / fs;
        if (lfo >= 1.0f) lfo -= 1.0f;
        lfo2 += 5.7f / fs;
        if (lfo2 >= 1.0f) lfo2 -= 1.0f;
        const float s1 = std::sin (kTwoPi * lfo), s2 = std::sin (kTwoPi * lfo2);

        float modSamples = 0.0f;
        switch (type)
        {
            case DelayType::Tape:    modSamples = 0.0011f * fs * s1 + 0.00012f * fs * s2; break;
            case DelayType::Analog:  modSamples = 0.0003f * fs * s1; break;
            case DelayType::Mod:     modSamples = 0.0022f * fs * s1; break;
            case DelayType::Galaxy:  modSamples = 0.0009f * fs * s1; break;
            case DelayType::Rainbow: modSamples = 0.0016f * fs * s1; break;
            default: break;
        }

        // Galaxy: repeats swell in behind the note
        float swellGain = 1.0f;
        if (type == DelayType::Galaxy)
        {
            const float e = swellEnv.process (0.5f * (inL + inR));
            swellGain = clampf (e * 6.0f, 0.0f, 1.0f);
        }

        // input low cut keeps the sub out of every repeat
        for (int c = 0; c < 2; ++c) { inCut[(size_t) c].set (p.lowCutHz, fs); lowCut[(size_t) c].set (std::max (20.0f, p.lowCutHz * (0.6f + 0.4f * clampf (p.feedback, 0.0f, 1.0f))), fs); tone[(size_t) c].set (p.tone, p.toneMode, fs); }

        float in[2] = { inCut[0].process (inL) * gate, inCut[1].process (inR) * gate };
        if (type == DelayType::Galaxy) { in[0] *= swellGain; in[1] *= swellGain; }

        float wet[2];
        for (int c = 0; c < 2; ++c)
        {
            // R reads slightly offset in modulated types, for width
            const float m = c == 0 ? modSamples : -modSamples;
            float y;
            if (type == DelayType::Echo)
            {
                // three playback heads, like a magnetic drum echo
                y = 0.22f * line[(size_t) c].tap (tSamples * 0.333f + m) + 0.3f * line[(size_t) c].tap (tSamples * 0.667f + m)
                    + 0.48f * line[(size_t) c].tap (tSamples + m);
            }
            else if (type == DelayType::TriMod)
            {
                const float d = 0.0018f * fs;
                y = 0.0f;
                for (int v = 0; v < 3; ++v)
                    y += line[(size_t) c].tap (tSamples + d * std::sin (kTwoPi * lfo + (float) v * 2.094f + (float) c));
                y *= 0.42f;
            }
            else if (type == DelayType::ModInv)
            {
                y = reverse[(size_t) c].process (line[(size_t) c].tap (8.0f + 0.0012f * fs * (1.0f + s1)), tSamples);
            }
            else
                y = line[(size_t) c].tap (tSamples + m);
            wet[c] = y;
        }

        // what goes back into the line
        for (int c = 0; c < 2; ++c)
        {
            float y = wet[c];
            if (! p.infinite)
            {
                y = tone[(size_t) c].process (y);
                y = lowCut[(size_t) c].process (y);
            }
            switch (type)
            {
                case DelayType::Analog: y = std::tanh (y * 1.4f) / 1.4f; y = sat[(size_t) c].process (y, darkFor (3200.0f)); break;
                case DelayType::Tape:   y = std::tanh (y * 1.2f) / 1.15f; y = sat[(size_t) c].process (y, darkFor (6500.0f)); break;
                case DelayType::Echo:   y = std::tanh (y * 1.3f) / 1.25f; y = sat[(size_t) c].process (y, darkFor (5000.0f)); break;
                case DelayType::Real:   y = sat[(size_t) c].process (y, darkFor (9000.0f)); break;
                case DelayType::Liquid: y = phaser[(size_t) c].process (y, 0.35f, 0.9f); break;
                case DelayType::LowBit: y = crush[(size_t) c].process (y, 7000.0f / fs, 8.0f); break;
                case DelayType::Fuzzy:
                {
                    const float d = y * 5.0f;
                    y = 0.19f * (d >= 0.0f ? std::tanh (d) : std::tanh (d * 0.7f) / 0.85f);
                    y = sat[(size_t) c].process (y, darkFor (3800.0f));
                    break;
                }
                case DelayType::Rainbow:
                case DelayType::Crystal:
                {
                    if (! p.infinite)
                    {
                        // keep the lows out of the shifter so octaves never turn to mud on bass
                        const float hp = pitchHp[(size_t) c].process (y);
                        shifter[(size_t) c].setGrain (p.mk1 ? 0.028f : 0.06f);
                        const float ratio = type == DelayType::Crystal ? 2.0f : 1.4983f;
                        const float shifted = shifter[(size_t) c].process (hp, ratio);
                        y = type == DelayType::Crystal ? 0.55f * y + 0.75f * shifted : 0.35f * y + 0.85f * shifted;
                    }
                    break;
                }
                default: break;
            }
            if (p.mk1) y = grit[(size_t) c].process (y, fs);
            y = dc[(size_t) c].process (y);
            fbL[(size_t) c] = softClip (y * fb);
            wet[c] = y;
        }

        // ping-pong sends each side's repeats to the other
        const float fbToL = p.pingPong ? fbL[1] : fbL[0];
        const float fbToR = p.pingPong ? fbL[0] : fbL[1];
        if (p.pingPong)
        {
            const float mono = 0.5f * (in[0] + in[1]);
            line[0].push (mono + fbToL);
            line[1].push (fbToR);
        }
        else
        {
            line[0].push (in[0] + fbToL);
            line[1].push (in[1] + fbToR);
        }

        // ducking follows the player's dry signal; Dyna always ducks
        float duck = p.duck;
        if (type == DelayType::Dyna) duck = std::max (duck, 0.6f);
        const float e = duckEnv.process (dryForDuck);
        const float g = 1.0f - duck * clampf (e * 4.0f, 0.0f, 0.92f);

        float l = wet[0] * g, r = wet[1] * g;
        if (p.mk1)
        {
            // MK I: narrower image
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            l = mid + 0.45f * side;
            r = mid - 0.45f * side;
        }
        // level trims for types that lose level to their own processing; the loop itself is untouched
        float trim = 1.0f;
        switch (type)
        {
            case DelayType::Rainbow: trim = 1.8f; break;
            case DelayType::Crystal: trim = 1.4f; break;
            case DelayType::TriMod:  trim = 2.0f; break;
            case DelayType::Galaxy:  trim = 1.25f; break;
            case DelayType::Tape:    trim = 1.15f; break;
            case DelayType::Echo:    trim = 1.2f; break;
            case DelayType::Fuzzy:   trim = 1.25f; break;
            case DelayType::Dyna:    trim = 1.3f; break;
            case DelayType::Mod:     trim = 1.4f; break;
            case DelayType::ModInv:  trim = 1.35f; break;
            default: break;
        }
        l *= trim; r *= trim;
        outL = l;
        outR = r;
        level = wetEnv.process (0.5f * (l + r));
    }

    float wetLevel() const { return level; }

private:
    // a fixed tone colour for a type, as a one-pole low-pass
    struct Darken
    {
        float z = 0.0f, a = 0.0f, lastHz = -1.0f;
        float process (float x, float a_) { a = a_; z = x + a * (z - x); return z; }
        void reset() { z = 0.0f; }
    };
    float darkFor (float hz) const { return std::exp (-kTwoPi * std::min (hz, fs * 0.45f) / fs); }

    float fs = 48000.0f;
    int module = 0;
    std::array<DelayLine, 2> line;
    std::array<PitchShifter, 2> shifter;
    std::array<ReverseReader, 2> reverse;
    std::array<Phaser, 2> phaser;
    std::array<Mk1Grit, 2> grit;
    std::array<DcBlock, 2> dc;
    std::array<ToneFilter, 2> tone;
    std::array<LowCut, 2> lowCut;
    std::array<LowCut4, 2> inCut;
    std::array<Darken, 2> sat;
    std::array<Crusher, 2> crush;
    struct PitchHp
    {
        Biquad b;
        float process (float x) { return b.process (x); }
    };
    std::array<PitchHp, 2> pitchHp;
    std::array<float, 2> fbL {};
    EnvFollower duckEnv, swellEnv, wetEnv;
    Smoother timeSm, tapeSm, inGate, fbSm;
    float lfo = 0.0f, lfo2 = 0.0f, level = 0.0f;
};
} // namespace kdd
