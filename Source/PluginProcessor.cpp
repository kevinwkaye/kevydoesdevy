#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several copies can be open at once. Footswitches drive one at a time: the one whose panel was
// last touched, or the one the DAW sends footswitch MIDI to (the Prism Patch scheme).
namespace
{
SpinLock focusLock;
Array<KddProcessor*> instances;
std::atomic<KddProcessor*> focused { nullptr };
thread_local bool fromDeviceNow = false;

void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    KddProcessor* target = nullptr;
    for (auto* p : instances) if (p->isTeachingOrLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}

double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
const char* const kModuleOn[3] = { ParamIDs::aOn, ParamIDs::bOn, ParamIDs::rOn };
const char* const kModuleInf[3] = { ParamIDs::aInfinite, ParamIDs::bInfinite, ParamIDs::rFreeze };
const char* const kModuleName[3] = { "DELAY A", "DELAY B", "REVERB" };
} // namespace

KddProcessor::KddProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KevyDoesDevy", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    for (auto& w : uiWet) w.store (0.0f);
    initSlots();
    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e, double t) { onFootEvent (sw, e, t); };
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (100);
}

KddProcessor::~KddProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

bool KddProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo()) return false;
    return in == AudioChannelSet::mono() || in == AudioChannelSet::stereo();
}

void KddProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateNow = sampleRate;
    engine.prepare ((float) sampleRate);
}

kdd::EngineParams KddProcessor::readParams()
{
    using namespace ParamIDs;
    kdd::EngineParams p;
    auto f = [this] (const char* id) { return raw.at (id)->load(); };
    auto i = [&f] (const char* id) { return (int) std::lround (f (id)); };
    auto b = [&f] (const char* id) { return f (id) > 0.5f; };

    double tempo = f (bpm);
    if (b (syncHost) && hostBpm.load() > 20.0) tempo = hostBpm.load();
    const double beatMs = 60000.0 / std::max (20.0, tempo);
    const bool allOff = b (bypassAll);

    auto delay = [&] (kdd::DelayParams& d, const char* on, const char* type, const char* time, const char* fb, const char* tone,
                      const char* lowCut, const char* duck, const char* voice, const char* toneMode, const char* sub, const char* ping, const char* inf)
    {
        d.on = b (on) && ! allOff;
        d.position = i (type);
        const int s = i (sub);
        d.timeMs = s == 0 ? f (time) : (float) jlimit (10.0, 2000.0, beatMs * names::subdivBeats (s));
        d.feedback = f (fb);
        d.tone = f (tone);
        d.toneMode = i (toneMode);
        d.lowCutHz = f (lowCut);
        d.duck = f (duck);
        d.mk1 = i (voice) == 1;
        d.pingPong = b (ping);
        d.infinite = b (inf);
    };
    delay (p.a, aOn, aType, aTime, aFeedback, aTone, aLowCut, aDuck, aVoice, aToneMode, aSubdiv, aPing, aInfinite);
    delay (p.b, bOn, bType, bTime, bFeedback, bTone, bLowCut, bDuck, bVoice, bToneMode, bSubdiv, bPing, bInfinite);
    p.r.on = b (rOn) && ! allOff;
    p.r.position = i (rType);
    p.r.decay = f (rDecay);
    p.r.preDelayMs = f (rPreDelay);
    p.r.shimmer = f (rShimmer);
    p.r.tone = f (rTone);
    p.r.toneMode = i (rToneMode);
    p.r.lowCutHz = f (rLowCut);
    p.r.mk1 = i (rVoice) == 1;
    p.r.freeze = b (rFreeze);
    p.r.merge = b (merge);
    p.mixA = f (aMix); p.mixB = f (bMix); p.mixR = f (rMix);
    p.route = i (route);
    p.loopPos = i (loopPos);
    p.loopLevel = f (loopLevel);
    p.trails = b (trails);
    p.killDry = b (killDry);
    p.eqOn = b (eqOn);
    p.eqOnDry = b (eqDry);
    p.eqLowDb = f (eqLow); p.eqMidDb = f (eqMid); p.eqHighDb = f (eqHigh);
    return p;
}

void KddProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    if (inCh == 1 && buffer.getNumChannels() > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = inCh; c < buffer.getNumChannels(); ++c)
        if (! (inCh == 1 && c == 1)) buffer.clear (c, 0, n);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) hostBpm.store (*b);

    for (const auto meta : midi) handleMidi (meta.getMessage(), false);

    if (buffer.getNumChannels() < 2) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = std::max (peak, std::fabs (L[i]));
    uiIn.store (std::max (peak, uiIn.load() * 0.9f));

    const auto p = readParams();
    engine.process (L, R, n, p);
    uiWet[0].store (engine.delayA.wetLevel());
    uiWet[1].store (engine.delayB.wetLevel());
    uiWet[2].store (engine.reverb.wetLevel());
}

// =====================================================================================
// MIDI: footswitches, expression, MIDI Learn and presets
// =====================================================================================
void KddProcessor::queue (int kind, int a, int b)
{
    const Pending q { kind, a, b, nowSeconds() };
    if (fromDeviceNow)
    {
        const auto scope = directFifo.write (1);
        if (scope.blockSize1 > 0) directPending[(size_t) scope.startIndex1] = q;
        return;
    }
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) pending[(size_t) scope.startIndex1] = q;
}

void KddProcessor::handleDirectMidi (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true);
    fromDeviceNow = false;
}

void KddProcessor::handleMidi (const MidiMessage& m, bool fromDevice)
{
    const bool isCC = m.isController(), isPC = m.isProgramChange(), isNoteOn = m.isNoteOn(), isNoteOff = m.isNoteOff();
    if (! isCC && ! isPC && ! isNoteOn && ! isNoteOff) return;
    const int ch = m.getChannel();

    // Teach my pedal and Learn take the next message, whatever it is
    if (teach.load() > 0 || learnTarget.load() != LearnNone)
    {
        if ((isCC && m.getControllerValue() > 0) || isPC || isNoteOn)
        {
            const int kind = isCC ? Binding::CC : (isPC ? Binding::Program : Binding::Note);
            const int number = isCC ? m.getControllerNumber() : (isPC ? m.getProgramChangeNumber() : m.getNoteNumber());
            queue (Learned, kind, ch * 128 + number);
        }
        return;
    }

    // When the device is read directly, the same messages arriving through the DAW are ignored
    if (fromDevice || ! pedalsDirect())
    {
        int foot = -1;
        const Binding* hit = nullptr;
        for (int k = 0; k < PedalSettings::kSwitches && foot < 0; ++k)
            for (auto& bnd : settings.fs[(size_t) k])
                if (bnd.matches (m, fromDevice)) { foot = k; hit = &bnd; break; }
        const bool expression = foot < 0 && isCC && m.getControllerNumber() == settings.expCC
                                && (settings.expChannel == 0 || ch == settings.expChannel);
        if (foot >= 0 || expression)
        {
            if (! fromDevice)
            {
                // the copy the DAW sends footswitch MIDI to takes charge
                hostFeedsPedals.store (true);
                const SpinLock::ScopedLockType l (focusLock);
                auto* f = focused.load();
                if (f != this && (f == nullptr || ! f->hostFeedsPedals.load())) focused.store (this);
                if (focused.load() != this) return;
            }
            if (foot >= 0)
            {
                if (hit->kind == Binding::CC) queue (FootCCKind, foot, m.getControllerValue());
                else if (hit->kind == Binding::Program) queue (FootTrigger, foot, 0);
                else queue (isNoteOn ? FootPress : FootRelease, foot, 0);
            }
            else queue (ExpressionKind, m.getControllerValue(), 0);
            return;
        }
    }

    if (isCC)
        for (int k = 0; k < (int) settings.ccMaps.size(); ++k)
        {
            const auto& map = settings.ccMaps[(size_t) k];
            if (map.cc == m.getControllerNumber() && (map.channel == 0 || map.channel == ch))
            {
                queue (CcMapKind, k, m.getControllerValue());
                return;
            }
        }

    // program changes on channel 16 pick presets 1 to 24
    if (isPC && ch == 16) queue (ProgramKind, m.getProgramChangeNumber(), 0);
}

void KddProcessor::footCC (int sw, int value, double t)
{
    auto& s = sense[(size_t) sw];
    const bool high = value >= 64;
    if (high)
    {
        if (s.momentary) footswitches.press (sw, t);
        else footswitches.trigger (sw, t);
        s.lastHighAt = t;
        s.lastWasHigh = true;
    }
    else
    {
        if (s.momentary) footswitches.release (sw, t);
        else if (s.lastWasHigh && t - s.lastHighAt < 0.35) s.momentary = true;   // that was the release of a momentary switch
        else footswitches.trigger (sw, t);                                       // a toggling switch's "off" message is a press too
        s.lastWasHigh = false;
    }
}

