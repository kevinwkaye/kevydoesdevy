#pragma once

// The Ocean Machine reverb module: nine types (Room, Hall, Plate, Dist-Verb, FL-Verb,
// Filter, Reverse, Spring, Mod) on one eight-line feedback delay network, with
// pre-delay, an octave-up shimmer inside the tail, Tone as low-pass or high-pass,
// a low cut for bass, freeze, and MK I / MK II voicing. Returns wet signal only.

#include "Blocks.h"
#include <array>

namespace kdd
{
enum class ReverbType { Room = 0, Hall, Plate, DistVerb, FlVerb, Filter, Reverse, Spring, Mod };

struct ReverbParams
{
    bool on = true;
    int position = 1;          // 0..8
    float decay = 0.5f;        // knob 0..1
    float preDelayMs = 30.0f;
    float shimmer = 0.0f;      // 0..1
    float tone = 0.6f;
    int toneMode = 0;          // 0 low-pass, 1 high-pass
    float lowCutHz = 180.0f;
    bool mk1 = false;
    bool freeze = false;
    bool merge = true;         // keep the tail when the type changes
};

class ReverbEngine
{
public:
    static constexpr int N = 8;
    static constexpr float kMakeUp = 3.2f;   // +10 dB

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        pre.init (nextPow2 ((int) (0.55f * fs) + 8));
        for (auto& d : lines) d.init (nextPow2 ((int) (0.16f * fs) + 64));
        for (auto& ch : diff) for (auto& a : ch) a.line.init (nextPow2 ((int) (0.03f * fs) + 8));
        for (auto& ch : spring) for (auto& a : ch) a.line.init (64);
        for (int c = 0; c < 2; ++c)
        {
            flange[(size_t) c].init (nextPow2 ((int) (0.012f * fs)));
            shimmer[(size_t) c].prepare (fs, 0.1f);
            reverse[(size_t) c].prepare ((int) (1.6f * fs));
            grit[(size_t) c].prepare (fs);
            dc[(size_t) c].prepare (fs);
            shimHp[(size_t) c].highpass (150.0f, 0.7071f, fs);
            shimLp[(size_t) c].lowpass (7000.0f, 0.7071f, fs);
        }
        inGate.setTime (0.02f, fs);
        preSm.setTime (0.2f, fs);
        wetEnv.prepare (fs, 5.0f, 300.0f);
        configure (ReverbType::Hall);
        current = -1;
        reset();
    }

