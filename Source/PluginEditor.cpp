#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

namespace ui
{
// =====================================================================================
// Fonts and drawing helpers
// =====================================================================================
Fonts::Fonts()
{
    titan = Typeface::createSystemTypefaceFor (BinaryData::TitanOneRegular_ttf, BinaryData::TitanOneRegular_ttfSize);
    archivo = Typeface::createSystemTypefaceFor (BinaryData::ArchivoBlackRegular_ttf, BinaryData::ArchivoBlackRegular_ttfSize);
    marker = Typeface::createSystemTypefaceFor (BinaryData::PermanentMarkerRegular_ttf, BinaryData::PermanentMarkerRegular_ttfSize);
}

const Fonts& fonts()
{
    static Fonts f;
    return f;
}

static float jitter (int seed, int k, float amount)
{
    uint32_t s = (uint32_t) (seed * 7919 + k * 104729 + 17);
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return ((float) (s % 1000) / 1000.0f * 2.0f - 1.0f) * amount;
}

Path wobblyRect (Rectangle<float> r, int seed, float radius)
{
    // irregular corner radii, like a hand-drawn box
    const float rr[4] = { radius + jitter (seed, 1, radius * 0.45f), radius + jitter (seed, 2, radius * 0.45f),
                          radius + jitter (seed, 3, radius * 0.45f), radius + jitter (seed, 4, radius * 0.45f) };
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    auto c = [&] (int i) { return jmin (rr[i], w * 0.5f, h * 0.5f); };
    Path p;
    p.startNewSubPath (x + c (0), y);
    p.lineTo (x + w - c (1), y + jitter (seed, 5, 0.8f));
    p.quadraticTo (x + w, y, x + w, y + c (1));
    p.lineTo (x + w + jitter (seed, 6, 0.8f), y + h - c (2));
    p.quadraticTo (x + w, y + h, x + w - c (2), y + h);
    p.lineTo (x + c (3), y + h + jitter (seed, 7, 0.8f));
    p.quadraticTo (x, y + h, x, y + h - c (3));
    p.lineTo (x + jitter (seed, 8, 0.8f), y + c (0));
    p.quadraticTo (x, y, x + c (0), y);
    p.closeSubPath();
    return p;
}

Path wobblyCircle (Point<float> c, float radius, int seed, float jit)
{
    const int n = 18;
    Point<float> pts[n];
    for (int i = 0; i < n; ++i)
    {
        const float a = MathConstants<float>::twoPi * (float) i / (float) n;
        const float r = radius + jitter (seed, i, jit);
        pts[i] = { c.x + std::cos (a) * r, c.y + std::sin (a) * r };
    }
    Path p;
    p.startNewSubPath ((pts[n - 1] + pts[0]) * 0.5f);
    for (int i = 0; i < n; ++i)
        p.quadraticTo (pts[i], (pts[i] + pts[(i + 1) % n]) * 0.5f);
    p.closeSubPath();
    return p;
}

void drawBox (Graphics& g, Rectangle<float> r, Colour fill, int seed, float shadow, float border, float radius)
{
    auto p = wobblyRect (r, seed, radius);
    if (shadow > 0.0f)
    {
        g.setColour (col::ink);
        g.fillPath (p, AffineTransform::translation (shadow, shadow));
    }
    g.setColour (fill);
    g.fillPath (p);
    g.setColour (col::ink);
    g.strokePath (p, PathStrokeType (border, PathStrokeType::curved, PathStrokeType::rounded));
}

void drawHatch (Graphics& g, Rectangle<float> r, Colour c, float spacing)
{
    Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (c);
    for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += spacing)
        g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 2.0f);
}

void chunkyText (Graphics& g, const String& text, const Font& font, Rectangle<float> area, Colour fill, Colour extrude,
                 int depth, float stroke, Justification j)
{
    GlyphArrangement ga;
    ga.addFittedText (font, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(), j, 1, 0.8f);
    Path p;
    ga.createPath (p);
    for (int d = depth; d >= 1; --d)
    {
        g.setColour (d >= depth - 1 ? col::ink : extrude);
        g.fillPath (p, AffineTransform::translation ((float) d, (float) d));
    }
    g.setColour (col::ink);
    g.strokePath (p, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (fill);
    g.fillPath (p);
}

// =====================================================================================
// Chunky buttons
// =====================================================================================
Chunky::Chunky (const String& label, Colour f) : Button (label), fill (f), onFill (f)
{
    seed = label.hashCode() & 0xff;
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void Chunky::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
    Button::mouseDown (e);
}

void Chunky::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (border * 0.5f);
    r.removeFromRight (shadow);
    r.removeFromBottom (shadow);
    const bool on = getToggleState();
    if (down) r.translate (shadow * 0.6f, shadow * 0.6f);
    drawBox (g, r, (on ? onFill : fill).brighter (over ? 0.08f : 0.0f), seed, down ? shadow * 0.4f : shadow, border,
             jmin (12.0f, r.getHeight() * 0.3f));
    auto textArea = r.reduced (6.0f, 2.0f);
    if (led)
    {
        auto l = textArea.removeFromLeft (30.0f).withSizeKeepingCentre (22.0f, 22.0f);
        if (on) { g.setColour (col::lime.withAlpha (0.45f)); g.fillEllipse (l.expanded (5.0f)); }
        g.setColour (on ? col::lime : col::ledOff);
        g.fillEllipse (l);
        g.setColour (col::ink);
        g.drawEllipse (l, 3.0f);
    }
    g.setColour (on ? onText : textColour);
    g.setFont (displayFont ? fonts().display (fontSize) : fonts().label (fontSize));
    g.drawFittedText (getButtonText(), textArea.toNearestInt().withTrimmedLeft (just == Justification::centredLeft ? 10 : 0), just, 2, 0.75f);
}

// =====================================================================================
// Knobs
// =====================================================================================
void KnobLook::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const float size = (float) jmin (w, h);
    const auto c = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    const float r = size * 0.36f;
    const auto ink = s.findColour (Slider::textBoxTextColourId);
    // tick marks
    g.setColour (ink);
    for (int i = 0; i < 7; ++i)
    {
        const float a = start + (end - start) * (float) i / 6.0f - MathConstants<float>::halfPi;
        g.drawLine (c.x + std::cos (a) * r * 1.15f, c.y + std::sin (a) * r * 1.15f, c.x + std::cos (a) * r * 1.32f, c.y + std::sin (a) * r * 1.32f, 3.0f);
    }
    auto body = wobblyCircle (c, r, 7);
    g.setColour (col::ink);
    g.fillPath (body, AffineTransform::translation (3.5f, 3.5f));
    g.setColour (s.findColour (Slider::rotarySliderFillColourId));
    g.fillPath (body);
    // a highlight and some hatching for shading
    g.setColour (Colours::white.withAlpha (0.75f));
    Path hl;
    hl.addCentredArc (c.x, c.y, r * 0.72f, r * 0.72f, 0.0f, -1.9f, -0.5f, true);
    g.strokePath (hl, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (col::ink.withAlpha (0.28f));
    for (int i = 0; i < 3; ++i)
        g.drawLine (c.x + r * (0.2f + 0.12f * (float) i), c.y + r * 0.75f, c.x + r * (0.62f + 0.1f * (float) i), c.y + r * (0.33f - 0.1f * (float) i), 2.0f);
    g.setColour (col::ink);
    g.strokePath (body, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.strokePath (wobblyCircle (c.translated (0.8f, -0.6f), r * 1.02f, 3, 1.6f), PathStrokeType (1.4f));
    // pointer
    const float a = start + pos * (end - start) - MathConstants<float>::halfPi;
    const Point<float> tip (c.x + std::cos (a) * r * 0.86f, c.y + std::sin (a) * r * 0.86f);
    g.drawLine ({ c, tip }, 7.0f);
    g.setColour (Colours::white);
    g.drawLine ({ c + (tip - c) * 0.1f, tip - (tip - c) * 0.08f }, 2.5f);
    g.setColour (col::ink);
    g.fillEllipse (Rectangle<float> (11.0f, 11.0f).withCentre (c));
}

Knob::Knob (KddProcessor& p, const String& paramId, const String& l, Colour f, Colour i)
    : proc (p), id (paramId), label (l), fill (f), ink (i)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setColour (Slider::rotarySliderFillColourId, fill);
    slider.setColour (Slider::textBoxTextColourId, ink);
    slider.setLookAndFeel (&look);
    slider.setPopupMenuEnabled (false);
    slider.setMouseCursor (MouseCursor::UpDownResizeCursor);
    slider.setDoubleClickReturnValue (true, (double) p.apvts.getParameter (id)->convertFrom0to1 (p.apvts.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (slider);
    attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id, slider);
    slider.addMouseListener (this, false);
    startTimerHz (12);
}

Knob::~Knob() { slider.setLookAndFeel (nullptr); }

void Knob::resized() { slider.setBounds (getLocalBounds().removeFromTop (getHeight() - 42).expanded (0, 2)); }

void Knob::timerCallback()
{
    auto* prm = proc.apvts.getParameter (id);
    String s = valueText ? valueText() : prm->getCurrentValueAsText();
    const bool l = proc.learning() == KddProcessor::LearnParam && proc.learningParam() == id;
    if (l) s = "MOVE IT!";
    if (s != shown || l != learning) { shown = s; learning = l; repaint(); }
}

void Knob::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto tag = r.removeFromBottom (20.0f);
    auto lab = r.removeFromBottom (20.0f);
    g.setColour (ink);
    g.setFont (fonts().label (12.5f));
    g.drawFittedText (label.toUpperCase(), lab.toNearestInt(), Justification::centred, 1, 0.7f);
    g.setFont (fonts().label (11.5f));
    const float tw = jmin (r.getWidth() - 4.0f, (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), shown) + 14.0f);
    auto box = tag.withSizeKeepingCentre (tw, 18.0f);
    g.setColour (learning ? col::yellow : col::paper);
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (col::ink);
    g.drawRoundedRectangle (box, 4.0f, 2.5f);
    g.drawFittedText (shown, box.toNearestInt(), Justification::centred, 1, 0.7f);
}