void KddProcessor::timerCallback()
{
    const double now = nowSeconds();
    if (now >= nextSettingsCheck)
    {
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone && teach.load() == 0 && ! calibrating)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
    }

    auto apply = [&] (const std::array<Pending, 512>& from, int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = from[(size_t) i];
            switch (q.kind)
            {
                case FootCCKind: footCC (q.a, q.b, q.t); lastFoot.store (q.t); break;
                case FootTrigger: footswitches.trigger (q.a, q.t); lastFoot.store (q.t); break;
                case FootPress: footswitches.press (q.a, q.t); lastFoot.store (q.t); break;
                case FootRelease: footswitches.release (q.a, q.t); break;
                case ExpressionKind: applyExpression (q.a); break;
                case CcMapKind:
                    if (q.a < (int) settings.ccMaps.size()) setParamNorm (settings.ccMaps[(size_t) q.a].param, (float) q.b / 127.0f);
                    break;
                case ProgramKind: if (q.a < kSlots) loadSlot (q.a); break;
                case Learned:
                {
                    const int kind = q.a, ch = q.b / 128, number = q.b % 128;
                    const int step = teach.load();
                    if (step > 0)
                    {
                        // Teach my pedal: the message each switch sends replaces its defaults.
                        // Straight from the device the channel doesn't matter.
                        const int sw = step - 1;
                        settings.fs[(size_t) sw] = { { kind, pedalsDirect() ? 0 : ch, number } };
                        sense[(size_t) sw] = {};
                        // ignore the same switch's release
                        if (step < 3) { teach.store (step + 1); say ("GOT " + String::charToString ((juce_wchar) ('A' + sw)) + ". NOW "
                                                                     + String::charToString ((juce_wchar) ('B' + sw)) + "!"); }
                        else { teach.store (0); say ("ALL LEARNT. NICE!"); }
                        savePedals();
                        break;
                    }
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < PedalSettings::kSwitches)
                    {
                        settings.fs[(size_t) what] = { { kind, pedalsDirect() ? 0 : ch, number } };
                        sense[(size_t) what] = {};
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + ": " + settings.fs[(size_t) what][0].describe());
                    }
                    else if (what == LearnExpression && kind == Binding::CC)
                    {
                        settings.expChannel = pedalsDirect() ? 0 : ch;
                        settings.expCC = number;
                        say ("EXPRESSION IS CC " + String (number));
                    }
                    else if (what == LearnParam && kind == Binding::CC)
                    {
                        auto& maps = settings.ccMaps;
                        maps.erase (std::remove_if (maps.begin(), maps.end(), [&] (const PedalSettings::CcMap& c)
                                                    { return c.param == learnParamId || (c.cc == number && (c.channel == 0 || c.channel == ch)); }), maps.end());
                        maps.push_back ({ pedalsDirect() ? 0 : ch, number, learnParamId });
                        if (auto* prm = apvts.getParameter (learnParamId)) say (prm->getName (24).toUpperCase() + " IS CC " + String (number));
                    }
                    savePedals();
                    break;
                }
                default: break;
            }
        }
    };
    {
        const auto scope = fifo.read (fifo.getNumReady());
        apply (pending, scope.startIndex1, scope.blockSize1);
        apply (pending, scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = directFifo.read (directFifo.getNumReady());
        apply (directPending, scope.startIndex1, scope.blockSize1);
        apply (directPending, scope.startIndex2, scope.blockSize2);
    }

    footswitches.tick (now);
    int mask = 0;
    for (int k = 0; k < PedalSettings::kSwitches; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo >= 8)
        {
            // which end the pedal started at tells us which way round it reads
            const bool reversed = calFirst >= 0 && calLast >= 0 && calFirst > calLast;
            settings.heel = reversed ? calHi : calLo;
            settings.toe = reversed ? calLo : calHi;
            savePedals();
            say ("EXPRESSION CALIBRATED");
        }
        else say ("NO MOVEMENT: TRY AGAIN");
    }
}

