#pragma once

// Stereo looper: 120 seconds, record, play, overdub, stop, clear, half speed, reverse,
// and undo / redo of the last overdub (the pedal has no undo). The last overdub is kept
// in its own layer until the next overdub starts, so undo costs nothing.

#include "DspUtil.h"
#include <array>
#include <atomic>
#include <vector>

namespace kdd
{
class Looper
{
public:
    enum State { Empty = 0, Recording, Playing, Overdubbing, Stopped };
    enum Command { CmdNone = 0, CmdMain, CmdStop, CmdClear, CmdUndo, CmdHalf, CmdReverse, CmdStopRevert };

    static constexpr float kMaxSeconds = 120.0f;

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        cap = (int) (kMaxSeconds * fs);
        for (int c = 0; c < 2; ++c)
        {
            base[(size_t) c].assign ((size_t) cap, 0.0f);
            layer[(size_t) c].assign ((size_t) cap, 0.0f);
            subGuard[(size_t) c].highpass (60.0f, 0.7071f, fs);
        }
        fadeLen = std::max (1, (int) (0.008f * fs));
        clear();
    }

    void clear()
    {
        state = Empty;
        length = 0;
        pos = 0.0;
        layerOn = false;
        layerHasAudio = false;
        mergeFrom = -1;
        gain = 0.0f;
        publish();
    }

    // Commands arrive from the message thread through an atomic slot per command
    void post (Command c) { pending.store ((int) c); }

    void setHalf (bool h) { half = h; }
    void setReverse (bool r) { reverse = r; }
    bool isHalf() const { return half; }
    bool isReverse() const { return reverse; }

    // inL/inR is what gets recorded; returns the loop's playback, at loop level
    void process (float inL, float inR, float level, float& outL, float& outR)
    {
        const int cmd = pending.exchange (CmdNone);
        if (cmd != CmdNone) handle ((Command) cmd);

        // finish merging the previous overdub into the base, a slice per sample
        if (mergeFrom >= 0)
            for (int k = 0; k < 64 && mergeFrom < length; ++k, ++mergeFrom)
                for (int c = 0; c < 2; ++c)
                {
                    base[(size_t) c][(size_t) mergeFrom] += layer[(size_t) c][(size_t) mergeFrom];
                    layer[(size_t) c][(size_t) mergeFrom] = 0.0f;
                }
        if (mergeFrom >= length) mergeFrom = -1;

        outL = outR = 0.0f;
        const float gTarget = (state == Playing || state == Overdubbing) ? 1.0f : 0.0f;
        gain += (gTarget - gain) * 0.002f;

        if (state == Recording)
        {
            if (length < cap)
            {
                base[0][(size_t) length] = inL;
                base[1][(size_t) length] = inR;
                ++length;
            }
            else startPlaying();
            publish();
            return;
        }

        if (length <= 0 || (state != Playing && state != Overdubbing && gain < 1.0e-4f)) { publish(); return; }

        const double speed = (half ? 0.5 : 1.0) * (reverse ? -1.0 : 1.0);
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % length;
        const float f = (float) (pos - (double) i0);
        float l = sampleAt (0, i0) * (1.0f - f) + sampleAt (0, i1) * f;
        float r = sampleAt (1, i0) * (1.0f - f) + sampleAt (1, i1) * f;

        // short fade at the loop point so the seam never clicks
        const int edge = std::min (i0, length - 1 - i0);
        if (edge < fadeLen && length > fadeLen * 4) { const float w = (float) edge / (float) fadeLen; l *= w; r *= w; }

        if (state == Overdubbing && i0 >= 0 && i0 < length)
        {
            layer[0][(size_t) i0] += inL;
            layer[1][(size_t) i0] += inR;
            layerHasAudio = true;
        }

        if (half) { l = subGuard[0].process (l); r = subGuard[1].process (r); }
        outL = l * gain * level;
        outR = r * gain * level;

        pos += speed;
        while (pos >= (double) length) pos -= (double) length;
        while (pos < 0.0) pos += (double) length;
        publish();
    }

    // read by the UI
    std::atomic<int> uiState { Empty };
    std::atomic<float> uiPos { 0.0f }, uiLength { 0.0f };
    std::atomic<bool> uiCanUndo { false }, uiLayerOn { false };

private:
    float sampleAt (int c, int i) const
    {
        float v = base[(size_t) c][(size_t) i];
        if (layerOn) v += layer[(size_t) c][(size_t) i];
        return v;
    }

    void startPlaying()
    {
        state = Playing;
        pos = 0.0;
        gain = 1.0f;
    }

    void beginOverdub()
    {
        // fold the previous overdub into the base before starting a new one
        if (layerHasAudio)
        {
            if (layerOn)
            {
                if (mergeFrom < 0) mergeFrom = 0;
                for (; mergeFrom < length; ++mergeFrom)
                    for (int c = 0; c < 2; ++c)
                    {
                        base[(size_t) c][(size_t) mergeFrom] += layer[(size_t) c][(size_t) mergeFrom];
                        layer[(size_t) c][(size_t) mergeFrom] = 0.0f;
                    }
            }
            else
                for (int c = 0; c < 2; ++c) std::fill (layer[(size_t) c].begin(), layer[(size_t) c].begin() + length, 0.0f);
            mergeFrom = -1;
        }
        layerHasAudio = false;
        layerOn = true;
        state = Overdubbing;
    }

    void handle (Command c)
    {
        switch (c)
        {
            case CmdMain:
                if (state == Empty) { state = Recording; length = 0; }
                else if (state == Recording) { if (length < (int) (0.05f * fs)) clear(); else startPlaying(); }
                else if (state == Playing) beginOverdub();
                else if (state == Overdubbing) state = Playing;
                else if (state == Stopped) { state = Playing; pos = reverse ? (double) (length - 1) : 0.0; }
                break;
            case CmdStop:
                if (state == Recording) { if (length < (int) (0.05f * fs)) clear(); else { startPlaying(); state = Stopped; } }
                else if (state == Playing || state == Overdubbing) state = Stopped;
                break;
            case CmdStopRevert:
                // double tap: the first tap's change is taken back, then stop
                if (state == Recording) clear();
                else if (state == Overdubbing) { layerOn = false; state = Stopped; }
                else if (state == Playing || state == Stopped) state = Stopped;
                break;
            case CmdClear: clear(); break;
            case CmdUndo:
                if (state == Overdubbing) state = Playing;
                if (layerHasAudio) layerOn = ! layerOn;
                else if (state == Playing || state == Stopped) { /* nothing to undo */ }
                break;
            case CmdHalf: half = ! half; break;
            case CmdReverse: reverse = ! reverse; break;
            default: break;
        }
    }

    void publish()
    {
        uiState.store (state);
        uiPos.store (length > 0 ? (float) (pos / (double) length) : 0.0f);
        uiLength.store ((float) length / fs);
        uiCanUndo.store (layerHasAudio);
        uiLayerOn.store (layerOn && layerHasAudio);
    }

    float fs = 48000.0f;
    int cap = 0, length = 0, fadeLen = 400, mergeFrom = -1;
    double pos = 0.0;
    State state = Empty;
    bool half = false, reverse = false, layerOn = false, layerHasAudio = false;
    float gain = 0.0f;
    std::array<std::vector<float>, 2> base, layer;
    std::array<Biquad, 2> subGuard;
    std::atomic<int> pending { CmdNone };
};
} // namespace kdd