void Knob::showMenu()
{
    PopupMenu m;
    const auto cc = proc.midiLearnFor (id);
    m.addItem (1, "MIDI Learn");
    if (cc.isNotEmpty()) m.addItem (2, "Forget MIDI Learn (" + cc + ")");
    m.addItem (3, "Reset to default");
    if (extraMenu) { m.addSeparator(); extraMenu (m); }
    SafePointer<Knob> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&slider), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->proc.startLearn (KddProcessor::LearnParam, safe->id);
        else if (r == 2) safe->proc.clearMidiLearn (safe->id);
        else if (r == 3) if (auto* prm = safe->proc.apvts.getParameter (safe->id)) prm->setValueNotifyingHost (prm->getDefaultValue());
    });
}


// =====================================================================================
// Read-outs
// =====================================================================================
Readout::Readout (std::function<String()> g, std::function<void (Graphics&, const String&, Rectangle<float>)> d, int hz)
    : get (std::move (g)), draw (std::move (d))
{
    text = get();
    startTimerHz (hz);
}

void Readout::timerCallback()
{
    auto s = get();
    if (s != text) { text = s; repaint(); }
}

static std::unique_ptr<Readout> textReadout (std::function<String()> get, Font font, Colour colour, Justification j = Justification::centredLeft)
{
    return std::make_unique<Readout> (std::move (get), [font, colour, j] (Graphics& g, const String& s, Rectangle<float> r)
    {
        g.setColour (colour);
        g.setFont (font);
        g.drawFittedText (s, r.toNearestInt(), j, 3, 0.7f);
    });
}

// =====================================================================================
// The skull
// =====================================================================================
Skull::Skull (KddProcessor& p) : proc (p)
{
    cranium = ImageCache::getFromMemory (BinaryData::cranium_png, BinaryData::cranium_pngSize);
    jaw = ImageCache::getFromMemory (BinaryData::jaw_png, BinaryData::jaw_pngSize);
    ballL = ImageCache::getFromMemory (BinaryData::ballL_png, BinaryData::ballL_pngSize);
    ballR = ImageCache::getFromMemory (BinaryData::ballR_png, BinaryData::ballR_pngSize);
    irisL = ImageCache::getFromMemory (BinaryData::irisL_png, BinaryData::irisL_pngSize);
    irisR = ImageCache::getFromMemory (BinaryData::irisR_png, BinaryData::irisR_pngSize);
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void Skull::timerCallback()
{
    const float dt = 1.0f / 30.0f;
    t += dt;
    const float delayLevel = jmax (proc.wetLevel (0), proc.wetLevel (1));
    const float target = jlimit (0.0f, 13.0f, delayLevel * 70.0f);
    jawAngle += (target - jawAngle) * (target > jawAngle ? 0.7f : 0.35f);
    tailAmt += (jlimit (0.0f, 1.0f, proc.wetLevel (2) * 14.0f) - tailAmt) * 0.08f;
    eyePhase += dt * (0.35f + 1.6f * tailAmt);
    jump *= 0.82f;

    if (t >= nextBlink) { blinkStart = t; nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 3.5; }
    const double lf = proc.lastFootAt();
    if (lf > lastFoot + 0.01) { lastFoot = lf; blinkStart = t; jump = 1.0f; }

    int stamp = 0;
    const auto msg = proc.latestMessage (stamp);
    if (stamp != sayStamp)
    {
        if (sayStamp >= 0 && msg.isNotEmpty()) { bubble = msg; bubbleUntil = t + 1.8; }
        sayStamp = stamp;
    }
    repaint();
}

void Skull::paint (Graphics& g)
{
    if (! cranium.isValid()) return;
    const float sw = (float) cranium.getWidth(), sh = (float) cranium.getHeight();
    const float scale = jmin ((float) getWidth() / sw, (float) (getHeight() - 10) / sh);
    using namespace ParamIDs;
    const bool bypassed = proc.getParam (bypassAll) > 0.5f
                          || (proc.getParam (aOn) < 0.5f && proc.getParam (bOn) < 0.5f && proc.getParam (rOn) < 0.5f);
    const bool frozen = ! bypassed && (proc.isInfinite (0) || proc.isInfinite (1) || proc.isInfinite (2));
    const float tf = (float) t;

    float jawDeg = 1.0f + 1.2f * std::sin (tf * 1.3f) + jawAngle;
    float ix = 6.0f * std::sin (tf * 0.37f), iy = 4.0f * std::sin (tf * 0.53f);
    float rx = ix, ry = iy, tilt = 0.0f, shiver = 0.0f;
    if (tailAmt > 0.02f)
    {
        ix = rx = 6.0f + 18.0f * tailAmt * std::cos (eyePhase);
        iy = ry = -12.0f * tailAmt + 12.0f * tailAmt * std::sin (eyePhase);
        tilt = 3.5f * tailAmt * std::sin (tf * 0.7f);
    }
    float lid = 0.0f;
    const double since = t - blinkStart;
    if (since < 0.2) lid = (float) std::sin (since / 0.2 * MathConstants<double>::pi);
    if (frozen) { jawDeg = 13.0f; ix = rx = 0.0f; iy = ry = -16.0f; tilt = 0.0f; lid = 0.0f; shiver = 1.4f * std::sin (tf * 47.0f); }
    if (bypassed) { jawDeg = 4.0f; ix = 24.0f; iy = 8.0f; rx = -26.0f; ry = 6.0f; tilt = -9.0f; lid = 0.0f; }

    const float ox = ((float) getWidth() - sw * scale) * 0.5f;
    const float oy = 10.0f - jump * 8.0f;
    const auto base = AffineTransform::scale (scale).translated (ox + shiver, oy)
                          .rotated (degreesToRadians (tilt), ox + sw * scale * 0.5f, oy + sh * scale * 0.7f);

    if (frozen)
    {
        g.setColour (col::sky.withAlpha (0.35f));
        g.fillEllipse (Rectangle<float> (sw * scale * 1.05f, sh * scale * 1.0f).withCentre ({ ox + sw * scale * 0.5f, oy + sh * scale * 0.5f }));
    }

    // jaw, hinged at the back
    const auto jawT = AffineTransform::rotation (degreesToRadians (-jawDeg), 470.0f, 600.0f)
                          .translated (bypassed ? -18.0f : 0.0f, 0.0f).followedBy (base);
    // the inside of the mouth, so the jaw's hinge never shows daylight
    {
        Path mouth;
        mouth.addRoundedRectangle (118.0f, 600.0f, 330.0f, 110.0f, 30.0f);
        g.setColour (Colour (0xff2a1e1a));
        g.fillPath (mouth, base);
    }
    g.drawImageTransformed (jaw, jawT);

    if (bypassed)
    {
        // tongue out
        Path tongue;
        tongue.startNewSubPath (22, 8);
        tongue.quadraticTo (70, -6, 118, 10);
        tongue.quadraticTo (130, 70, 112, 130);
        tongue.quadraticTo (100, 182, 66, 184);
        tongue.quadraticTo (30, 186, 26, 136);
        tongue.quadraticTo (10, 80, 22, 8);
        tongue.closeSubPath();
        const auto tt = AffineTransform::rotation (degreesToRadians (8.0f + 6.0f * std::sin (tf * 2.2f)), 70.0f, 0.0f)
                            .scaled (0.94f).translated (187.0f, 602.0f).followedBy (base);
        g.setColour (col::pink);
        g.fillPath (tongue, tt);
        g.setColour (col::ink);
        g.strokePath (tongue, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded), tt);
        Path mid;
        mid.startNewSubPath (70, 20);
        mid.quadraticTo (66, 80, 72, 140);
        g.strokePath (mid, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded), tt);
    }

    g.drawImageTransformed (cranium, base);

    auto eye = [&] (const Image& ball, const Image& iris, Rectangle<float> box, Point<float> irisPos, float dx, float dy)
    {
        Graphics::ScopedSaveState s (g);
        Path clip;
        clip.addEllipse (box);
        g.reduceClipRegion (clip, base);
        g.drawImageTransformed (ball, AffineTransform::translation (box.getX(), box.getY()).followedBy (base));
        const float iw = (float) iris.getWidth();
        g.drawImageTransformed (iris, AffineTransform::translation (irisPos.x + dx * 0.01f * iw, irisPos.y + dy * 0.01f * iw).followedBy (base));
        if (lid > 0.01f)
        {
            auto lidRect = box.withHeight (box.getHeight() * lid);
            Path p;
            p.addRectangle (lidRect);
            g.setColour (Colour (0xffd9b98a));
            g.fillPath (p, base);
            Path line;
            line.startNewSubPath (lidRect.getBottomLeft());
            line.lineTo (lidRect.getBottomRight());
            g.setColour (col::ink);
            g.strokePath (line, PathStrokeType (5.0f), base);
        }
    };
    eye (ballL, irisL, { 175.0f, 273.0f, 118.0f, 125.0f }, { 195.0f, 312.0f }, ix, iy);
    eye (ballR, irisR, { 334.0f, 322.0f, 114.0f, 118.0f }, { 359.0f, 359.0f }, rx, ry);

    if (frozen)
    {
        // icicles on the teeth
        Path ice;
        const float pts[][2] = { { 6, 6 }, { 294, 10 }, { 282, 22 }, { 270, 58 }, { 258, 20 }, { 236, 24 }, { 226, 48 }, { 214, 22 }, { 190, 22 }, { 178, 66 },
                                 { 166, 20 }, { 140, 22 }, { 128, 44 }, { 118, 20 }, { 94, 18 }, { 84, 54 }, { 72, 18 }, { 50, 16 }, { 40, 40 }, { 30, 14 } };
        ice.startNewSubPath (pts[0][0], pts[0][1]);
        for (auto& p : pts) ice.lineTo (p[0], p[1]);
        ice.closeSubPath();
        const auto it = AffineTransform::scale (1.2f).translated (88.0f, 640.0f).followedBy (base);
        g.setColour (col::ice);
        g.fillPath (ice, it);
        g.setColour (col::ink);
        g.strokePath (ice, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded), it);
    }

    // speech bubble
    String say = bypassed ? "BLEH!" : (frozen ? "BRRRR!" : (t < bubbleUntil ? bubble : String()));
    if (say.isNotEmpty())
    {
        auto f = fonts().display (19.0f);
        const float w = jmin ((float) getWidth() - 10.0f, (float) GlyphArrangement::getStringWidthInt (f, say) + 30.0f);
        Rectangle<float> b (4.0f, 2.0f, w, 40.0f);
        Graphics::ScopedSaveState s (g);
        g.addTransform (AffineTransform::rotation (degreesToRadians (-5.0f), b.getCentreX(), b.getCentreY()));
        drawBox (g, b, col::paper, 9, 4.0f, 4.0f, 18.0f);
        g.setColour (col::ink);
        g.setFont (f);
        g.drawFittedText (say, b.toNearestInt().reduced (8, 2), Justification::centred, 1, 0.6f);
    }
}

