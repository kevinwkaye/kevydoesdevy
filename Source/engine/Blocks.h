#pragma once

// Building blocks shared by the delays and the reverb: pitch shifter, reverse reader,
// phaser, envelope follower and the MK I "grit" stage. Plain C++17, no JUCE.

#include "DspUtil.h"

namespace kdd
{
// Two-grain pitch shifter with Hann crossfades. Grain length sets the character:
// long grains are smooth (MK II), short grains are grainy and warbly (MK I).
struct PitchShifter
{
    DelayLine line;
    float phase = 0.0f, grain = 2400.0f, fs = 48000.0f;

    void prepare (float sampleRate, float maxGrainSeconds = 0.12f)
    {
        fs = sampleRate;
        line.init (nextPow2 ((int) (maxGrainSeconds * fs * 2.0f) + 8));
        phase = 0.0f;
    }
    void clear() { line.clear(); phase = 0.0f; }
    void setGrain (float seconds) { grain = clampf (seconds * fs, 64.0f, (float) line.size() * 0.45f); }

    float process (float x, float ratio)
    {
        line.push (x);
        // the read point drifts at (1 - ratio) samples per sample, wrapped inside one grain
        phase += (1.0f - ratio) / grain;
        phase -= std::floor (phase);
        const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
        const float d1 = 4.0f + phase * grain, d2 = 4.0f + p2 * grain;
        const float w1 = std::sin (kPi * phase), w2 = std::sin (kPi * p2);
        return line.tap (d1) * w1 * w1 + line.tap (d2) * w2 * w2;
    }
};

// Plays the last "length" samples backwards, in overlapping windows.
// Average delay is about one window.
struct ReverseReader
{
    DelayLine line;
    float phase = 0.0f;

    void prepare (int maxSamples) { line.init (nextPow2 (maxSamples * 2 + 16)); phase = 0.0f; }
    void clear() { line.clear(); phase = 0.0f; }

    float process (float x, float length)
    {
        line.push (x);
        length = clampf (length, 256.0f, (float) line.size() * 0.45f);
        phase += 1.0f / length;
        phase -= std::floor (phase);
        const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
        // distance grows two samples per sample, so the read runs backwards at normal speed
        const float a = line.tap (4.0f + 2.0f * phase * length);
        const float b = line.tap (4.0f + 2.0f * p2 * length);
        const float w1 = std::sin (kPi * phase), w2 = std::sin (kPi * p2);
        return a * w1 * w1 + b * w2 * w2;
    }
};

// Four first-order all-passes swept by a slow LFO
struct Phaser
{
    float z[4] {}, lfo = 0.0f, fb = 0.0f, fs = 48000.0f;
    void prepare (float sampleRate) { fs = sampleRate; reset(); }
    void reset() { for (auto& v : z) v = 0.0f; lfo = 0.0f; fb = 0.0f; }
    float process (float x, float rateHz, float depth)
    {
        lfo += rateHz / fs;
        if (lfo >= 1.0f) lfo -= 1.0f;
        const float m = 0.5f + 0.5f * std::sin (kTwoPi * lfo);
        const float fc = 300.0f * std::pow (2.0f, m * 3.5f);
        const float t = std::tan (kPi * fc / fs);
        const float a = (t - 1.0f) / (t + 1.0f);
        float y = x + 0.5f * fb;
        for (auto& s : z)
        {
            const float o = a * y + s;
            s = y - a * o;
            y = o;
        }
        fb = y;
        return x * (1.0f - 0.5f * depth) + y * 0.5f * depth;
    }
};

// Peak follower with separate attack and release
struct EnvFollower
{
    float env = 0.0f, att = 0.0f, rel = 0.0f;
    void prepare (float fs, float attackMs, float releaseMs)
    {
        att = 1.0f - std::exp (-1.0f / (attackMs * 0.001f * fs));
        rel = 1.0f - std::exp (-1.0f / (releaseMs * 0.001f * fs));
    }
    float process (float x)
    {
        const float a = std::fabs (x);
        env += (a - env) * (a > env ? att : rel);
        return env;
    }
    void reset() { env = 0.0f; }
};

// Sample-rate and bit-depth reduction. Used hard by Low-Bit, gently by MK I voicing.
struct Crusher
{
    float held = 0.0f, acc = 0.0f;
    float process (float x, float rateRatio, float bits)
    {
        acc += rateRatio;
        if (acc >= 1.0f) { acc -= std::floor (acc); held = x; }
        const float q = std::pow (2.0f, bits - 1.0f);
        return std::round (held * q) / q;
    }
    void reset() { held = acc = 0.0f; }
};

// Gentle "older converter" colour for MK I voicing: a little band limiting and grain.
struct Mk1Grit
{
    Biquad lp;
    Crusher crush;
    void prepare (float fs) { lp.lowpass (std::min (11000.0f, fs * 0.45f), 0.707f, fs); crush.reset(); }
    float process (float x, float fs)
    {
        const float y = crush.process (x, 32000.0f / fs, 12.0f);
        return lp.process (y);
    }
    void reset() { lp.reset(); crush.reset(); }
};

// Tone knob: low-pass or high-pass, as the pedal's menu chooses for each block
struct ToneFilter
{
    Biquad f;
    float lastT = -1.0f;
    int lastMode = -1;
    void set (float tone01, int mode, float fs)
    {
        if (std::fabs (tone01 - lastT) < 1.0e-4f && mode == lastMode) return;
        lastT = tone01; lastMode = mode;
        if (mode == 0) f.lowpass (600.0f * std::pow (2.0f, tone01 * 4.8f), 0.6f, fs);   // 600 Hz .. 16.7 kHz
        else           f.highpass (20.0f * std::pow (2.0f, tone01 * 6.0f), 0.6f, fs);   // 20 Hz .. 1.3 kHz
    }
    float process (float x) { return f.process (x); }
    void reset() { f.reset(); }
};

// Low cut: 12 dB/oct Butterworth high-pass, retuned only when the knob moves
struct LowCut
{
    Biquad f;
    float last = -1.0f;
    void set (float hz, float fs)
    {
        if (std::fabs (hz - last) < 0.05f) return;
        last = hz;
        f.highpass (hz, 0.7071f, fs);
    }
    float process (float x) { return f.process (x); }
    void reset() { f.reset(); }
};
} // namespace kdd