void KddProcessor::applyExpression (int cc)
{
    if (calibrating)
    {
        if (calFirst < 0) calFirst = cc;
        calLast = cc;
        calLo = jmin (calLo, cc);
        calHi = jmax (calHi, cc);
        uiExp.store ((float) cc / 127.0f);
        return;
    }
    const float v = settings.expressionValue (cc);
    uiExp.store (v);
    const auto& targets = names::expTargets();
    const int t = jlimit (0, (int) targets.size() - 1, (int) std::lround (getParam (ParamIDs::expTarget)));
    for (auto* id : targets[(size_t) t].ids) setParamNorm (id, v);
}

// =====================================================================================
// Footswitch jobs, mode by mode, as on the pedal
// =====================================================================================
String KddProcessor::modeName (int m)
{
    static const char* n[4] = { "PLAY", "TAP", "LOOPER", "PATCH" };
    return n[jlimit (0, 3, m)];
}

std::pair<String, String> KddProcessor::jobFor (int m, int sw)
{
    static const char* jobs[4][4][2] = {
        { { "Delay A on / off", "hold or 2x: infinite" }, { "Delay B on / off", "hold or 2x: infinite" }, { "Reverb on / off", "hold or 2x: freeze" }, { "Next mode", "" } },
        { { "Tap Delay A", "sets its own time" }, { "Tap Delay B", "sets its own time" }, { "Master tap", "delays on a note value follow" }, { "Next mode", "" } },
        { { "Reverse", "toggle" }, { "Half speed", "2x: undo last dub" }, { "Rec / play / dub", "2x: stop  hold: clear" }, { "Next mode", "" } },
        { { "Preset down", "8 banks of 3" }, { "Preset up", "8 banks of 3" }, { "Back to play", "" }, { "Next mode", "" } },
    };
    const auto& j = jobs[jlimit (0, 3, m)][jlimit (0, 3, sw)];
    return { j[0], j[1] };
}

void KddProcessor::setMode (int m)
{
    m = jlimit (0, 3, m);
    mode.store (m);
    say (modeName (m) + " MODE");
}

void KddProcessor::onFootEvent (int sw, FootswitchLogic::Event e, double t)
{
    using E = FootswitchLogic;
    const int m = mode.load();

    if (e == E::Combo)
    {
        // two switches together change mode; the same pair again goes back to Play
        int target = -1;
        if (sw == 0b011) target = ModeLooper;
        else if (sw == 0b110) target = ModeTap;
        else if (sw == 0b101) target = ModePatch;
        if (target >= 0) setMode (m == target ? ModePlay : target);
        return;
    }
    if (sw == 3)
    {
        if (e == E::Tap || e == E::DoubleTap) setMode ((m + 1) % 4);
        return;
    }

    switch (m)
    {
        case ModePlay:
            if (e == E::Tap) toggleModule (sw);
            else if (e == E::DoubleTap)
            {
                // the first tap of a double tap switched the module; put it back, then latch infinite
                toggleModule (sw);
                infLatched[(size_t) sw] = ! infLatched[(size_t) sw];
                setInfinite (sw, infLatched[(size_t) sw]);
            }
            else if (e == E::HoldStart) setInfinite (sw, true);
            else if (e == E::HoldEnd) setInfinite (sw, infLatched[(size_t) sw]);
            break;
        case ModeTap:
            if (e == E::Tap || e == E::DoubleTap)
            {
                if (sw == 2) tapMaster (t); else tapDelay (sw, t);
            }
            break;
        case ModeLooper:
            if (sw == 2)
            {
                if (e == E::Tap) looperCommand (kdd::Looper::CmdMain);
                else if (e == E::DoubleTap) looperCommand (kdd::Looper::CmdStopRevert);
                else if (e == E::HoldStart) looperCommand (kdd::Looper::CmdClear);
            }
            else if (sw == 1)
            {
                if (e == E::Tap) looperCommand (kdd::Looper::CmdHalf);
                else if (e == E::DoubleTap) { looperCommand (kdd::Looper::CmdHalf); looperCommand (kdd::Looper::CmdUndo); }
            }
            else if (e == E::Tap || e == E::DoubleTap) looperCommand (kdd::Looper::CmdReverse);
            break;
        case ModePatch:
            if (e == E::Tap || e == E::DoubleTap)
            {
                if (sw == 0) stepPreset (-1);
                else if (sw == 1) stepPreset (1);
                else setMode (ModePlay);
            }
            break;
        default: break;
    }
}

