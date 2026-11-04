// Offline render test for the Kevy does Devy engine (no JUCE needed).
// Plays a bass line through every delay type, every reverb type, each voicing, routing,
// infinite and freeze, and the looper, then checks for NaNs, runaway levels and silence.
// Pass a folder to also write WAV files.
//
// Usage: kdd_render_test [output_folder]

#include "Engine.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

using namespace kdd;

static constexpr float kFs = 48000.0f;
static int failures = 0;
static std::string outDir;

static void makeBass (std::vector<float>& l, std::vector<float>& r, int n)
{
    l.assign ((size_t) n, 0.0f);
    r.assign ((size_t) n, 0.0f);
    const float notes[8] = { 41.2f, 41.2f, 55.0f, 49.0f, 41.2f, 61.7f, 55.0f, 36.7f };
    for (int k = 0; k < 8; ++k)
    {
        const int s0 = (int) ((0.1f + k * 0.5f) * kFs);
        for (int i = 0; i < (int) (0.45f * kFs) && s0 + i < n; ++i)
        {
            const float t = (float) i / kFs;
            const float e = std::exp (-t * 3.0f) * std::min (1.0f, t * 800.0f);
            const float f = notes[k];
            const float v = 0.45f * e * (std::sin (kTwoPi * f * t) + 0.45f * std::sin (kTwoPi * 2 * f * t) + 0.2f * std::sin (kTwoPi * 3 * f * t)
                                         + 0.1f * std::sin (kTwoPi * 5 * f * t));
            l[(size_t) (s0 + i)] = v;
            r[(size_t) (s0 + i)] = v;
        }
    }
}

static void writeWav (const std::string& name, const std::vector<float>& l, const std::vector<float>& r)
{
    if (outDir.empty()) return;
    FILE* f = std::fopen ((outDir + "/" + name + ".wav").c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4, rate = (uint32_t) kFs, byteRate = rate * 4;
    const uint16_t ch = 2, bits = 16, align = 4, fmt = 1;
    const uint32_t riffSize = 36 + dataBytes, fmtSize = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riffSize, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtSize, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&byteRate, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i)
    {
        const int16_t a = (int16_t) (clampf (l[i], -1.0f, 1.0f) * 32767.0f), b = (int16_t) (clampf (r[i], -1.0f, 1.0f) * 32767.0f);
        std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
    }
    std::fclose (f);
}

struct Result { float peak = 0, tailRms = 0, wetRms = 0; bool finite = true; };

// Renders seconds of audio; the hook can change parameters at each block
static Result render (const std::string& name, EngineParams p, float seconds,
                      const std::function<void (Engine&, EngineParams&, float)>& hook = {})
{
    auto e = std::make_unique<Engine>();
    e->prepare (kFs);
    const int n = (int) (seconds * kFs);
    std::vector<float> inL, inR;
    makeBass (inL, inR, n);
    std::vector<float> l = inL, r = inR;
    const int block = 128;
    for (int s = 0; s < n; s += block)
    {
        const int m = std::min (block, n - s);
        if (hook) hook (*e, p, (float) s / kFs);
        e->process (l.data() + s, r.data() + s, m, p);
    }
    Result res;
    double tail = 0, wet = 0;
    int tailN = 0;
    for (int i = 0; i < n; ++i)
    {
        if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) res.finite = false;
        res.peak = std::max (res.peak, std::max (std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i])));
        const float w = l[(size_t) i] - (p.killDry ? 0.0f : inL[(size_t) i]);
        wet += (double) w * w;
        if (i > (int) (4.3f * kFs)) { tail += (double) l[(size_t) i] * l[(size_t) i]; ++tailN; }
    }
    res.tailRms = tailN > 0 ? (float) std::sqrt (tail / tailN) : 0.0f;
    res.wetRms = (float) std::sqrt (wet / n);
    writeWav (name, l, r);
    return res;
}

static void check (const std::string& name, const Result& r, float minWet, float maxPeak)
{
    const bool ok = r.finite && r.peak < maxPeak && r.wetRms >= minWet;
    std::printf ("%-28s %s peak %.2f wet %.4f tail %.4f\n", name.c_str(), ok ? "ok  " : "FAIL", r.peak, r.wetRms, r.tailRms);
    if (! ok) ++failures;
}