// =====================================================================================
// Looper progress and expression bars
// =====================================================================================
void LoopBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    auto& lp = proc.looper();
    const int st = lp.uiState.load();
    const float len = lp.uiLength.load();
    g.setColour (col::paper);
    g.fillRoundedRectangle (r, 14.0f);
    float frac = st == kdd::Looper::Recording ? jmin (1.0f, len / kdd::Looper::kMaxSeconds) : lp.uiPos.load();
    if (st == kdd::Looper::Empty) frac = 0.0f;
    if (frac > 0.0f)
    {
        Graphics::ScopedSaveState s (g);
        Path clip;
        clip.addRoundedRectangle (r, 14.0f);
        g.reduceClipRegion (clip);
        g.setColour (st == kdd::Looper::Recording || st == kdd::Looper::Overdubbing ? col::red : col::cobalt);
        g.fillRect (r.withWidth (r.getWidth() * frac));
        g.setColour (col::ink);
        g.fillRect (Rectangle<float> (r.getX() + r.getWidth() * frac - 1.5f, r.getY(), 3.0f, r.getHeight()));
    }
    g.setColour (col::ink);
    g.drawRoundedRectangle (r, 14.0f, 3.0f);
}

void ExpressionBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (col::paper);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    {
        Graphics::ScopedSaveState s (g);
        auto fillR = r.withWidth (r.getWidth() * proc.expressionPosition());
        g.reduceClipRegion (fillR.toNearestInt());
        g.setColour (col::pink);
        g.fillRect (fillR);
        drawHatch (g, fillR, col::ink, 9.0f);
    }
    g.setColour (col::ink);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 3.0f);
}

// =====================================================================================
// Shared page pieces
// =====================================================================================
static void sunburst (Graphics& g, Rectangle<float> area, Point<float> centre, Colour a, Colour b)
{
    g.setColour (a);
    g.fillRect (area);
    g.setColour (b);
    const float R = area.getWidth() + area.getHeight();
    for (int i = 0; i < 26; ++i)
    {
        const float a0 = MathConstants<float>::twoPi * (float) i / 26.0f, a1 = a0 + MathConstants<float>::twoPi / 52.0f;
        Path w;
        w.startNewSubPath (centre);
        w.lineTo (centre.x + std::cos (a0) * R, centre.y + std::sin (a0) * R);
        w.lineTo (centre.x + std::cos (a1) * R, centre.y + std::sin (a1) * R);
        w.closeSubPath();
        g.fillPath (w);
    }
}

static void grime (Graphics& g, Rectangle<float> area, int seed)
{
    g.setColour (col::ink.withAlpha (0.2f));
    for (int i = 0; i < 40; ++i)
    {
        const float x = area.getX() + (jitter (seed, i, 1.0f) * 0.5f + 0.5f) * area.getWidth();
        const float y = area.getY() + (jitter (seed + 9, i, 1.0f) * 0.5f + 0.5f) * area.getHeight();
        const float s = 2.5f + 2.5f * (jitter (seed + 3, i, 1.0f) * 0.5f + 0.5f);
        g.fillEllipse (x, y, s, s);
    }
}

static void styleTab (Chunky& b)
{
    b.fill = col::paper; b.onFill = col::ink; b.textColour = col::ink; b.onText = col::yellow;
    b.shadow = 0.0f; b.border = 3.0f; b.fontSize = 15.0f;
    b.setClickingTogglesState (false);
}