    void reset()
    {
        pre.clear();
        for (auto& d : lines) d.clear();
        for (auto& ch : diff) for (auto& a : ch) a.line.clear();
        for (auto& ch : spring) for (auto& a : ch) a.line.clear();
        for (auto& z : damp) z = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            flange[(size_t) c].clear(); shimmer[(size_t) c].clear(); reverse[(size_t) c].clear();
            grit[(size_t) c].reset(); dc[(size_t) c].reset(); tone[(size_t) c].reset(); lowCut[(size_t) c].reset();
            filt[(size_t) c].reset(); post[(size_t) c].reset(); shimHp[(size_t) c].reset(); shimLp[(size_t) c].reset();
            shimFb[(size_t) c] = 0.0f; flFb[(size_t) c] = 0.0f;
        }
        wetEnv.reset();
    }

    void clearTails()
    {
        for (auto& d : lines) d.clear();
        for (auto& z : damp) z = 0.0f;
        for (auto& s : shimFb) s = 0.0f;
        for (auto& f : flFb) f = 0.0f;
        for (int c = 0; c < 2; ++c) { flange[(size_t) c].clear(); shimmer[(size_t) c].clear(); reverse[(size_t) c].clear(); }
        for (auto& ch : diff) for (auto& a : ch) a.line.clear();
        pre.clear();
    }

    void process (float inL, float inR, const ReverbParams& p, float& outL, float& outR)
    {
        if (p.position != current)
        {
            current = p.position;
            if (! p.merge) clearTails();
            configure ((ReverbType) std::min (8, std::max (0, p.position)));
        }

        const float gate = inGate.next (p.on && ! p.freeze ? 1.0f : 0.0f);
        for (int c = 0; c < 2; ++c) { lowCut[(size_t) c].set (p.lowCutHz, fs); tone[(size_t) c].set (p.tone, p.toneMode, fs); }

        float l = lowCut[0].process (inL) * gate, r = lowCut[1].process (inR) * gate;

        // pre-delay on the mid, a little side kept for width
        pre.push (0.5f * (l + r));
        const float preSamples = preSm.next (clampf (p.preDelayMs, 0.0f, 500.0f) * 0.001f * fs + base.preMs * 0.001f * fs);
        float mid = pre.tap (std::max (2.0f, preSamples));
        const float side = 0.35f * (l - r);

        // spring: a chirpy dispersion chain before the tank
        float sl = mid + side, sr = mid - side;
        if (type == ReverbType::Spring)
            for (int k = 0; k < kSpring; ++k) { sl = spring[0][(size_t) k].process (sl); sr = spring[1][(size_t) k].process (sr); }

        // input diffusion
        for (int k = 0; k < 4; ++k) { sl = diff[0][(size_t) k].process (sl); sr = diff[1][(size_t) k].process (sr); }

        // tank
        const float rt = rtSeconds (p.decay);
        float taps[N];
        for (int i = 0; i < N; ++i)
        {
            lfo[(size_t) i] += lfoRate[(size_t) i] / fs;
            if (lfo[(size_t) i] >= 1.0f) lfo[(size_t) i] -= 1.0f;
            const float m = base.mod * modDepthSamples * std::sin (kTwoPi * lfo[(size_t) i]);
            float y = lines[(size_t) i].tap (len[(size_t) i] + m);
            if (! p.freeze)
                y = damp[(size_t) i] = y + dampA * (damp[(size_t) i] - y);
            taps[i] = y;
        }

        float outTankL = 0.0f, outTankR = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            const float s = (i & 2) ? -1.0f : 1.0f;
            if (i & 1) outTankR += taps[i] * s; else outTankL += taps[i] * s;
        }
        outTankL *= 0.5f; outTankR *= 0.5f;

        // shimmer: octave up inside the loop, lows kept out
        float shimIn[2] = { 0.0f, 0.0f };
        if (p.shimmer > 0.001f && ! p.freeze)
        {
            const float grain = p.mk1 ? 0.03f : 0.075f;
            for (int c = 0; c < 2; ++c)
            {
                shimmer[(size_t) c].setGrain (grain);
                const float src = shimHp[(size_t) c].process (c == 0 ? outTankL : outTankR);
                float s = shimmer[(size_t) c].process (src, 2.0f);
                if (! p.mk1) s = shimLp[(size_t) c].process (s);
                shimFb[(size_t) c] = s;
                shimIn[c] = s * p.shimmer * 0.55f;
            }
        }

        hadamard (taps);
        for (int i = 0; i < N; ++i)
        {
            const float g = p.freeze ? 1.0f : lineGain (i, rt);
            const float inj = (i & 1) ? (sr + shimIn[1]) : (sl + shimIn[0]);
            const float sign = (i & 2) ? -1.0f : 1.0f;
            lines[(size_t) i].push (softClip (taps[i] * g + inj * sign * 0.35f));
        }

        // colour after the tank
        float yl = outTankL, yr = outTankR;
        switch (type)
        {
            case ReverbType::DistVerb:
                yl = std::tanh (yl * 4.0f) * 0.45f; yr = std::tanh (yr * 4.0f) * 0.45f;
                yl = post[0].process (yl); yr = post[1].process (yr);
                break;
            case ReverbType::FlVerb:
            {
                flLfo += 0.22f / fs; if (flLfo >= 1.0f) flLfo -= 1.0f;
                const float d = (0.0012f + 0.0035f * (0.5f + 0.5f * std::sin (kTwoPi * flLfo))) * fs;
                for (int c = 0; c < 2; ++c)
                {
                    float& y = c == 0 ? yl : yr;
                    const float delayed = flange[(size_t) c].tap (c == 0 ? d : d * 1.07f);
                    flange[(size_t) c].push (y + 0.65f * flFb[(size_t) c]);
                    flFb[(size_t) c] = delayed;
                    y = 0.6f * (y + delayed);
                }
                break;
            }
            case ReverbType::Filter:
            {
                fLfo += 0.18f / fs; if (fLfo >= 1.0f) fLfo -= 1.0f;
                const float fc = 350.0f * std::pow (2.0f, 3.6f * (0.5f + 0.5f * std::sin (kTwoPi * fLfo)));
                filt[0].set (fc, 3.0f, fs); filt[1].set (fc * 1.05f, 3.0f, fs);
                yl = filt[0].lp (yl) * 0.8f; yr = filt[1].lp (yr) * 0.8f;
                break;
            }
            case ReverbType::Reverse:
            {
                const float window = (0.25f + 1.0f * p.decay) * fs;
                yl = reverse[0].process (yl, window); yr = reverse[1].process (yr, window);
                break;
            }
            case ReverbType::Spring:
                yl = post[0].process (yl); yr = post[1].process (yr);
                break;
            default: break;
        }

        for (int c = 0; c < 2; ++c)
        {
            float& y = c == 0 ? yl : yr;
            y = tone[(size_t) c].process (y);
            if (p.mk1) y = grit[(size_t) c].process (y, fs);
            y = dc[(size_t) c].process (y);
        }
        if (p.mk1)
        {
            const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr);
            yl = m + 0.5f * s; yr = m - 0.5f * s;
        }
        // make-up gain so the reverb sits level with the delays at the same Mix setting
        outL = yl * base.outGain * kMakeUp;
        outR = yr * base.outGain * kMakeUp;
        level = wetEnv.process (0.5f * (outL + outR));
    }

    float wetLevel() const { return level; }

