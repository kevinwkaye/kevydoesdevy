// Preset audit: plays a bass riff through every preset and measures what a player would hear
// as harsh or lost. Build with the header made by tools/gen_presets.py:
//
//   python3 tools/gen_presets.py Source/Params.h build/PresetsGenerated.h
//   g++ -std=c++17 -O2 -ISource/engine -Ibuild Tests/PresetAudit.cpp -o preset_audit
//   ./preset_audit [wav_folder]
//
// Columns, all in dB:
//   wet   effects level against the dry bass while playing, both above 100 Hz (LOST below -18, LOUD above -4)
//   hi    share of the effects above 4 kHz (HARSH above -13: fizz and pick clatter in the repeats)
//   sub   share of the effects below 100 Hz (MUD above -9: boom in the repeats and tails)
//   tail  effects level 1 to 3 s after the playing stops
//   end   effects level in the last second (RUNAWAY if it isn't dying away)

#include "PresetsGenerated.h"
#include <cstdio>
#include <memory>
#include <string>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

using namespace kdd;
static constexpr float kFs = 48000.0f;

static void makeRiff (std::vector<float>& x, int n)
{
    x.assign ((size_t) n, 0.0f);
    // E1 A1 G1 E1 B1 A1 D2 E1: a bass line with a pick attack
    const float notes[8] = { 41.2f, 55.0f, 49.0f, 41.2f, 61.7f, 55.0f, 73.4f, 41.2f };
    uint32_t seed = 1;
    for (int k = 0; k < 8; ++k)
    {
        const int s0 = (int) ((0.05f + k * 0.48f) * kFs);
        for (int i = 0; i < (int) (0.44f * kFs) && s0 + i < n; ++i)
        {
            const float t = (float) i / kFs, f = notes[k];
            const float e = std::exp (-t * 2.5f) * std::min (1.0f, t * 900.0f);
            float v = 0.5f * e * (std::sin (kTwoPi * f * t) + 0.5f * std::sin (kTwoPi * 2 * f * t) + 0.3f * std::sin (kTwoPi * 3 * f * t)
                                  + 0.15f * std::sin (kTwoPi * 4 * f * t) + 0.08f * std::sin (kTwoPi * 6 * f * t));
            seed = seed * 1664525u + 1013904223u;
            const float noise = ((float) (seed >> 8) / 8388608.0f - 1.0f);
            v += 0.12f * std::exp (-t * 120.0f) * noise;   // pick click
            x[(size_t) (s0 + i)] = v;
        }
    }
}

struct Band
{
    Biquad a, b;
    float process (float x) { return b.process (a.process (x)); }
};

static double db (double ratio) { return 10.0 * std::log10 (std::max (1.0e-12, ratio)); }

int main (int argc, char** argv)
{
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040);
#endif
    const std::string wavDir = argc > 1 ? argv[1] : "";
    const float playSecs = 4.0f, totalSecs = 9.0f;
    const int n = (int) (totalSecs * kFs), playN = (int) (playSecs * kFs);
    std::vector<float> in;
    makeRiff (in, n);
    // loudness is judged above 100 Hz, where the ear actually hears a bass's repeats
    double dryE = 0.0;
    {
        Biquad h1, h2;
        h1.highpass (100.0f, 0.707f, kFs); h2.highpass (100.0f, 0.707f, kFs);
        for (int i = 0; i < playN; ++i) { const float v = h2.process (h1.process (in[(size_t) i])); dryE += (double) v * v; }
        dryE /= playN;
    }

    std::printf ("%-16s %6s %6s %6s %6s %6s %6s  %s\n", "preset", "wet", "hi", "sub", "tail", "end", "peak", "flags");
    for (auto& gp : genPresets())
    {
        auto e = std::make_unique<Engine>();
        e->prepare (kFs);
        std::vector<float> L = in, R = in;
        for (int s = 0; s < n; s += 128) e->process (L.data() + s, R.data() + s, std::min (128, n - s), gp.p);

        Band hi, lo, aud;
        aud.a.highpass (100.0f, 0.707f, kFs); aud.b.highpass (100.0f, 0.707f, kFs);
        hi.a.highpass (4000.0f, 0.707f, kFs); hi.b.highpass (4000.0f, 0.707f, kFs);
        lo.a.lowpass (100.0f, 0.707f, kFs); lo.b.lowpass (100.0f, 0.707f, kFs);
        double wetPlay = 0, wetAll = 0, hiE = 0, loE = 0, tail = 0, end = 0;
        float peak = 0;
        for (int i = 0; i < n; ++i)
        {
            const float w = 0.5f * ((L[(size_t) i] - in[(size_t) i]) + (R[(size_t) i] - in[(size_t) i]));
            peak = std::max (peak, std::max (std::fabs (L[(size_t) i]), std::fabs (R[(size_t) i])));
            const float h = hi.process (w), l = lo.process (w), a = aud.process (w);
            wetAll += (double) w * w; hiE += (double) h * h; loE += (double) l * l;
            if (i < playN) wetPlay += (double) a * a;
            if (i >= playN + (int) kFs && i < playN + 3 * (int) kFs) tail += (double) a * a;
            if (i >= n - (int) kFs) end += (double) a * a;
        }
        const double wetDb = db (wetPlay / playN / dryE), hiDb = db (hiE / wetAll), subDb = db (loE / wetAll);
        const double tailDb = db (tail / (2.0 * kFs) / dryE), endDb = db (end / kFs / dryE);
        std::string flags;
        if (wetDb < -18) flags += "LOST ";
        if (wetDb > -4) flags += "LOUD ";
        if (hiDb > -13) flags += "HARSH ";
        if (subDb > -9) flags += "MUD ";
        if (endDb > -30 && endDb > tailDb - 4) flags += "RUNAWAY ";
        if (peak > 1.2f) flags += "HOT ";
        std::printf ("%-16s %6.1f %6.1f %6.1f %6.1f %6.1f %6.2f  %s\n", gp.name, wetDb, hiDb, subDb, tailDb, endDb, peak, flags.c_str());

        if (! wavDir.empty())
        {
            std::string fn = wavDir + "/" + gp.name + ".wav";
            for (auto& c : fn) if (c == ' ') c = '_';
            if (FILE* f = std::fopen (fn.c_str(), "wb"))
            {
                const uint32_t frames = (uint32_t) n, bytes = frames * 4, rate = (uint32_t) kFs, br = rate * 4, riff = 36 + bytes, fmtSize = 16;
                const uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
                std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
                std::fwrite (&fmtSize, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
                std::fwrite (&rate, 4, 1, f); std::fwrite (&br, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
                std::fwrite ("data", 1, 4, f); std::fwrite (&bytes, 4, 1, f);
                for (int i = 0; i < n; ++i)
                {
                    const int16_t a = (int16_t) (clampf (L[(size_t) i], -1.0f, 1.0f) * 32767.0f), b = (int16_t) (clampf (R[(size_t) i], -1.0f, 1.0f) * 32767.0f);
                    std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
                }
                std::fclose (f);
            }
        }
    }
    return 0;
}