void KddProcessor::toggleModule (int module)
{
    module = jlimit (0, 2, module);
    const bool on = getParam (kModuleOn[module]) < 0.5f;
    setParamPlain (kModuleOn[module], on ? 1.0f : 0.0f);
    if (on && getParam (ParamIDs::bypassAll) > 0.5f) setParamPlain (ParamIDs::bypassAll, 0.0f);
    say (String (kModuleName[module]) + (on ? " ON" : " OFF"));
}

void KddProcessor::setInfinite (int module, bool on)
{
    module = jlimit (0, 2, module);
    setParamPlain (kModuleInf[module], on ? 1.0f : 0.0f);
    if (! on) infLatched[(size_t) module] = false;
}

bool KddProcessor::isInfinite (int module) const { return getParam (kModuleInf[jlimit (0, 2, module)]) > 0.5f; }

void KddProcessor::toggleBypassAll()
{
    const bool off = getParam (ParamIDs::bypassAll) < 0.5f;
    setParamPlain (ParamIDs::bypassAll, off ? 1.0f : 0.0f);
    say (off ? "ALL BYPASSED" : "ALL BACK ON");
}

// tap tempo: the average of the last few taps, reset after two seconds without one
static double tapInterval (double t, double& last, std::vector<double>& intervals)
{
    const double gap = t - last;
    last = t;
    if (gap > 2.0 || gap < 0.06) { intervals.clear(); return 0.0; }
    intervals.push_back (gap);
    if (intervals.size() > 4) intervals.erase (intervals.begin());
    double sum = 0.0;
    for (auto g : intervals) sum += g;
    return sum / (double) intervals.size();
}

void KddProcessor::tapMaster (double when)
{
    const double t = when < 0.0 ? nowSeconds() : when;
    const double g = tapInterval (t, lastTapAt[2], tapIntervals[2]);
    if (g <= 0.0) { say ("TAP AGAIN"); return; }
    const double bpmNow = jlimit (40.0, 240.0, 60.0 / g);
    setParamPlain (ParamIDs::bpm, (float) bpmNow);
    if (getParam (ParamIDs::syncHost) > 0.5f) setParamPlain (ParamIDs::syncHost, 0.0f);
    say ("TEMPO " + String (roundToInt (bpmNow)) + " BPM");
}

void KddProcessor::tapDelay (int module, double when)
{
    module = jlimit (0, 1, module);
    const double t = when < 0.0 ? nowSeconds() : when;
    const double g = tapInterval (t, lastTapAt[(size_t) module], tapIntervals[(size_t) module]);
    if (g <= 0.0) { say ("TAP AGAIN"); return; }
    const float ms = (float) jlimit (10.0, 2000.0, g * 1000.0);
    setParamPlain (module == 0 ? ParamIDs::aSubdiv : ParamIDs::bSubdiv, 0.0f);
    setParamPlain (module == 0 ? ParamIDs::aTime : ParamIDs::bTime, ms);
    say (String (kModuleName[module]) + " " + String (roundToInt (ms)) + " MS");
}

void KddProcessor::looperCommand (kdd::Looper::Command c)
{
    engine.looper.post (c);
    static const char* words[] = { "", "", "LOOP STOP", "LOOP CLEARED", "UNDO / REDO", "HALF SPEED", "REVERSE", "LOOP STOP" };
    if (c != kdd::Looper::CmdMain && c >= 0 && c <= kdd::Looper::CmdStopRevert) say (words[c]);
}

// =====================================================================================
// Presets
// =====================================================================================
static bool skipInPresets (const String& id)
{
    using namespace ParamIDs;
    return id == bypassAll || id == aInfinite || id == bInfinite || id == rFreeze || id == syncHost || id == expTarget;
}

void KddProcessor::initSlots()
{
    slots = ValueTree ("Slots");
    const auto& f = factoryPresets();
    for (int s = 0; s < kSlots; ++s)
    {
        ValueTree v ("Slot");
        if (s < (int) f.size())
        {
            v.setProperty ("name", f[(size_t) s].name, nullptr);
            v.setProperty ("note", f[(size_t) s].note, nullptr);
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
                {
                    const auto id = rp->getParameterID();
                    if (skipInPresets (id)) continue;
                    auto it = f[(size_t) s].values.find (id);
                    const float plain = it != f[(size_t) s].values.end() ? it->second : rp->convertFrom0to1 (rp->getDefaultValue());
                    v.setProperty (Identifier (id), plain, nullptr);
                }
        }
        slots.appendChild (v, nullptr);
    }
}

