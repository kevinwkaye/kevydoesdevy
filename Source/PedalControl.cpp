#include "PedalControl.h"

using namespace juce;

// =====================================================================================
// Bindings and settings
// =====================================================================================
bool Binding::matches (const MidiMessage& m, bool anyChannel) const
{
    juce::ignoreUnused (anyChannel);
    if (channel != 0 && m.getChannel() != channel) return false;   // a binding with a channel is strict, 0 means any
    switch (kind)
    {
        case CC: return m.isController() && m.getControllerNumber() == number;
        case Program: return m.isProgramChange() && m.getProgramChangeNumber() == number;
        case Note: return (m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() == number;
        default: return false;
    }
}

String Binding::describe() const
{
    const String what = kind == CC ? "CC " + String (number) : (kind == Program ? "PC " + String (number + 1) : "NOTE " + String (number));
    return what + (channel == 0 ? "" : " CH " + String (channel));
}

File PedalSettings::file()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile ("Kevy does Devy").getChildFile ("Pedals.xml");
}

void PedalSettings::resetFootswitches()
{
    fs[0] = { { Binding::CC, 0, 17 }, { Binding::CC, 0, 102 }, { Binding::CC, 16, 20 } };
    fs[1] = { { Binding::CC, 0, 18 }, { Binding::CC, 0, 103 }, { Binding::CC, 16, 21 } };
    fs[2] = { { Binding::CC, 0, 19 }, { Binding::CC, 0, 104 }, { Binding::CC, 16, 22 } };
    fs[3] = { { Binding::CC, 16, 23 } };
}

void PedalSettings::load()
{
    auto xml = parseXML (file());
    if (xml == nullptr || ! xml->hasTagName ("Pedals")) return;
    for (int i = 0; i < kSwitches; ++i)
        if (auto* sw = xml->getChildByName ("Switch" + String (i)))
        {
            std::vector<Binding> list;
            for (auto* b : sw->getChildIterator())
                list.push_back ({ jlimit (0, 2, b->getIntAttribute ("kind")), jlimit (0, 16, b->getIntAttribute ("channel")),
                                  jlimit (0, 127, b->getIntAttribute ("number")) });
            fs[(size_t) i] = list;
        }
    expChannel = jlimit (0, 16, xml->getIntAttribute ("expChannel", expChannel));
    expCC = jlimit (0, 127, xml->getIntAttribute ("expCC", expCC));
    heel = jlimit (0, 127, xml->getIntAttribute ("heel", heel));
    toe = jlimit (0, 127, xml->getIntAttribute ("toe", toe));
    curve = jlimit (0, 2, xml->getIntAttribute ("curve", curve));
    footDevice = xml->getStringAttribute ("footDevice", footDevice);
    ccMaps.clear();
    if (auto* maps = xml->getChildByName ("MidiLearn"))
        for (auto* m : maps->getChildIterator())
            ccMaps.push_back ({ jlimit (0, 16, m->getIntAttribute ("channel")), jlimit (0, 127, m->getIntAttribute ("cc")), m->getStringAttribute ("param") });
}

void PedalSettings::save() const
{
    XmlElement xml ("Pedals");
    for (int i = 0; i < kSwitches; ++i)
    {
        auto* sw = xml.createNewChildElement ("Switch" + String (i));
        for (auto& b : fs[(size_t) i])
        {
            auto* e = sw->createNewChildElement ("Binding");
            e->setAttribute ("kind", b.kind);
            e->setAttribute ("channel", b.channel);
            e->setAttribute ("number", b.number);
        }
    }
    xml.setAttribute ("expChannel", expChannel);
    xml.setAttribute ("expCC", expCC);
    xml.setAttribute ("heel", heel);
    xml.setAttribute ("toe", toe);
    xml.setAttribute ("curve", curve);
    xml.setAttribute ("footDevice", footDevice);
    auto* maps = xml.createNewChildElement ("MidiLearn");
    for (auto& m : ccMaps)
    {
        auto* e = maps->createNewChildElement ("Map");
        e->setAttribute ("channel", m.channel);
        e->setAttribute ("cc", m.cc);
        e->setAttribute ("param", m.param);
    }
    file().getParentDirectory().createDirectory();
    xml.writeTo (file());
}

float PedalSettings::expressionValue (int cc) const
{
    const float span = (float) (toe - heel);
    float v = std::abs (span) < 4.0f ? (float) cc / 127.0f : ((float) cc - (float) heel) / span;
    v = jlimit (0.0f, 1.0f, v);
    if (curve == 1) v = v * v;
    else if (curve == 2) v = std::sqrt (v);
    return v;
}

// =====================================================================================
// Taps, double taps, holds and combos
// =====================================================================================
void FootswitchLogic::press (int sw, double now)
{
    if (sw < 0 || sw >= N || down[(size_t) sw]) return;
    down[(size_t) sw] = true;
    held[(size_t) sw] = false;
    consumed[(size_t) sw] = false;
    downAt[(size_t) sw] = now;

    // two switches pressed together: a combo, and neither switch does its own job
    for (int j = 0; j < N; ++j)
    {
        if (j == sw || consumed[(size_t) j] || held[(size_t) j]) continue;
        const bool recent = (down[(size_t) j] || pending[(size_t) j]) && now - downAt[(size_t) j] < kComboWindow;
        if (recent)
        {
            consumed[(size_t) sw] = consumed[(size_t) j] = true;
            pending[(size_t) j] = false;
            if (onEvent) onEvent ((1 << sw) | (1 << j), Combo, now);
            return;
        }
    }
}