private:
    struct Allpass
    {
        DelayLine line;
        int len = 10;
        float g = 0.6f;
        float process (float x)
        {
            const float d = line.at (len);
            const float v = x + g * d;
            line.push (v);
            return d - g * v;
        }
    };

    struct Voicing { float size, diffusion, dampHz, mod, rtScale, preMs, outGain; };

    static void hadamard (float* x)
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h * 2)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b; x[j + h] = a - b;
                }
        const float s = 0.35355339f;
        for (int i = 0; i < N; ++i) x[i] *= s;
    }

    float rtSeconds (float decay) const { return 0.25f * std::pow (80.0f, clampf (decay, 0.0f, 1.0f)) * base.rtScale; }

    float lineGain (int i, float rt) const
    {
        return std::pow (10.0f, -3.0f * len[(size_t) i] / (std::max (0.05f, rt) * fs));
    }

    void configure (ReverbType t)
    {
        type = t;
        switch (t)
        {
            case ReverbType::Room:     base = { 0.32f, 0.62f, 6000.0f, 0.3f, 0.35f, 0.0f, 1.1f }; break;
            case ReverbType::Hall:     base = { 1.0f, 0.72f, 7500.0f, 0.6f, 1.0f, 12.0f, 1.0f }; break;
            case ReverbType::Plate:    base = { 0.55f, 0.82f, 11000.0f, 0.4f, 0.8f, 0.0f, 1.2f }; break;
            case ReverbType::DistVerb: base = { 0.8f, 0.7f, 6500.0f, 0.4f, 0.8f, 6.0f, 0.75f }; break;
            case ReverbType::FlVerb:   base = { 0.8f, 0.7f, 8000.0f, 0.4f, 0.9f, 6.0f, 1.35f }; break;
            case ReverbType::Filter:   base = { 0.9f, 0.72f, 9000.0f, 0.5f, 0.9f, 6.0f, 1.1f }; break;
            case ReverbType::Reverse:  base = { 0.7f, 0.75f, 8000.0f, 0.4f, 0.6f, 0.0f, 1.6f }; break;
            case ReverbType::Spring:   base = { 0.26f, 0.5f, 4500.0f, 0.2f, 0.5f, 0.0f, 1.2f }; break;
            case ReverbType::Mod:      base = { 0.9f, 0.72f, 7000.0f, 2.6f, 1.0f, 8.0f, 1.0f }; break;
        }
        static constexpr float ms[N] = { 31.7f, 37.1f, 41.3f, 47.9f, 53.3f, 59.9f, 67.1f, 73.9f };
        for (int i = 0; i < N; ++i)
        {
            len[(size_t) i] = std::max (0.008f * fs, ms[i] * 0.001f * fs * base.size * 1.6f);
            len[(size_t) i] = std::min (len[(size_t) i], (float) lines[(size_t) i].size() - 64.0f);
            lfoRate[(size_t) i] = 0.11f + 0.07f * (float) i;
        }
        modDepthSamples = 0.0006f * fs;
        dampA = std::exp (-kTwoPi * std::min (base.dampHz, fs * 0.45f) / fs);
        static constexpr float dms[4] = { 4.7f, 6.1f, 8.3f, 11.9f };
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
            {
                auto& a = diff[(size_t) c][(size_t) k];
                a.len = std::max (8, std::min (a.line.size() - 4, (int) (dms[k] * (c == 0 ? 1.0f : 1.13f) * 0.001f * fs * (0.5f + 0.5f * base.size))));
                a.g = base.diffusion;
            }
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kSpring; ++k)
            {
                auto& a = spring[(size_t) c][(size_t) k];
                a.len = 1 + ((k + c) % 3);
                a.g = 0.62f;
            }
        for (int c = 0; c < 2; ++c)
        {
            if (t == ReverbType::Spring) post[(size_t) c].lowpass (4200.0f, 0.9f, fs);
            else post[(size_t) c].lowpass (5200.0f, 0.707f, fs);
        }
    }

    static constexpr int kSpring = 12;
    float fs = 48000.0f;
    ReverbType type = ReverbType::Hall;
    int current = -1;
    Voicing base {};
    DelayLine pre;
    std::array<DelayLine, N> lines;
    std::array<float, N> len {}, lfo {}, lfoRate {}, damp {};
    std::array<std::array<Allpass, 4>, 2> diff;
    std::array<std::array<Allpass, kSpring>, 2> spring;
    std::array<DelayLine, 2> flange;
    std::array<float, 2> flFb {}, shimFb {};
    std::array<PitchShifter, 2> shimmer;
    std::array<ReverseReader, 2> reverse;
    std::array<Mk1Grit, 2> grit;
    std::array<DcBlock, 2> dc;
    std::array<ToneFilter, 2> tone;
    std::array<LowCut4, 2> lowCut;
    std::array<Svf, 2> filt;
    std::array<Biquad, 2> post, shimHp, shimLp;
    Smoother inGate, preSm;
    EnvFollower wetEnv;
    float modDepthSamples = 30.0f, dampA = 0.5f, flLfo = 0.0f, fLfo = 0.0f, level = 0.0f;
};
} // namespace kdd