int main (int argc, char** argv)
{
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040); // flush denormals, as hosts do
#endif
    if (argc > 1) outDir = argv[1];
    const char* aNames[9] = { "Digital", "Analog", "Tape", "Echo", "Liquid", "Rainbow", "Crystal", "LowBit", "Fuzzy" };
    const char* bNames[9] = { "Digital", "Analog", "Tape", "Real", "Dyna", "Galaxy", "Mod", "TriMod", "ModInv" };
    const char* rNames[9] = { "Room", "Hall", "Plate", "DistVerb", "FlVerb", "Filter", "Reverse", "Spring", "Mod" };

    const auto t0 = std::chrono::steady_clock::now();

    for (int mk = 0; mk < 2; ++mk)
        for (int t = 0; t < 9; ++t)
        {
            EngineParams p;
            p.a.on = true; p.b.on = false; p.r.on = false; p.mixA = 0.7f; p.mixR = 0.0f; p.mixB = 0.0f;
            p.a.position = t; p.a.mk1 = mk == 1; p.a.feedback = 0.6f; p.a.timeMs = 330.0f;
            check (std::string ("A ") + aNames[t] + (mk ? " MK1" : ""), render (std::string ("A_") + aNames[t] + (mk ? "_mk1" : ""), p, 6.0f), 0.01f, 3.0f);

            EngineParams q;
            q.a.on = false; q.b.on = true; q.r.on = false; q.mixA = 0.0f; q.mixB = 0.7f; q.mixR = 0.0f;
            q.b.position = t; q.b.mk1 = mk == 1; q.b.feedback = 0.6f; q.b.timeMs = 420.0f; q.b.pingPong = t == 0;
            check (std::string ("B ") + bNames[t] + (mk ? " MK1" : ""), render (std::string ("B_") + bNames[t] + (mk ? "_mk1" : ""), q, 6.0f), 0.01f, 3.0f);

            EngineParams v;
            v.a.on = false; v.b.on = false; v.r.on = true; v.mixA = 0.0f; v.mixB = 0.0f; v.mixR = 0.7f;
            v.r.position = t; v.r.mk1 = mk == 1; v.r.decay = 0.6f; v.r.shimmer = t == 1 ? 0.6f : 0.0f;
            check (std::string ("R ") + rNames[t] + (mk ? " MK1" : ""), render (std::string ("R_") + rNames[t] + (mk ? "_mk1" : ""), v, 6.0f), 0.005f, 3.0f);
        }

    // feedback at maximum must self-oscillate without blowing up
    {
        EngineParams p; p.b.on = false; p.r.on = false; p.mixB = 0; p.mixR = 0; p.mixA = 0.8f; p.a.feedback = 1.0f; p.a.position = 2;
        check ("A Tape feedback max", render ("A_runaway", p, 20.0f), 0.01f, 3.5f);
    }
    // infinite: holds the repeats without growing, input closed
    {
        EngineParams p; p.b.on = false; p.r.on = false; p.mixB = 0; p.mixR = 0; p.mixA = 0.7f; p.a.feedback = 0.5f;
        const auto r = render ("A_infinite", p, 12.0f, [] (Engine&, EngineParams& q, float t) { q.a.infinite = t > 1.0f; });
        check ("A infinite", r, 0.01f, 3.0f);
        if (r.tailRms < 0.01f) { std::printf ("  infinite died away\n"); ++failures; }
    }
    // reverb freeze
    {
        EngineParams p; p.a.on = false; p.b.on = false; p.mixA = 0; p.mixB = 0; p.mixR = 0.8f; p.r.decay = 0.5f;
        const auto r = render ("R_freeze", p, 12.0f, [] (Engine&, EngineParams& q, float t) { q.r.freeze = t > 1.5f; });
        check ("R freeze", r, 0.005f, 3.0f);
        if (r.tailRms < 0.005f) { std::printf ("  freeze died away\n"); ++failures; }
    }
    // every routing with everything on, plus heavy shimmer and Mk I everywhere
    for (int route = 0; route < 4; ++route)
    {
        EngineParams p; p.route = route; p.r.shimmer = 0.8f; p.r.decay = 0.85f; p.a.feedback = 0.8f; p.b.feedback = 0.8f;
        p.a.mk1 = p.b.mk1 = p.r.mk1 = route == 3;
        check ("Route " + std::to_string (route), render ("route_" + std::to_string (route), p, 8.0f), 0.01f, 3.5f);
    }
    // looper: record two seconds, play, overdub, undo, half speed, reverse, stop
    {
        EngineParams p; p.mixA = p.mixB = p.mixR = 0.0f; p.loopPos = LoopPost; p.loopLevel = 0.8f;
        const auto r = render ("looper", p, 14.0f, [] (Engine& e, EngineParams&, float t)
        {
            static int step = 0;
            if (t < 0.01f) step = 0;
            const float at[7] = { 0.05f, 2.05f, 4.1f, 6.1f, 7.0f, 9.0f, 12.0f };
            if (step < 7 && t >= at[step])
            {
                switch (step)
                {
                    case 0: case 1: case 2: case 3: e.looper.post (Looper::CmdMain); break;    // rec, play, dub, play
                    case 4: e.looper.post (Looper::CmdUndo); break;
                    case 5: e.looper.post (Looper::CmdHalf); break;
                    case 6: e.looper.post (Looper::CmdReverse); break;
                }
                ++step;
            }
        });
        check ("Looper", r, 0.0f, 3.0f);
        if (r.tailRms < 0.01f) { std::printf ("  looper silent after the input ended\n"); ++failures; }
    }

    // CPU: everything on, 10 seconds
    {
        EngineParams p; p.r.shimmer = 0.5f; p.a.position = 5; p.b.position = 7;
        const auto c0 = std::chrono::steady_clock::now();
        render ("cpu", p, 10.0f);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - c0).count();
        std::printf ("CPU: 10 s of audio with everything on took %.2f s (%.1f%% of one core)\n", secs, secs * 10.0);
    }

    const double total = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("%s (%d failures, %.1f s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, total);
    return failures == 0 ? 0 : 1;
}
