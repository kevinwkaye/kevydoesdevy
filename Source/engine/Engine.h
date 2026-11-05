#pragma once

// Kevy does Devy engine: Delay A, Delay B and Reverb in any of four routings, the looper
// before, between or after them, a wet-only global EQ, trails and kill dry.
// The dry signal passes straight through at unity and is never filtered, so a bass's
// low end and attack stay exactly as played. Plain C++17, no JUCE.

#include "DelayEngine.h"
#include "ReverbEngine.h"
#include "Looper.h"

namespace kdd
{
enum Route { RouteSeries = 0, RouteDelaysParallel, RouteReverbFirst, RouteAllParallel };
enum LoopPos { LoopPre = 0, LoopMid, LoopPost };

struct EngineParams
{
    DelayParams a, b;
    ReverbParams r;
    float mixA = 0.4f, mixB = 0.3f, mixR = 0.35f;   // knobs 0..1
    int route = RouteSeries;
    int loopPos = LoopPost;
    float loopLevel = 0.8f;                          // knob 0..1
    bool trails = true;
    bool killDry = false;
    bool eqOn = false;
    bool eqOnDry = false;
    float eqLowDb = 0.0f, eqMidDb = 0.0f, eqHighDb = 0.0f;
};

// RBJ shelving and peaking filters for the global EQ
struct EqBand
{
    Biquad b;
    float last = 1.0e9f;
    void lowShelf (float db, float fc, float fs) { if (db == last) return; last = db; shelf (db, fc, fs, true); }
    void highShelf (float db, float fc, float fs) { if (db == last) return; last = db; shelf (db, fc, fs, false); }
    void peak (float db, float fc, float q, float fs)
    {
        if (db == last) return;
        last = db;
        const float A = std::pow (10.0f, db / 40.0f), w = kTwoPi * fc / fs, al = std::sin (w) / (2.0f * q), c = std::cos (w);
        const float a0 = 1.0f + al / A;
        b.b0 = (1.0f + al * A) / a0; b.b1 = -2.0f * c / a0; b.b2 = (1.0f - al * A) / a0;
        b.a1 = -2.0f * c / a0; b.a2 = (1.0f - al / A) / a0;
    }
    void shelf (float db, float fc, float fs, bool low)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = kTwoPi * fc / fs, c = std::cos (w), s = std::sin (w);
        const float al = s / 2.0f * std::sqrt (2.0f), sa = 2.0f * std::sqrt (A) * al;
        float b0, b1, b2, a0, a1, a2;
        if (low)
        {
            b0 = A * ((A + 1) - (A - 1) * c + sa); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sa);
            a0 = (A + 1) + (A - 1) * c + sa; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sa;
        }
        else
        {
            b0 = A * ((A + 1) + (A - 1) * c + sa); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sa);
            a0 = (A + 1) - (A - 1) * c + sa; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sa;
        }
        b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0;
    }
    float process (float x) { return b.process (x); }
};