void FootswitchLogic::release (int sw, double now)
{
    if (sw < 0 || sw >= N || ! down[(size_t) sw]) return;
    tick (now);
    down[(size_t) sw] = false;
    if (consumed[(size_t) sw]) return;
    if (held[(size_t) sw]) { if (onEvent) onEvent (sw, HoldEnd, downAt[(size_t) sw]); return; }
    // wait out the combo window before acting
    pending[(size_t) sw] = true;
    fireAt[(size_t) sw] = std::max (now, downAt[(size_t) sw] + kComboWindow);
}

void FootswitchLogic::tick (double now)
{
    for (int i = 0; i < N; ++i)
    {
        if (pending[(size_t) i] && now >= fireAt[(size_t) i])
        {
            pending[(size_t) i] = false;
            const double t = downAt[(size_t) i];
            const bool isDouble = t - lastTapAt[(size_t) i] < kDoubleWindow;
            lastTapAt[(size_t) i] = isDouble ? -10.0 : t;
            if (onEvent) onEvent (i, isDouble ? DoubleTap : Tap, t);
        }
        if (down[(size_t) i] && ! held[(size_t) i] && ! consumed[(size_t) i] && now - downAt[(size_t) i] >= kHoldSeconds)
        {
            held[(size_t) i] = true;
            lastTapAt[(size_t) i] = -10.0;
            if (onEvent) onEvent (i, HoldStart, downAt[(size_t) i]);
        }
    }
}

// =====================================================================================
// Direct MIDI input (from Prism Patch)
// =====================================================================================
DirectMidiInput::DirectMidiInput()
{
    refresh();
    startTimer (2000);
}

DirectMidiInput::~DirectMidiInput()
{
    stopTimer();
    const ScopedLock l (lock);
    for (auto& in : inputs) in->stop();
    inputs.clear();
}

bool DirectMidiInput::looksLikeFootswitch (const String& name)
{
    const auto n = name.toLowerCase();
    for (auto* skip : { "keyboard", "keys", "piano", "mixer", "pad", "smk", "launch", "maschine", "push", "loopmidi", "loopbe" })
        if (n.contains (skip)) return false;
    for (auto* want : { "anagram", "darkglass", "footctrl", "foot ctrl", "footswitch", "foot switch", "foot controller", "chocolate",
                        "m-vave", "mvave", "cuvave", "pedal" })
        if (n.contains (want)) return true;
    return false;
}

StringArray DirectMidiInput::deviceNames()
{
    StringArray names;
    for (const auto& d : MidiInput::getAvailableDevices()) names.add (d.name);
    return names;
}

void DirectMidiInput::setWanted (const String& device)
{
    {
        const ScopedLock l (lock);
        if (device == wanted) return;
        wanted = device;
    }
    refresh();
}

void DirectMidiInput::refresh()
{
    const auto available = MidiInput::getAvailableDevices();
    const ScopedLock l (lock);
    auto want = [this] (const String& name)
    {
        if (wanted == "none") return false;
        return wanted.isEmpty() ? looksLikeFootswitch (name) : name == wanted;
    };

    for (int i = (int) inputs.size(); --i >= 0;)
    {
        bool present = false;
        for (const auto& d : available) present = present || d.identifier == inputs[(size_t) i]->getIdentifier();
        if (! present || ! want (inputs[(size_t) i]->getName()))
        {
            inputs[(size_t) i]->stop();
            inputs.erase (inputs.begin() + i);
        }
    }

    busy.clear();
    for (const auto& d : available)
    {
        if (! want (d.name)) continue;
        bool open = false;
        for (auto& in : inputs) open = open || in->getIdentifier() == d.identifier;
        if (open) continue;
        if (auto in = MidiInput::openDevice (d.identifier, this))
        {
            in->start();
            inputs.push_back (std::move (in));
        }
        else busy.add (d.name); // on Windows only one program can hold a MIDI input
    }
    isConnected.store (! inputs.empty());
}

String DirectMidiInput::status() const
{
    const ScopedLock l (lock);
    if (wanted == "none") return "Off: footswitch MIDI comes through your DAW";
    if (! inputs.empty())
    {
        StringArray names;
        for (auto& in : inputs) names.add (in->getName());
        return "Connected: " + names.joinIntoString (", ");
    }
    if (! busy.isEmpty()) return busy[0] + " is in use by your DAW. Switch its MIDI input off in the DAW's MIDI settings";
    return wanted.isEmpty() ? "No footswitch found: plug it in, or pick it below"
                            : wanted + " isn't plugged in";
}

StringArray DirectMidiInput::openDevices() const
{
    const ScopedLock l (lock);
    StringArray names;
    for (auto& in : inputs) names.add (in->getName());
    return names;
}

void DirectMidiInput::handleIncomingMidiMessage (MidiInput*, const MidiMessage& m)
{
    if (auto s = sink.load()) s (m);
}