String KddProcessor::slotName (int s) const { return slots.getChild (jlimit (0, kSlots - 1, s)).getProperty ("name", "EMPTY").toString(); }
String KddProcessor::slotNote (int s) const { return slots.getChild (jlimit (0, kSlots - 1, s)).getProperty ("note", "").toString(); }

void KddProcessor::loadSlot (int s)
{
    s = jlimit (0, kSlots - 1, s);
    auto v = slots.getChild (s);
    if (! v.hasProperty ("name")) { say ("EMPTY SLOT"); return; }
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (skipInPresets (id) || ! v.hasProperty (Identifier (id))) continue;
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) v.getProperty (Identifier (id))));
        }
    slot.store (s);
    infLatched = {};
    say (slotName (s));
}

void KddProcessor::captureSlot (ValueTree& v)
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (skipInPresets (id)) continue;
            v.setProperty (Identifier (id), rp->convertFrom0to1 (rp->getValue()), nullptr);
        }
}

void KddProcessor::saveSlot (int s)
{
    s = jlimit (0, kSlots - 1, s);
    auto v = slots.getChild (s);
    if (! v.hasProperty ("name")) v.setProperty ("name", "SLOT " + String (s / 3 + 1) + String::charToString ((juce_wchar) ('A' + s % 3)), nullptr);
    v.setProperty ("note", "saved", nullptr);
    captureSlot (v);
    slot.store (s);
    say ("SAVED TO " + String (s / 3 + 1) + String::charToString ((juce_wchar) ('A' + s % 3)));
}

void KddProcessor::stepPreset (int direction)
{
    loadSlot ((slot.load() + direction + kSlots) % kSlots);
}

// =====================================================================================
// Pedal setup
// =====================================================================================
void KddProcessor::savePedals()
{
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

void KddProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    savePedals();
    direct->setWanted (device);
}

void KddProcessor::takeFootswitches() { focused.store (this); }

void KddProcessor::startTeach()
{
    focused.store (this);
    learnTarget.store (LearnNone);
    teach.store (1);
    say ("STOMP A!");
}

void KddProcessor::startLearn (int what, const String& paramId)
{
    focused.store (this);
    teach.store (0);
    learnParamId = paramId;
    learnTarget.store (what);
    say (what == LearnExpression ? "MOVE THE PEDAL" : (what == LearnParam ? "MOVE A KNOB ON YOUR CONTROLLER" : "PRESS THE SWITCH"));
}

String KddProcessor::midiLearnFor (const String& paramId) const
{
    for (auto& m : settings.ccMaps) if (m.param == paramId) return "CC " + String (m.cc);
    return {};
}

void KddProcessor::clearMidiLearn (const String& paramId)
{
    auto& maps = settings.ccMaps;
    maps.erase (std::remove_if (maps.begin(), maps.end(), [&] (const PedalSettings::CcMap& c) { return c.param == paramId; }), maps.end());
    savePedals();
}

void KddProcessor::startCalibration()
{
    calibrating = true;
    calibrateUntil = nowSeconds() + 6.0;
    calLo = 127; calHi = 0; calFirst = calLast = -1;
    say ("ROCK THE PEDAL HEEL TO TOE");
}

// =====================================================================================
// Parameters, messages and state
// =====================================================================================
void KddProcessor::setParamPlain (const String& id, float plain)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void KddProcessor::setParamNorm (const String& id, float norm)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (jlimit (0.0f, 1.0f, norm));
}

void KddProcessor::say (const String& text)
{
    const SpinLock::ScopedLockType l (sayLock);
    sayText = text;
    ++sayStamp;
}

String KddProcessor::latestMessage (int& stamp) const
{
    const SpinLock::ScopedLockType l (sayLock);
    stamp = sayStamp;
    return sayText;
}

void KddProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Slots"), nullptr);
    state.appendChild (slots.createCopy(), nullptr);
    state.setProperty ("slot", slot.load(), nullptr);
    state.setProperty ("mode", mode.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void KddProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;
        auto saved = state.getChildWithName ("Slots");
        if (saved.isValid() && saved.getNumChildren() == kSlots) slots = saved.createCopy();
        state.removeChild (saved, nullptr);
        slot.store (jlimit (0, kSlots - 1, (int) state.getProperty ("slot", 0)));
        mode.store (jlimit (0, 3, (int) state.getProperty ("mode", 0)));
        apvts.replaceState (state);
    }
}

AudioProcessorEditor* KddProcessor::createEditor() { return new KddEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KddProcessor(); }