// =====================================================================================
// Main page
// =====================================================================================
class MainPage : public Component, private Timer
{
public:
    MainPage (KddProcessor& p, std::function<void()> openPedals) : proc (p), skull (p), loopBar (p), expBar (p)
    {
        using namespace ParamIDs;
        // ---- header: preset box
        presetName = textReadout ([this] { return proc.slotName (proc.getSlot()); }, fonts().display (27.0f), col::ink);
        presetSlot = textReadout ([this] { const int s = proc.getSlot(); return "PRESET  BANK " + String (s / 3 + 1) + "  " + String::charToString ((juce_wchar) ('A' + s % 3)); },
                                  fonts().label (12.0f), col::ink);
        presetNote = textReadout ([this] { return proc.slotNote (proc.getSlot()); }, fonts().hand (15.0f), col::grey);
        presetName->onClick = [this] { presetMenu (false); };
        for (auto* r : { presetName.get(), presetSlot.get(), presetNote.get() }) addAndMakeVisible (r);
        presetName->setMouseCursor (MouseCursor::PointingHandCursor);
        for (auto* b : { &presetUp, &presetDown, &presetSave }) { b->fill = col::lime; addAndMakeVisible (b); }
        presetUp.onClick = [this] { proc.stepPreset (1); };
        presetDown.onClick = [this] { proc.stepPreset (-1); };
        presetSave.fontSize = 12.0f;
        presetSave.onClick = [this] { presetMenu (true); };

        // ---- mode tabs, tap, routing, bypass all
        for (int m = 0; m < 4; ++m)
        {
            auto* b = modeTabs.add (new Chunky (KddProcessor::modeName (m)));
            styleTab (*b);
            b->onClick = [this, m] { proc.setMode (m); };
            addAndMakeVisible (b);
        }
        tap.fill = col::sky;
        tap.just = Justification::centredLeft;
        tap.onClick = [this] { proc.tapMaster(); };
        addAndMakeVisible (tap);
        bpmTag = textReadout ([this] {
            const bool sync = proc.getParam (ParamIDs::syncHost) > 0.5f;
            return (sync ? "HOST " : "") + String (roundToInt (proc.getParam (ParamIDs::bpm))) + " BPM"; }, fonts().label (14.0f), col::ink, Justification::centred);
        bpmTag->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*bpmTag);
        routeBtn.onClick = [this] { proc.setParamPlain (ParamIDs::route, (float) (((int) proc.getParam (ParamIDs::route) + 1) % 4)); };
        addAndMakeVisible (routeBtn);
        bypassBtn.fill = col::paper; bypassBtn.onFill = col::red; bypassBtn.onText = col::paper;
        bypassBtn.setClickingTogglesState (true);
        bypassAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, bypassAll, bypassBtn);
        addAndMakeVisible (bypassBtn);

        // ---- the three modules
        buildModule (0, col::sky, col::bone, col::ink, names::typesA());
        buildModule (1, col::cobalt, col::yellow, col::paper, names::typesB());
        buildModule (2, col::pink, col::bone, col::ink, names::typesR());

        // ---- looper
        loopMain.fill = col::red; loopMain.textColour = col::paper; loopMain.fontSize = 18.0f; loopMain.displayFont = true;
        loopMain.onClick = [this] { proc.looperCommand (kdd::Looper::CmdMain); };
        addAndMakeVisible (loopMain);
        const kdd::Looper::Command cmds[5] = { kdd::Looper::CmdStop, kdd::Looper::CmdUndo, kdd::Looper::CmdHalf, kdd::Looper::CmdReverse, kdd::Looper::CmdClear };
        const char* labels[5] = { "STOP", "UNDO", "HALF", "REV", "CLEAR" };
        for (int i = 0; i < 5; ++i)
        {
            auto* b = loopBtns.add (new Chunky (labels[i], i == 2 || i == 3 ? col::yellow : col::paper));
            b->fontSize = 13.0f;
            b->onFill = col::pink;
            const auto c = cmds[i];
            b->onClick = [this, c] { proc.looperCommand (c); };
            addAndMakeVisible (b);
        }
        for (int i = 0; i < 3; ++i)
        {
            auto* b = loopPosBtns.add (new Chunky (names::loopPositions()[i]));
            styleTab (*b);
            b->fontSize = 12.0f;
            b->onClick = [this, i] { proc.setParamPlain (ParamIDs::loopPos, (float) i); };
            addAndMakeVisible (b);
        }
        loopState = textReadout ([this] {
            auto& lp = proc.looper();
            static const char* st[] = { "EMPTY: HIT REC", "RECORDING", "PLAYING", "OVERDUBBING", "STOPPED" };
            const int s = jlimit (0, 4, lp.uiState.load());
            String extra;
            if (lp.isHalf()) extra << "  HALF";
            if (lp.isReverse()) extra << "  REV";
            if (lp.uiCanUndo.load()) extra << (lp.uiLayerOn.load() ? "  UNDO READY" : "  REDO READY");
            const float len = lp.uiLength.load();
            return String (st[s]) + extra + "   " + String ((int) len / 60) + ":" + String ((int) len % 60).paddedLeft ('0', 2) + " / 2:00"; },
            fonts().label (13.0f), col::ink);
        addAndMakeVisible (*loopState);
        addAndMakeVisible (loopBar);
        loopLevel = std::make_unique<Knob> (proc, loopLevel_id(), "Level", col::bone, col::ink);
        addAndMakeVisible (*loopLevel);

        // ---- right column
        addAndMakeVisible (skull);
        modeChip = textReadout ([this] { return KddProcessor::modeName (proc.getMode()) + " MODE"; }, fonts().label (12.0f), col::yellow, Justification::centred);
        addAndMakeVisible (*modeChip);
        for (int k = 0; k < 3; ++k)
        {
            auto row = std::make_unique<Readout> ([this, k] { auto j = KddProcessor::jobFor (proc.getMode(), k); return j.first + "\n" + j.second
                                                              + ((proc.footDownMask() >> k) & 1 ? "\x01" : ""); },
                [k] (Graphics& g, const String& s, Rectangle<float> r)
                {
                    const bool down = s.endsWithChar ((juce_wchar) 1);
                    auto lines = StringArray::fromLines (s.trimCharactersAtEnd (String::charToString ((juce_wchar) 1)));
                    auto badge = r.removeFromLeft (40.0f).withSizeKeepingCentre (38.0f, 38.0f);
                    g.setColour (down ? col::lime : col::pink);
                    g.fillEllipse (badge);
                    g.setColour (col::ink);
                    g.drawEllipse (badge, 3.0f);
                    chunkyText (g, String::charToString ((juce_wchar) ('A' + k)), fonts().display (21.0f), badge.translated (0.0f, -1.0f), col::paper, col::ink, 0, 3.0f, Justification::centred);
                    r.removeFromLeft (8.0f);
                    g.setColour (col::ink);
                    g.setFont (fonts().label (14.0f));
                    g.drawFittedText (lines[0], r.removeFromTop (r.getHeight() * 0.52f).toNearestInt(), Justification::bottomLeft, 1, 0.7f);
                    g.setColour (col::grey);
                    g.setFont (fonts().label (11.0f));
                    g.drawFittedText (lines[1], r.toNearestInt(), Justification::topLeft, 1, 0.7f);
                }, 20);
            addAndMakeVisible (*row);
            jobs.push_back (std::move (row));
        }
        teachBtn.fill = col::yellow;
        teachBtn.onClick = [this] { proc.startTeach(); };
        addAndMakeVisible (teachBtn);
        setupBtn.fill = col::sky;
        setupBtn.onClick = std::move (openPedals);
        addAndMakeVisible (setupBtn);
        expTargetTag = textReadout ([this] { return "> " + names::expTargetNames()[(int) proc.getParam (ParamIDs::expTarget)]; },
                                    fonts().label (12.0f), col::ink, Justification::centredRight);
        expTargetTag->onClick = [this] { expressionMenu (proc, expTargetTag.get()); };
        expTargetTag->setMouseCursor (MouseCursor::PointingHandCursor);
        addAndMakeVisible (*expTargetTag);
        addAndMakeVisible (expBar);

        startTimerHz (15);
        timerCallback();
    }

    static String loopLevel_id() { return ParamIDs::loopLevel; }

    static void expressionMenu (KddProcessor& proc, Component* target)
    {
        PopupMenu m;
        const auto names = names::expTargetNames();
        const int cur = (int) proc.getParam (ParamIDs::expTarget);
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i], true, i == cur);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (target), [&proc] (int r) { if (r > 0) proc.setParamPlain (ParamIDs::expTarget, (float) (r - 1)); });
    }

    void presetMenu (bool save)
    {
        PopupMenu m;
        for (int b = 0; b < 8; ++b)
        {
            PopupMenu bank;
            for (int s = 0; s < 3; ++s)
            {
                const int slot = b * 3 + s;
                bank.addItem (slot + 1, String (b + 1) + String::charToString ((juce_wchar) ('A' + s)) + "  " + proc.slotName (slot), true, slot == proc.getSlot());
            }
            m.addSubMenu ("BANK " + String (b + 1), bank);
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (save ? (Component*) &presetSave : presetName.get()), [this, save] (int r)
        {
            if (r <= 0) return;
            if (save) proc.saveSlot (r - 1); else proc.loadSlot (r - 1);
        });
    }

    void buildModule (int m, Colour panel, Colour knobFill, Colour ink, const StringArray& types)
    {
        using namespace ParamIDs;
        auto& mod = modules[(size_t) m];
        mod.panel = panel;
        mod.ink = ink;
        const String pre = m == 0 ? "a" : (m == 1 ? "b" : "r");
        mod.typeId = pre + "Type";
        mod.voiceId = pre + "Voice";
        mod.typeName = textReadout ([this, types, id = mod.typeId] { return types[(int) proc.getParam (id.toRawUTF8())]; }, fonts().display (24.0f), col::yellow);
        mod.typeNum = textReadout ([this, id = mod.typeId] { return String ((int) proc.getParam (id.toRawUTF8()) + 1) + "/9"; }, fonts().label (11.0f), col::paper, Justification::centredRight);
        mod.typeName->setInterceptsMouseClicks (false, false);
        mod.typeNum->setInterceptsMouseClicks (false, false);
        mod.prev.fill = mod.next.fill = col::yellow;
        mod.prev.shadow = mod.next.shadow = 0.0f;
        mod.prev.onClick = [this, id = mod.typeId] { proc.setParamPlain (id, (float) (((int) proc.getParam (id.toRawUTF8()) + 8) % 9)); };
        mod.next.onClick = [this, id = mod.typeId] { proc.setParamPlain (id, (float) (((int) proc.getParam (id.toRawUTF8()) + 1) % 9)); };
        mod.voice.fontSize = 13.0f;
        mod.voice.shadow = 0.0f;
        mod.voice.onClick = [this, id = mod.voiceId] { proc.setParamPlain (id, proc.getParam (id.toRawUTF8()) > 0.5f ? 0.0f : 1.0f); };
        for (auto* c : { (Component*) &mod.prev, (Component*) &mod.next, (Component*) &mod.voice }) addAndMakeVisible (c);
        addAndMakeVisible (*mod.typeName);
        addAndMakeVisible (*mod.typeNum);

        if (m < 2)
        {
            mod.subdiv.fontSize = 12.0f;
            mod.subdiv.shadow = 0.0f;
            mod.subdiv.onClick = [this, sid = pre + "Subdiv", m]
            {
                PopupMenu menu;
                const int cur = (int) proc.getParam (sid.toRawUTF8());
                for (int i = 0; i < names::subdivs().size(); ++i) menu.addItem (i + 1, i == 0 ? "FREE (TIME KNOB)" : names::subdivs()[i] + " OF THE TEMPO", true, i == cur);
                menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&modules[(size_t) m].subdiv),
                                    [this, sid] (int r) { if (r > 0) proc.setParamPlain (sid, (float) (r - 1)); });
            };
            addAndMakeVisible (mod.subdiv);
        }

        struct K { const char* suffix; const char* label; };
        const std::vector<K> ks = m < 2 ? std::vector<K> { { "Time", "Time" }, { "Feedback", "Feedback" }, { "Mix", "Mix" }, { "Tone", "Tone" }, { "LowCut", "Low Cut" }, { "Duck", "Duck" } }
                                        : std::vector<K> { { "Decay", "Decay" }, { "PreDelay", "Pre-Delay" }, { "Mix", "Mix" }, { "Shimmer", "Shimmer" }, { "Tone", "Tone" }, { "LowCut", "Low Cut" } };
        for (auto& k : ks)
        {
            const String id = pre + k.suffix;
            auto kn = std::make_unique<Knob> (proc, id, k.label, knobFill, ink);
            if (String (k.suffix) == "Time")
                kn->valueText = [this, pre]
                {
                    const int s = (int) proc.getParam ((pre + "Subdiv").toRawUTF8());
                    return s == 0 ? String (roundToInt (proc.getParam ((pre + "Time").toRawUTF8()))) + " ms" : names::subdivs()[s];
                };
            if (String (k.suffix) == "Tone")
            {
                const String tm = pre + "ToneMode";
                kn->valueText = [this, tm, id] { return (proc.getParam (tm.toRawUTF8()) > 0.5f ? "HPF " : "LPF ") + String (roundToInt (proc.getParam (id.toRawUTF8()) * 100.0f)); };
                kn->extraMenu = [this, tm] (PopupMenu& menu)
                {
                    const bool hp = proc.getParam (tm.toRawUTF8()) > 0.5f;
                    menu.addItem ("Tone is a low-pass", true, ! hp, [this, tm] { proc.setParamPlain (tm, 0.0f); });
                    menu.addItem ("Tone is a high-pass", true, hp, [this, tm] { proc.setParamPlain (tm, 1.0f); });
                };
            }
            addAndMakeVisible (*kn);
            mod.knobs.push_back (std::move (kn));
        }

        mod.stomp.led = true;
        mod.stomp.fontSize = 17.0f;
        mod.stomp.setClickingTogglesState (true);
        mod.stompAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, pre + "On", mod.stomp);
        addAndMakeVisible (mod.stomp);
        mod.hold.fill = col::ice;
        mod.hold.onFill = col::sky;
        mod.hold.fontSize = 12.0f;
        mod.hold.setClickingTogglesState (true);
        mod.holdAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, m == 2 ? String (rFreeze) : pre + "Infinite", mod.hold);
        addAndMakeVisible (mod.hold);
    }

    void timerCallback() override
    {
        for (int m = 0; m < 4; ++m) modeTabs[m]->setToggleState (proc.getMode() == m, dontSendNotification);
        routeBtn.setButtonText ("ROUTE  " + names::routes()[(int) proc.getParam (ParamIDs::route)]);
        for (int m = 0; m < 3; ++m)
        {
            auto& mod = modules[(size_t) m];
            mod.voice.setButtonText (proc.getParam (mod.voiceId.toRawUTF8()) > 0.5f ? "MK I" : "MK II");
            mod.stomp.setButtonText (mod.stomp.getToggleState() ? "ON" : "OFF");
            if (m < 2) mod.subdiv.setButtonText (names::subdivs()[(int) proc.getParam (m == 0 ? ParamIDs::aSubdiv : ParamIDs::bSubdiv)]);
        }
        const int lp = (int) proc.getParam (ParamIDs::loopPos);
        for (int i = 0; i < 3; ++i) loopPosBtns[i]->setToggleState (lp == i, dontSendNotification);
        auto& looper = proc.looper();
        const int st = looper.uiState.load();
        loopMain.setButtonText (st == kdd::Looper::Empty ? "REC" : (st == kdd::Looper::Recording ? "PLAY" : (st == kdd::Looper::Playing ? "DUB" : (st == kdd::Looper::Overdubbing ? "PLAY" : "PLAY"))));
        loopBtns[2]->setToggleState (looper.isHalf(), dontSendNotification);
        loopBtns[3]->setToggleState (looper.isReverse(), dontSendNotification);
        teachBtn.setButtonText (proc.teachStep() > 0 ? "STOMP " + String::charToString ((juce_wchar) ('A' + proc.teachStep() - 1)) + " NOW" : "TEACH MY PEDAL");
    }

    void paint (Graphics& g) override
    {
        auto all = getLocalBounds().toFloat();
        sunburst (g, all, { 1180.0f, 200.0f }, col::mustard, col::ochre);
        grime (g, all, 5);

        // title
        {
            Graphics::ScopedSaveState s (g);
            g.addTransform (AffineTransform::rotation (degreesToRadians (-1.5f), 24.0f, 70.0f));
            chunkyText (g, "KEVY DOES DEVY", fonts().display (74.0f), { 26.0f, 18.0f, 650.0f, 92.0f }, col::pink, col::pinkDark, 8, 6.0f);
            // cracks
            g.setColour (col::ink);
            const float cracks[][8] = { { 70, 30, 76, 46, 70, 54, 78, 72 }, { 228, 82, 236, 70, 232, 62, 242, 52 }, { 392, 34, 386, 50, 396, 58, 390, 76 },
                                        { 540, 84, 550, 72, 546, 64, 556, 58 } };
            for (auto& c : cracks)
            {
                Path p;
                p.startNewSubPath (c[0], c[1]);
                p.lineTo (c[2], c[3]); p.lineTo (c[4], c[5]); p.lineTo (c[6], c[7]);
                g.strokePath (p, PathStrokeType (3.0f, PathStrokeType::mitered, PathStrokeType::rounded));
            }
        }
        {
            Graphics::ScopedSaveState s (g);
            Rectangle<float> tag (34.0f, 118.0f, 430.0f, 34.0f);
            g.addTransform (AffineTransform::rotation (degreesToRadians (1.2f), tag.getCentreX(), tag.getCentreY()));
            drawBox (g, tag, col::paper, 2, 4.0f, 3.0f, 2.0f);
            g.setColour (col::ink);
            g.setFont (fonts().hand (19.0f));
            g.drawText ("dual delay, reverb, looper. tuned for bass", tag.reduced (10.0f, 0.0f), Justification::centred);
        }

        drawBox (g, { 702.0f, 22.0f, 330.0f, 112.0f }, col::paper, 3, 6.0f);

        // mode group
        drawBox (g, { 24.0f, 160.0f, 378.0f, 52.0f }, col::paper, 4, 5.0f);

        // modules
        for (int m = 0; m < 3; ++m)
        {
            auto& mod = modules[(size_t) m];
            const Rectangle<float> r (moduleX (m), 232.0f, 325.0f, 452.0f);
            drawBox (g, r, mod.panel, 10 + m, 7.0f);
            drawHatch (g, Rectangle<float> (r.getRight() - 150.0f, r.getBottom() - 150.0f, 146.0f, 146.0f).reduced (2.0f),
                       (m == 1 ? col::paper : col::ink).withAlpha (0.16f));
            static const char* names[3] = { "DELAY A", "DELAY B", "REVERB" };
            chunkyText (g, names[m], fonts().display (30.0f), { r.getX() + 12.0f, r.getY() + 10.0f, 165.0f, 44.0f },
                        m == 1 ? col::yellow : col::paper, col::ink, 3, 5.0f);
            // type window
            Rectangle<float> win (r.getX() + 60.0f, r.getY() + 62.0f, 205.0f, 46.0f);
            g.setColour (col::ink);
            g.fillPath (wobblyRect (win, 20 + m, 7.0f));
            if (m == 2)
            {
                // green slime over the top edge
                Path slime;
                slime.startNewSubPath (r.getX() + 150.0f, r.getY() - 4.0f);
                const float xs[] = { 0, 24, 30, 38, 60, 100, 108, 118, 150, 198, 204, 212, 236, 264, 272, 282, 300 };
                const float ys[] = { 10, 10, 24, 16, 12, 12, 38, 26, 12, 12, 28, 20, 12, 20, 34, 18, 10 };
                for (int i = 0; i < 17; ++i) slime.lineTo (r.getX() + 150.0f + xs[i] * 0.55f, r.getY() - 4.0f + ys[i] * 0.8f);
                slime.lineTo (r.getX() + 315.0f, r.getY() - 4.0f);
                slime.closeSubPath();
                g.setColour (col::lime);
                g.fillPath (slime);
                g.setColour (col::ink);
                g.strokePath (slime, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }

        // looper strip
        Rectangle<float> lp (24.0f, 706.0f, 1008.0f, 170.0f);
        drawBox (g, lp, col::lime, 30, 7.0f);
        drawHatch (g, Rectangle<float> (lp.getX() + 4.0f, lp.getBottom() - 80.0f, 170.0f, 76.0f), col::ink.withAlpha (0.14f));
        {
            Graphics::ScopedSaveState s (g);
            g.addTransform (AffineTransform::rotation (degreesToRadians (-4.0f), 100.0f, 760.0f));
            chunkyText (g, "LOOPER", fonts().display (36.0f), { 38.0f, 730.0f, 160.0f, 52.0f }, col::paper, col::ink, 3, 5.0f);
        }
        drawBox (g, { 346.0f, 802.0f, 186.0f, 50.0f }, col::paper, 31, 0.0f, 3.0f, 8.0f);

        // right column cards
        drawBox (g, { 1056.0f, 392.0f, 280.0f, 318.0f }, col::paper, 40, 6.0f);
        chunkyText (g, "FOOTSWITCHES", fonts().display (21.0f), { 1070.0f, 402.0f, 160.0f, 30.0f }, col::ink, col::ink, 0, 0.5f);
        g.setColour (col::ink);
        g.fillRoundedRectangle (1232.0f, 405.0f, 92.0f, 24.0f, 4.0f);
        drawBox (g, { 1056.0f, 730.0f, 280.0f, 100.0f }, col::paper, 41, 6.0f);
        chunkyText (g, "EXPRESSION", fonts().display (19.0f), { 1070.0f, 740.0f, 140.0f, 28.0f }, col::ink, col::ink, 0, 0.5f);
        g.setColour (col::grey);
        g.setFont (fonts().label (11.0f));
        g.drawFittedText ("Right-click any knob for MIDI Learn", Rectangle<int> (1056, 842, 280, 20), Justification::centred, 1);
    }

    static float moduleX (int m) { return 24.0f + (float) m * 341.5f; }

    void resized() override
    {
        presetSlot->setBounds (716, 30, 240, 18);
        presetName->setBounds (716, 48, 240, 38);
        presetNote->setBounds (716, 88, 240, 22);
        presetUp.setBounds (970, 30, 52, 46);
        presetDown.setBounds (970, 80, 52, 46);
        presetSave.setBounds (900, 94, 66, 34);

        int x = 28;
        const int tabW[4] = { 82, 70, 116, 104 };
        for (int m = 0; m < 4; ++m) { modeTabs[m]->setBounds (x, 164, tabW[m], 44); x += tabW[m]; }
        tap.setBounds (420, 158, 190, 58);
        bpmTag->setBounds (488, 170, 112, 30);
        tap.setButtonText ("TAP");
        routeBtn.setBounds (624, 158, 236, 58);
        bypassBtn.setBounds (872, 158, 160, 58);

        for (int m = 0; m < 3; ++m)
        {
            auto& mod = modules[(size_t) m];
            const int px = (int) moduleX (m), py = 232;
            if (m < 2) mod.subdiv.setBounds (px + 178, py + 12, 66, 42);
            mod.voice.setBounds (px + 250, py + 12, 66, 42);
            mod.prev.setBounds (px + 12, py + 62, 44, 46);
            mod.next.setBounds (px + 269, py + 62, 44, 46);
            mod.typeName->setBounds (px + 72, py + 62, 150, 46);
            mod.typeNum->setBounds (px + 220, py + 62, 38, 46);
            for (int k = 0; k < 6; ++k)
                mod.knobs[(size_t) k]->setBounds (px + 18 + (k % 3) * 96, py + 114 + (k / 3) * 130, 96, 128);
            mod.stomp.setBounds (px + 12, py + 378, 196, 64);
            mod.hold.setBounds (px + 214, py + 378, 102, 64);
            mod.hold.setButtonText (m == 2 ? "HOLD\nFREEZE" : "HOLD\nINFINITE");
        }

        loopMain.setBounds (200, 724, 134, 88);
        for (int i = 0; i < 5; ++i) loopBtns[i]->setBounds (346 + i * 74, 724, 70, 60);
        for (int i = 0; i < 3; ++i) loopPosBtns[i]->setBounds (350 + i * 60, 806, 58, 42);
        loopBar.setBounds (546, 806, 340, 36);
        loopState->setBounds (546, 846, 340, 22);
        loopLevel->setBounds (912, 724, 96, 128);

        skull.setBounds (1061, 14, 270, 372);
        modeChip->setBounds (1232, 405, 92, 24);
        for (int k = 0; k < 3; ++k) jobs[(size_t) k]->setBounds (1068, 440 + k * 56, 260, 52);
        teachBtn.setBounds (1066, 610, 262, 46);
        setupBtn.setBounds (1066, 658, 262, 44);
        expTargetTag->setBounds (1180, 742, 144, 24);
        expBar.setBounds (1068, 778, 256, 30);
    }

private:
    struct Module
    {
        Colour panel, ink;
        String typeId, voiceId;
        std::unique_ptr<Readout> typeName, typeNum;
        Chunky prev { "<" }, next { ">" }, voice { "MK II" }, subdiv { "FREE" };
        std::vector<std::unique_ptr<Knob>> knobs;
        Chunky stomp { "ON" }, hold { "HOLD" };
        std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> stompAttach, holdAttach;
    };

    KddProcessor& proc;
    std::unique_ptr<Readout> presetName, presetSlot, presetNote, bpmTag, loopState, modeChip, expTargetTag;
    Chunky presetUp { "+" }, presetDown { "-" }, presetSave { "SAVE" };
    OwnedArray<Chunky> modeTabs, loopBtns, loopPosBtns;
    Chunky tap { "TAP" }, routeBtn { "ROUTE" }, bypassBtn { "BYPASS ALL" };
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;
    std::array<Module, 3> modules;
    Chunky loopMain { "REC" };
    std::unique_ptr<Knob> loopLevel;
    Skull skull;
    LoopBar loopBar;
    ExpressionBar expBar;
    std::vector<std::unique_ptr<Readout>> jobs;
    Chunky teachBtn { "TEACH MY PEDAL" }, setupBtn { "PEDAL SETUP + EXTRAS" };
};

// =====================================================================================
// Pedal setup and extras page
// =====================================================================================
class PedalPage : public Component, private Timer
{
public:
    PedalPage (KddProcessor& p, std::function<void()> back) : proc (p)
    {
        using namespace ParamIDs;
        backBtn.fill = col::yellow; backBtn.fontSize = 18.0f; backBtn.displayFont = true;
        backBtn.onClick = std::move (back);
        addAndMakeVisible (backBtn);
        for (int m = 0; m < 4; ++m)
        {
            auto* b = viewTabs.add (new Chunky (KddProcessor::modeName (m)));
            styleTab (*b);
            b->onClick = [this, m] { viewMode = m; };
            addAndMakeVisible (b);
        }
        for (int k = 0; k < 4; ++k)
        {
            auto job = std::make_unique<Readout> ([this, k] { auto j = KddProcessor::jobFor (viewMode, k); return j.first + "\n" + j.second; },
                [] (Graphics& g, const String& s, Rectangle<float> r)
                {
                    auto lines = StringArray::fromLines (s);
                    g.setColour (col::ink);
                    g.setFont (fonts().display (21.0f));
                    g.drawFittedText (lines[0], r.removeFromTop (34.0f).toNearestInt(), Justification::centred, 1, 0.6f);
                    auto chip = r.removeFromTop (34.0f).reduced (4.0f, 2.0f);
                    if (lines[1].isNotEmpty())
                    {
                        drawBox (g, chip, col::yellow, 50, 0.0f, 3.0f, 6.0f);
                        g.setColour (col::ink);
                        g.setFont (fonts().label (12.0f));
                        g.drawFittedText (lines[1], chip.toNearestInt().reduced (4, 0), Justification::centred, 1, 0.6f);
                    }
                }, 10);
            addAndMakeVisible (*job);
            jobReadouts.push_back (std::move (job));
            auto listens = textReadout ([this, k]
            {
                StringArray b;
                for (auto& bnd : proc.pedals().fs[(size_t) k]) b.add (bnd.describe());
                return "listens for\n" + (b.isEmpty() ? String ("nothing") : b.joinIntoString (" or "));
            }, fonts().label (11.5f), col::ink);
            addAndMakeVisible (*listens);
            listenReadouts.push_back (std::move (listens));
            auto* learn = learnBtns.add (new Chunky ("LEARN", col::lime));
            learn->fontSize = 13.0f;
            learn->onClick = [this, k] { proc.startLearn (k); };
            addAndMakeVisible (learn);
        }

        status = textReadout ([this] { return proc.footInputStatus(); }, fonts().label (13.0f), col::ink);
        addAndMakeVisible (*status);
        deviceBtn.fontSize = 13.0f;
        deviceBtn.onClick = [this] { deviceMenu(); };
        addAndMakeVisible (deviceBtn);
        teachBtn.fill = col::pink; teachBtn.textColour = col::paper; teachBtn.fontSize = 18.0f; teachBtn.displayFont = true;
        teachBtn.onClick = [this] { proc.startTeach(); };
        addAndMakeVisible (teachBtn);
        teachText = textReadout ([this]
        {
            const int s = proc.teachStep();
            if (s == 0) return String ("Press it, then step on A, B and C in turn. Whatever your pedal sends is learnt.");
            return "Step on " + String::charToString ((juce_wchar) ('A' + s - 1)) + " now...";
        }, fonts().label (13.0f), col::ink);
        addAndMakeVisible (*teachText);
        resetBtn.fontSize = 12.0f;
        resetBtn.onClick = [this] { proc.pedals().resetFootswitches(); proc.savePedals(); proc.say ("FOOTSWITCHES RESET"); };
        addAndMakeVisible (resetBtn);

        expTarget.fontSize = 13.0f;
        expTarget.onClick = [this] { MainPage::expressionMenu (proc, &expTarget); };
        addAndMakeVisible (expTarget);
        addAndMakeVisible (expBar);
        for (auto* b : { &expLearn, &expCal, &expCurve }) { b->fill = col::sky; b->fontSize = 12.0f; addAndMakeVisible (b); }
        expLearn.onClick = [this] { proc.startLearn (KddProcessor::LearnExpression); };
        expCal.onClick = [this] { proc.startCalibration(); };
        expCurve.onClick = [this] { auto& s = proc.pedals(); s.curve = (s.curve + 1) % 3; proc.savePedals(); };
        expListen = textReadout ([this] { auto& s = proc.pedals(); return "listens for CC " + String (s.expCC) + (s.expChannel == 0 ? " on any channel" : " ch " + String (s.expChannel)); },
                                 fonts().label (12.0f), col::ink);
        addAndMakeVisible (*expListen);

        // extras
        struct T { const char* id; const char* label; };
        const T toggles[] = { { trails, "TRAILS" }, { merge, "MERGE" }, { killDry, "KILL DRY" }, { syncHost, "SYNC TO DAW TEMPO" },
                              { aPing, "PING-PONG A" }, { bPing, "PING-PONG B" }, { eqOn, "GLOBAL EQ" }, { eqDry, "EQ ON DRY TOO" } };
        for (auto& tg : toggles)
        {
            auto* b = extraToggles.add (new Chunky (tg.label, col::paper));
            b->onFill = col::lime; b->fontSize = 12.5f; b->led = true;
            b->setClickingTogglesState (true);
            toggleAttach.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, tg.id, *b));
            addAndMakeVisible (b);
        }
        for (auto [id, label] : { std::pair<const char*, const char*> { eqLow, "EQ Low" }, { eqMid, "EQ Mid" }, { eqHigh, "EQ High" } })
        {
            auto k = std::make_unique<Knob> (proc, id, label, col::bone, col::ink);
            addAndMakeVisible (*k);
            eqKnobs.push_back (std::move (k));
        }
        startTimerHz (10);
        timerCallback();
    }

    void deviceMenu()
    {
        PopupMenu m;
        const auto& cur = proc.pedals().footDevice;
        m.addItem (1, "Find my footswitch automatically", true, cur.isEmpty());
        m.addItem (2, "Off: footswitch MIDI comes through my DAW", true, cur == "none");
        m.addSeparator();
        const auto devs = DirectMidiInput::deviceNames();
        for (int i = 0; i < devs.size(); ++i) m.addItem (10 + i, devs[i], true, cur == devs[i]);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&deviceBtn), [this, devs] (int r)
        {
            if (r == 1) proc.setFootDevice ({});
            else if (r == 2) proc.setFootDevice ("none");
            else if (r >= 10 && r - 10 < devs.size()) proc.setFootDevice (devs[r - 10]);
        });
    }

    void timerCallback() override
    {
        for (int m = 0; m < 4; ++m) viewTabs[m]->setToggleState (viewMode == m, dontSendNotification);
        for (int k = 0; k < 4; ++k) learnBtns[k]->setButtonText (proc.learning() == k ? "PRESS IT!" : "LEARN");
        expLearn.setButtonText (proc.learning() == KddProcessor::LearnExpression ? "MOVE IT!" : "LEARN");
        expCal.setButtonText (proc.isCalibrating() ? "ROCK IT!" : "CALIBRATE");
        static const char* curves[] = { "CURVE: STRAIGHT", "CURVE: SLOW START", "CURVE: FAST START" };
        expCurve.setButtonText (curves[proc.pedals().curve]);
        expTarget.setButtonText ("CONTROLS  " + names::expTargetNames()[(int) proc.getParam (ParamIDs::expTarget)]);
        const auto& d = proc.pedals().footDevice;
        deviceBtn.setButtonText (d.isEmpty() ? "PEDAL: AUTOMATIC" : (d == "none" ? "PEDAL: THROUGH THE DAW" : "PEDAL: " + d.toUpperCase()));
        repaint (0, 100, 1040, 360);
    }

    void paint (Graphics& g) override
    {
        auto all = getLocalBounds().toFloat();
        g.fillAll (col::sky);
        grime (g, all, 11);
        chunkyText (g, "FOOTSWITCHES", fonts().display (54.0f), { 24.0f, 18.0f, 440.0f, 70.0f }, col::paper, col::ink, 6, 6.0f);
        drawBox (g, { 470.0f, 26.0f, 384.0f, 56.0f }, col::paper, 60, 5.0f);
        g.setColour (col::ink);
        g.setFont (fonts().label (12.0f));
        g.drawText ("SHOW JOBS FOR", Rectangle<float> (862.0f, 26.0f, 140.0f, 56.0f), Justification::centredLeft);

        const int down = proc.footDownMask();
        for (int k = 0; k < 4; ++k)
        {
            Rectangle<float> card (24.0f + (float) k * 254.0f, 110.0f, 236.0f, 340.0f);
            drawBox (g, card, col::paper, 70 + k, 7.0f);
            const Point<float> c (card.getCentreX(), card.getY() + 64.0f);
            auto ring = wobblyCircle (c, 46.0f, 3 + k, 1.6f);
            g.setColour (col::ink); g.fillPath (ring, AffineTransform::translation (4.0f, 4.0f));
            g.setColour (Colour (0xffc9c2b8)); g.fillPath (ring);
            g.setColour (col::ink); g.strokePath (ring, PathStrokeType (4.5f));
            const bool isDown = (down >> k) & 1;
            g.setColour (isDown ? col::lime : col::pink);
            g.fillEllipse (Rectangle<float> (52.0f, 52.0f).withCentre (c));
            g.setColour (col::ink);
            g.drawEllipse (Rectangle<float> (52.0f, 52.0f).withCentre (c), 4.0f);
            chunkyText (g, String::charToString ((juce_wchar) ('A' + k)), fonts().display (30.0f), Rectangle<float> (52.0f, 52.0f).withCentre (c.translated (0.0f, -1.0f)),
                        col::paper, col::ink, 0, 4.0f, Justification::centred);
            if (k == 3)
            {
                g.setColour (col::grey);
                g.setFont (fonts().label (11.0f));
                g.drawText ("for 4-switch pedals", Rectangle<float> (card.getX(), card.getY() + 122.0f, card.getWidth(), 16.0f), Justification::centred);
            }
        }

        Rectangle<float> combos (24.0f, 470.0f, 1002.0f, 104.0f);
        drawBox (g, combos, col::paper, 80, 6.0f);
        chunkyText (g, "CHANGE MODE WITH TWO SWITCHES", fonts().display (22.0f), { 40.0f, 478.0f, 600.0f, 32.0f }, col::ink, col::ink, 0, 0.5f);
        const char* combo[3][2] = { { "A + B", "LOOPER" }, { "B + C", "TAP" }, { "A + C", "PATCH" } };
        for (int i = 0; i < 3; ++i)
        {
            Rectangle<float> chip (44.0f + (float) i * 320.0f, 514.0f, 76.0f, 28.0f);
            drawBox (g, chip, col::pink, 90 + i, 0.0f, 3.0f, 6.0f);
            g.setColour (col::ink);
            g.setFont (fonts().label (14.0f));
            g.drawText (combo[i][0], chip, Justification::centred);
            g.drawText (String ("> ") + combo[i][1], Rectangle<float> (chip.getRight() + 8.0f, chip.getY(), 200.0f, 28.0f), Justification::centredLeft);
        }
        g.setFont (fonts().hand (15.0f));
        g.drawText ("Same pair again goes back to PLAY. Double-tap works wherever a hold does. Every job is also on the main panel.",
                    Rectangle<float> (40.0f, 544.0f, 980.0f, 26.0f), Justification::centredLeft);

        Rectangle<float> extras (24.0f, 594.0f, 1002.0f, 284.0f);
        drawBox (g, extras, col::yellow, 81, 6.0f);
        chunkyText (g, "EXTRAS", fonts().display (24.0f), { 40.0f, 602.0f, 200.0f, 34.0f }, col::ink, col::ink, 0, 0.5f);
        g.setColour (col::ink);
        g.setFont (fonts().label (11.5f));
        g.drawFittedText ("Trails: tails ring on after switching off.  Merge: tails blend when you change type or preset.  Kill dry: for a send / return track.  "
                          "Global EQ shapes the effects only, unless 'EQ on dry too' is on.",
                          Rectangle<int> (40, 818, 970, 52), Justification::topLeft, 3);

        drawBox (g, { 1056.0f, 110.0f, 280.0f, 360.0f }, col::yellow, 82, 6.0f);
        chunkyText (g, "YOUR PEDAL", fonts().display (24.0f), { 1072.0f, 118.0f, 240.0f, 34.0f }, col::ink, col::ink, 0, 0.5f);
        drawBox (g, { 1056.0f, 490.0f, 280.0f, 300.0f }, col::paper, 83, 6.0f);
        chunkyText (g, "EXPRESSION", fonts().display (24.0f), { 1072.0f, 498.0f, 240.0f, 34.0f }, col::ink, col::ink, 0, 0.5f);
    }

    void resized() override
    {
        backBtn.setBounds (1196, 22, 140, 60);
        int x = 474;
        const int tabW[4] = { 82, 66, 116, 104 };
        for (int m = 0; m < 4; ++m) { viewTabs[m]->setBounds (x, 32, tabW[m], 44); x += tabW[m]; }
        for (int k = 0; k < 4; ++k)
        {
            const int cx = 24 + k * 254;
            jobReadouts[(size_t) k]->setBounds (cx + 10, 250, 216, 74);
            listenReadouts[(size_t) k]->setBounds (cx + 14, 330, 210, 52);
            learnBtns[k]->setBounds (cx + 14, 388, 208, 50);
        }
        status->setBounds (1070, 156, 252, 62);
        deviceBtn.setBounds (1066, 222, 262, 46);
        teachBtn.setBounds (1066, 274, 262, 58);
        teachText->setBounds (1070, 336, 252, 72);
        resetBtn.setBounds (1066, 412, 262, 44);
        expTarget.setBounds (1066, 540, 262, 46);
        expBar.setBounds (1070, 594, 256, 30);
        expLearn.setBounds (1066, 632, 128, 46);
        expCal.setBounds (1200, 632, 128, 46);
        expCurve.setBounds (1066, 684, 262, 44);
        expListen->setBounds (1070, 734, 256, 40);
        for (int i = 0; i < extraToggles.size(); ++i)
            extraToggles[i]->setBounds (40 + (i % 4) * 172, 646 + (i / 4) * 60, 166, 54);
        for (int i = 0; i < 3; ++i) eqKnobs[(size_t) i]->setBounds (734 + i * 92, 640, 92, 128);
    }