class Engine
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        delayA.prepare (fs, 0);
        delayB.prepare (fs, 1);
        reverb.prepare (fs);
        looper.prepare (fs);
        for (auto* s : { &gA, &gB, &gR, &tA, &tB, &tR, &gLoop }) s->setTime (0.03f, fs);
        for (int c = 0; c < 2; ++c)
        {
            eqLow[(size_t) c].last = eqMid[(size_t) c].last = eqHigh[(size_t) c].last = 1.0e9f;
        }
    }

    void reset() { delayA.reset(); delayB.reset(); reverb.reset(); looper.clear(); }

    // Kill tails: fade every delay repeat and reverb tail out over 30 ms, clear them, and fade
    // the sections back in, so a runaway feedback loop stops at once. Dry and looper keep going.
    void killTails() { killRequest.store (true); }

    static float mixGain (float knob) { return 1.25f * std::pow (clampf (knob, 0.0f, 1.0f), 1.5f); }

    void process (float* L, float* R, int n, const EngineParams& p)
    {
        for (int c = 0; c < 2; ++c)
        {
            eqLow[(size_t) c].lowShelf (p.eqLowDb, 120.0f, fs);
            eqMid[(size_t) c].peak (p.eqMidDb, 800.0f, 0.8f, fs);
            eqHigh[(size_t) c].highShelf (p.eqHighDb, 5000.0f, fs);
        }
        const float loopLevel = 1.4f * p.loopLevel;

        if (killRequest.exchange (false)) killing = true;
        const float killOut = 1.0f / (0.03f * fs), killIn = 1.0f / (0.02f * fs);

        for (int i = 0; i < n; ++i)
        {
            if (killing)
            {
                killGain -= killOut;
                if (killGain <= 0.0f)
                {
                    killGain = 0.0f;
                    killing = false;
                    delayA.clearTails(); delayB.clearTails(); reverb.clearTails();
                }
            }
            else if (killGain < 1.0f) killGain = std::min (1.0f, killGain + killIn);
            const float dryL = L[i], dryR = R[i];
            const float duckSrc = 0.5f * (dryL + dryR);
            float xL = dryL, xR = dryR;

            // a module switched off keeps ringing unless trails are off
            const float ga = mixGain (p.mixA) * tA.next (p.a.on || p.trails ? 1.0f : 0.0f);
            const float gb = mixGain (p.mixB) * tB.next (p.b.on || p.trails ? 1.0f : 0.0f);
            const float gr = mixGain (p.mixR) * tR.next (p.r.on || p.trails ? 1.0f : 0.0f);
            const float wa = gA.next (ga) * killGain, wb = gB.next (gb) * killGain, wr = gR.next (gr) * killGain;

            float lpL = 0.0f, lpR = 0.0f;
            auto loopAt = [&] (int where, float& sl, float& sr)
            {
                if (p.loopPos != where) return;
                looper.process (sl, sr, loopLevel, lpL, lpR);
                sl += lpL; sr += lpR;
            };

            loopAt (LoopPre, xL, xR);

            auto runA = [&] (float inL, float inR, float& oL, float& oR) { float l, r; delayA.process (inL, inR, duckSrc, p.a, l, r); oL = l * wa; oR = r * wa; };
            auto runB = [&] (float inL, float inR, float& oL, float& oR) { float l, r; delayB.process (inL, inR, duckSrc, p.b, l, r); oL = l * wb; oR = r * wb; };
            auto runR = [&] (float inL, float inR, float& oL, float& oR) { float l, r; reverb.process (inL, inR, p.r, l, r); oL = l * wr; oR = r * wr; };

            float aL, aR, bL, bR, rL, rR;
            switch (p.route)
            {
                default:
                case RouteSeries:
                    runA (xL, xR, aL, aR); xL += aL; xR += aR;
                    runB (xL, xR, bL, bR); xL += bL; xR += bR;
                    loopAt (LoopMid, xL, xR);
                    runR (xL, xR, rL, rR); xL += rL; xR += rR;
                    break;
                case RouteDelaysParallel:
                    runA (xL, xR, aL, aR); runB (xL, xR, bL, bR);
                    xL += aL + bL; xR += aR + bR;
                    loopAt (LoopMid, xL, xR);
                    runR (xL, xR, rL, rR); xL += rL; xR += rR;
                    break;
                case RouteReverbFirst:
                    runR (xL, xR, rL, rR); xL += rL; xR += rR;
                    loopAt (LoopMid, xL, xR);
                    runA (xL, xR, aL, aR); runB (xL, xR, bL, bR);
                    xL += aL + bL; xR += aR + bR;
                    break;
                case RouteAllParallel:
                    runA (xL, xR, aL, aR); runB (xL, xR, bL, bR); runR (xL, xR, rL, rR);
                    xL += aL + bL + rL; xR += aR + bR + rR;
                    loopAt (LoopMid, xL, xR);
                    break;
            }
            loopAt (LoopPost, xL, xR);

            // everything that isn't the dry signal
            float wL = xL - dryL, wR = xR - dryR;
            float dL = dryL, dR = dryR;
            if (p.eqOn)
            {
                wL = eqHigh[0].process (eqMid[0].process (eqLow[0].process (wL)));
                wR = eqHigh[1].process (eqMid[1].process (eqLow[1].process (wR)));
                if (p.eqOnDry)
                {
                    dL = eqHigh2[0].process (eqMid2[0].process (eqLow2[0].process (dL)));
                    dR = eqHigh2[1].process (eqMid2[1].process (eqLow2[1].process (dR)));
                }
            }
            // a gentle ceiling on the effects only
            wL = 2.0f * softClip (wL * 0.5f);
            wR = 2.0f * softClip (wR * 0.5f);
            if (! std::isfinite (wL) || ! std::isfinite (wR)) { wL = wR = 0.0f; reset(); }

            L[i] = (p.killDry ? 0.0f : dL) + wL;
            R[i] = (p.killDry ? 0.0f : dR) + wR;
        }
        if (p.eqOn && p.eqOnDry)
            for (int c = 0; c < 2; ++c)
            {
                eqLow2[(size_t) c].lowShelf (p.eqLowDb, 120.0f, fs);
                eqMid2[(size_t) c].peak (p.eqMidDb, 800.0f, 0.8f, fs);
                eqHigh2[(size_t) c].highShelf (p.eqHighDb, 5000.0f, fs);
            }
    }

    DelayEngine delayA, delayB;
    ReverbEngine reverb;
    Looper looper;

private:
    float fs = 48000.0f;
    std::atomic<bool> killRequest { false };
    bool killing = false;
    float killGain = 1.0f;
    Smoother gA, gB, gR, tA, tB, tR, gLoop;
    std::array<EqBand, 2> eqLow, eqMid, eqHigh, eqLow2, eqMid2, eqHigh2;
};
} // namespace kdd