private:
    KddProcessor& proc;
    int viewMode = 0;
    Chunky backBtn { "BACK" };
    OwnedArray<Chunky> viewTabs, learnBtns, extraToggles;
    std::vector<std::unique_ptr<Readout>> jobReadouts, listenReadouts;
    std::unique_ptr<Readout> status, teachText, expListen;
    Chunky deviceBtn { "PEDAL" }, teachBtn { "TEACH MY PEDAL" }, resetBtn { "RESET FOOTSWITCHES" };
    Chunky expTarget { "CONTROLS" }, expLearn { "LEARN" }, expCal { "CALIBRATE" }, expCurve { "CURVE" };
    ExpressionBar expBar { proc };
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment>> toggleAttach;
    std::vector<std::unique_ptr<Knob>> eqKnobs;
};

// =====================================================================================
// Editor
// =====================================================================================
static constexpr int kDesignW = 1360, kDesignH = 900;

KddEditor::KddEditor (KddProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    mainPage = std::make_unique<MainPage> (p, [this] { showPedalPage (true); });
    pedalPage = std::make_unique<PedalPage> (p, [this] { showPedalPage (false); });
    for (auto* c : { (Component*) mainPage.get(), (Component*) pedalPage.get() })
    {
        c->setBounds (0, 0, kDesignW, kDesignH);
        holder.addChildComponent (c);
    }
    mainPage->setVisible (true);
    holder.setBounds (0, 0, kDesignW, kDesignH);
    addAndMakeVisible (holder);
    setResizable (true, true);
    setResizeLimits (kDesignW / 2, kDesignH / 2, kDesignW * 3 / 2, kDesignH * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    setSize (kDesignW * 4 / 5, kDesignH * 4 / 5);
    addMouseListener (this, true);
}

// touching the panel hands the footswitches to this copy
void KddEditor::mouseDown (const MouseEvent&) { proc.takeFootswitches(); }

KddEditor::~KddEditor() = default;

void KddEditor::resized()
{
    const float s = (float) getWidth() / (float) kDesignW;
    holder.setTransform (AffineTransform::scale (s));
}

void KddEditor::showPedalPage (bool show)
{
    mainPage->setVisible (! show);
    pedalPage->setVisible (show);
}
} // namespace ui
