#include "PluginEditor.h"
#include "BinaryData.h"

using TongaColours::yellow;
using TongaColours::pink;
using TongaColours::bgDark;
using TongaColours::padColour;

namespace
{
    juce::String fmtTime (double s)
    {
        const int t = juce::jmax (0, (int) s);
        return juce::String::formatted ("%02d:%02d", t / 60, t % 60);
    }

    void setBoolParam (juce::AudioProcessorValueTreeState& a, const char* id, bool v)
    {
        if (auto* p = a.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (v ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    }

    const double divisionBeats[5] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
    const char* divisionNames[5] = { "1/4", "1/2", "1", "2", "4" };

    // Modo (enum del procesador) <-> id del desplegable
    const int modeOfId[5] = { 0, 0, 3, 2, 1 };   // 1 Slicer, 2 Random, 3 Samples, 4 Transientes
    const int idOfMode[4] = { 1, 4, 3, 2 };
}

//==============================================================================
// WaveView
//==============================================================================
WaveView::WaveView (TongaProcessor& p) : proc (p) {}

double WaveView::xToSec (float x) const
{
    return viewStart + (double) x / (double) juce::jmax (1, getWidth()) * viewLen;
}

float WaveView::secToX (double s) const
{
    return (float) ((s - viewStart) / viewLen * (double) getWidth());
}

void WaveView::clampView()
{
    const double dur = juce::jmax (0.02, proc.getLengthSec());
    viewLen = juce::jlimit (0.02, dur, viewLen);
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, dur - viewLen), viewStart);
}

void WaveView::setViewStart (double s)
{
    viewStart = s;
    clampView();
    repaint();
}

void WaveView::resetView()
{
    viewStart = 0.0;
    viewLen = juce::jmax (0.02, proc.getLengthSec());
    repaint();
}

void WaveView::showPad (int pad)
{
    const auto s = proc.getSlice (pad);
    if (! s.valid)
        return;

    const double len = s.end - s.start;
    if (len * 1.2 > viewLen)
        viewLen = len * 1.4;

    if (s.start < viewStart || s.start > viewStart + viewLen)
        viewStart = s.start - viewLen * 0.1;

    clampView();
    repaint();
}

void WaveView::showTime (double sec)
{
    if (sec < viewStart || sec > viewStart + viewLen)
    {
        viewStart = sec - viewLen * 0.1;
        clampView();
        repaint();
    }
}

void WaveView::setGridMode (bool on)
{
    gridMode = on;
    if (on)
    {
        gridA = proc.getGridOffset();
        gridB = gridA + proc.getBarSeconds();
        const double dur = proc.getLengthSec();
        if (gridB > dur)
        {
            gridB = dur;
            gridA = juce::jmax (0.0, gridB - proc.getBarSeconds());
        }
    }
    repaint();
}

void WaveView::commitGrid()
{
    if (! gridMode)
        return;

    const double barLen = gridB - gridA;
    if (barLen > 0.05)
    {
        const double bpm = proc.getQuartersPerBar() * 60.0 / barLen;
        proc.setGrid (gridA, bpm);
        if (proc.getLastMode() == TongaProcessor::modeSlicer)
            proc.applySlicing (TongaProcessor::modeSlicer);
    }
    gridMode = false;
    repaint();
}

void WaveView::paint (juce::Graphics& g)
{
    g.fillAll (TongaColours::inset);

    if (! proc.hasSample())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (16.0f);
        g.drawText ("Arrastra un audio aca o toca \"Cargar audio\"", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int w = getWidth();
    const int h = getHeight();
    const float mid = (float) h * 0.5f;

    // Bandas de los pads asignados (cada uno con su color)
    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        const auto s = proc.getSlice (i);
        if (! s.valid)
            continue;

        const float x0 = secToX (s.start);
        const float x1 = secToX (s.end);
        if (x1 < 0.0f || x0 > (float) w)
            continue;

        g.setColour (padColour (i).withAlpha (i == selectedPad ? 0.30f : 0.12f));
        g.fillRect (x0, 0.0f, juce::jmax (1.0f, x1 - x0), (float) h);

        g.setColour (padColour (i));
        g.fillRect (x0, 0.0f, 1.0f, (float) h);
        g.setFont (11.0f);
        g.drawText (juce::String (i + 1), (int) x0 + 4, 3, 24, 13, juce::Justification::left);
    }

    // Onda
    proc.getPeaks (viewStart, viewLen, w, mins, maxs);
    g.setColour (juce::Colour (0xffe8e8ef));
    for (int x = 0; x < w; ++x)
    {
        const float mn = mins[(size_t) x];
        const float mx = maxs[(size_t) x];
        const float top = mid - mx * mid * 0.92f;
        const float hh = juce::jmax (1.0f, (mx - mn) * mid * 0.92f);
        g.fillRect ((float) x, top, 1.0f, hh);
    }

    // Grilla (compases y tiempos)
    const double q = proc.getQuartersPerBar();
    const double barS = gridMode ? (gridB - gridA) : proc.getBarSeconds();
    const double off = gridMode ? gridA : proc.getGridOffset();

    if (barS > 0.01)
    {
        const double pxBar = barS / viewLen * (double) w;
        if (pxBar >= 6.0)
        {
            const double beatS = barS / q;
            const double pxBeat = beatS / viewLen * (double) w;
            const long long k0 = (long long) std::floor ((viewStart - off) / barS);

            for (long long k = k0; k < k0 + 5000; ++k)
            {
                const double tBar = off + (double) k * barS;
                if (tBar > viewStart + viewLen)
                    break;

                if (pxBeat >= 14.0)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.08f));
                    for (int j = 1; (double) j * beatS < barS - 1.0e-6; ++j)
                    {
                        const double t = tBar + (double) j * beatS;
                        if (t >= 0.0 && t >= viewStart && t <= viewStart + viewLen)
                            g.drawVerticalLine ((int) secToX (t), 0.0f, (float) h);
                    }
                }

                if (tBar >= 0.0 && tBar >= viewStart)
                {
                    g.setColour (yellow.withAlpha (0.30f));
                    g.drawVerticalLine ((int) secToX (tBar), 0.0f, (float) h);
                }
            }
        }
    }

    if (gridMode)
    {
        // Manijas A y B
        const float xa = secToX (gridA);
        const float xb = secToX (gridB);
        g.setColour (pink);
        g.fillRect (xa - 1.5f, 0.0f, 3.0f, (float) h);
        g.fillRect (xb - 1.5f, 0.0f, 3.0f, (float) h);
        g.fillEllipse (xa - 8.0f, mid - 8.0f, 16.0f, 16.0f);
        g.fillEllipse (xb - 8.0f, mid - 8.0f, 16.0f, 16.0f);
        g.setColour (juce::Colours::black);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText ("A", (int) xa - 8, (int) mid - 8, 16, 16, juce::Justification::centred);
        g.drawText ("B", (int) xb - 8, (int) mid - 8, 16, 16, juce::Justification::centred);
    }
    else
    {
        // Marcas de inicio y fin del pad seleccionado (se arrastran)
        const auto s = proc.getSlice (selectedPad);
        if (s.valid)
        {
            const float xs = secToX (s.start);
            const float xe = secToX (s.end);
            g.setColour (padColour (selectedPad));
            g.fillRect (xs - 1.0f, 0.0f, 2.0f, (float) h);
            g.fillRect (xe - 1.0f, 0.0f, 2.0f, (float) h);

            juce::Path tri;
            tri.addTriangle (xs, 0.0f, xs + 12.0f, 0.0f, xs, 12.0f);
            tri.addTriangle (xe, 0.0f, xe - 12.0f, 0.0f, xe, 12.0f);
            g.fillPath (tri);
        }
    }

    // Cursor (donde se asigna un pad vacio y desde donde suena el play)
    const double cur = proc.isAuditioning() ? proc.getAuditionPos() : proc.getCursor();
    const float xc = secToX (cur);
    if (xc >= -2.0f && xc <= (float) w + 2.0f)
    {
        g.setColour (juce::Colours::white);
        g.fillRect (xc - 1.0f, 0.0f, 2.0f, (float) h);
        juce::Path tri;
        tri.addTriangle (xc - 6.0f, (float) h, xc + 6.0f, (float) h, xc, (float) h - 9.0f);
        g.fillPath (tri);
    }

    // Cabezal del pad que esta sonando
    const double pos = proc.lastPlayPosSec.load();
    if (pos >= 0.0)
    {
        g.setColour (pink);
        g.fillRect (secToX (pos) - 1.0f, 0.0f, 2.0f, (float) h);
    }
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    if (! proc.hasSample())
        return;

    drag = dragScroll;
    dragStartView = viewStart;
    const float x = (float) e.x;

    if (gridMode)
    {
        if (std::abs (x - secToX (gridA)) < 10.0f)       drag = dragGridA;
        else if (std::abs (x - secToX (gridB)) < 10.0f)  drag = dragGridB;
    }
    else
    {
        const auto s = proc.getSlice (selectedPad);
        if (s.valid)
        {
            if (std::abs (x - secToX (s.start)) < 8.0f)      drag = dragPadStart;
            else if (std::abs (x - secToX (s.end)) < 8.0f)   drag = dragPadEnd;
        }
    }
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (! proc.hasSample() || drag == dragNone)
        return;

    const double dur = proc.getLengthSec();
    const double t = juce::jlimit (0.0, dur, xToSec ((float) e.x));

    switch (drag)
    {
        case dragScroll:
            viewStart = dragStartView - (double) e.getDistanceFromDragStartX() / (double) juce::jmax (1, getWidth()) * viewLen;
            clampView();
            break;

        case dragGridA:
        {
            const double newA = juce::jmax (0.0, t);
            const double d = newA - gridA;
            gridA += d;
            gridB += d;
            break;
        }

        case dragGridB:
            gridB = juce::jmax (gridA + 0.05, t);
            break;

        case dragPadStart:
        {
            auto s = proc.getSlice (selectedPad);
            s.start = juce::jlimit (0.0, s.end - 0.002, t);
            proc.setSlice (selectedPad, s);
            break;
        }

        case dragPadEnd:
        {
            auto s = proc.getSlice (selectedPad);
            s.end = juce::jlimit (s.start + 0.002, dur, t);
            proc.setSlice (selectedPad, s);
            break;
        }

        default: break;
    }
    repaint();
}

void WaveView::mouseUp (const juce::MouseEvent& e)
{
    // Un click simple (sin arrastrar) pone el cursor
    if (drag == dragScroll && ! gridMode && e.getDistanceFromDragStart() < 4 && proc.hasSample())
    {
        proc.setCursor (xToSec ((float) e.x));
        repaint();
    }
    drag = dragNone;
}

void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! proc.hasSample())
        return;

    const double under = xToSec ((float) e.x);
    const double factor = std::pow (0.85, (double) wheel.deltaY * 6.0);
    viewLen *= factor;

    const double dur = juce::jmax (0.02, proc.getLengthSec());
    viewLen = juce::jlimit (0.02, dur, viewLen);
    viewStart = under - ((double) e.x / (double) juce::jmax (1, getWidth())) * viewLen;
    clampView();
    repaint();
}

//==============================================================================
// OverviewView
//==============================================================================
void OverviewView::paint (juce::Graphics& g)
{
    g.setColour (TongaColours::inset);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);

    if (! proc.hasSample())
        return;

    const int w = getWidth();
    const int h = getHeight();
    const double dur = proc.getLengthSec();

    const juce::String key = proc.getSampleName() + "|" + juce::String (dur, 3) + "|" + juce::String (w);
    if (key != cacheKey)
    {
        proc.getPeaks (0.0, dur, w, mins, maxs);
        cacheKey = key;
    }

    const float mid = (float) h * 0.5f - 2.0f;
    g.setColour (juce::Colour (0xff8a8a96));
    for (int x = 0; x < w && x < (int) mins.size(); ++x)
    {
        const float top = mid - maxs[(size_t) x] * mid;
        const float hh = juce::jmax (1.0f, (maxs[(size_t) x] - mins[(size_t) x]) * mid);
        g.fillRect ((float) x, top, 1.0f, hh);
    }

    // Franjitas de color: los pads asignados
    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        const auto s = proc.getSlice (i);
        if (! s.valid)
            continue;
        const float x0 = (float) (s.start / dur * (double) w);
        const float x1 = (float) (s.end / dur * (double) w);
        g.setColour (padColour (i));
        g.fillRect (x0, (float) h - 4.0f, juce::jmax (1.5f, x1 - x0), 3.0f);
    }

    // Zona que se ve en la onda grande
    const float vx0 = (float) (wave.getViewStart() / dur * (double) w);
    const float vx1 = (float) ((wave.getViewStart() + wave.getViewLen()) / dur * (double) w);
    g.setColour (yellow.withAlpha (0.12f));
    g.fillRect (vx0, 0.0f, vx1 - vx0, (float) h);
    g.setColour (yellow);
    g.drawRect (vx0, 0.5f, juce::jmax (2.0f, vx1 - vx0), (float) h - 1.0f, 1.0f);

    // Cursor
    const double cur = proc.isAuditioning() ? proc.getAuditionPos() : proc.getCursor();
    g.setColour (juce::Colours::white);
    g.fillRect ((float) (cur / dur * (double) w) - 0.5f, 0.0f, 1.5f, (float) h);
}

void OverviewView::moveTo (const juce::MouseEvent& e)
{
    if (! proc.hasSample())
        return;
    const double sec = (double) e.x / (double) juce::jmax (1, getWidth()) * proc.getLengthSec();
    wave.setViewStart (sec - wave.getViewLen() * 0.5);
}

//==============================================================================
// PadComp
//==============================================================================
void PadComp::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const juce::Colour pc = padColour (index);

    juce::Colour fill = hasSlice ? pc.withMultipliedBrightness (0.55f) : juce::Colour (0xff222229);
    if (playing)
        fill = pc.brighter (0.2f);

    g.setColour (fill);
    g.fillRoundedRectangle (b, 5.0f);

    if (selected)
    {
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (b, 5.0f, 2.0f);
    }

    // Etiqueta con el numero, arriba a la derecha
    const juce::Rectangle<float> tag (b.getRight() - 24.0f, b.getY() + 4.0f, 20.0f, 14.0f);
    g.setColour (hasSlice ? juce::Colours::black.withAlpha (0.35f) : juce::Colour (0xff33333d));
    g.fillRoundedRectangle (tag, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (hasSlice ? 0.95f : 0.5f));
    g.setFont (10.0f);
    g.drawText (juce::String (index + 1), tag.toNearestInt(), juce::Justification::centred);
}

void PadComp::mouseDown (const juce::MouseEvent& e)
{
    if (onDown)
        onDown (index, e.mods);
}

void PadComp::mouseUp (const juce::MouseEvent&)
{
    if (onUp)
        onUp (index);
}

//==============================================================================
// StemPanel
//==============================================================================
StemPanel::StemPanel (juce::AudioProcessorValueTreeState& apvts)
{
    // Orden en pantalla: Voz, Bateria, Bajo, Otros. El modelo los entrega: 0 bateria, 1 bajo, 2 otros, 3 voz.
    const char* names[4] = { "Voz", "Bateria", "Bajo", "Otros" };
    const int paramIndex[4] = { 3, 0, 1, 2 };

    for (int i = 0; i < 4; ++i)
    {
        auto& k = knobs[(size_t) i];

        k.name.setText (names[i], juce::dontSendNotification);
        k.name.setJustificationType (juce::Justification::centred);
        k.name.setFont (juce::Font (12.0f));
        k.name.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (k.name);

        k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, true);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        k.slider.setTextBoxIsEditable (false);
        k.slider.setDoubleClickReturnValue (true, 1.0);
        addAndMakeVisible (k.slider);

        atts[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, juce::String ("stem") + juce::String (paramIndex[i]), k.slider);

        k.slider.textFromValueFunction = [] (double v) -> juce::String
        {
            return juce::String (juce::roundToInt (v * 100.0)) + "%";
        };
        k.slider.updateText();
    }

    addAndMakeVisible (removeButton);
    removeButton.onClick = [this] { if (onRemove) onRemove(); };
}

void StemPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xf0121217));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (yellow.withAlpha (0.55f));
    g.drawRoundedRectangle (b, 10.0f, 1.5f);

    g.setColour (juce::Colours::grey);
    g.setFont (11.0f);
    g.drawText ("STEMS", 14, 6, 80, 14, juce::Justification::left);
}

void StemPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    r.removeFromTop (12);
    removeButton.setBounds (r.removeFromRight (84).withSizeKeepingCentre (84, 28));
    r.removeFromRight (8);

    const int w = r.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = r.removeFromLeft (w);
        knobs[(size_t) i].name.setBounds (cell.removeFromTop (16));
        knobs[(size_t) i].slider.setBounds (cell);
    }
}

//==============================================================================
// TongaEditor
//==============================================================================
void TongaEditor::styleIncDec (juce::Slider& s, int textW)
{
    s.setSliderStyle (juce::Slider::IncDecButtons);
    s.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_AutoDirection);
    s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, textW, 22);
    s.setColour (juce::Slider::textBoxBackgroundColourId, TongaColours::inset);
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    addAndMakeVisible (s);
}

void TongaEditor::setupFader (int i, const juce::String& label, bool knob, double mn, double mx, double step, double def,
                              std::function<juce::String (double)> text,
                              std::function<void (PadSlice&, float)> apply,
                              std::function<float (const PadSlice&)> read)
{
    auto& f = faders[(size_t) i];

    f.name.setText (label, juce::dontSendNotification);
    f.name.setJustificationType (juce::Justification::centred);
    f.name.setFont (juce::Font (12.0f));
    f.name.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (f.name);

    if (knob)
    {
        f.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        f.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, true);
        f.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        f.slider.setTextBoxIsEditable (false);
        addAndMakeVisible (f.slider);
    }
    else
    {
        styleIncDec (f.slider, 64);
        f.slider.setTextBoxIsEditable (true);
        f.slider.valueFromTextFunction = [] (const juce::String& t)
        {
            return t.retainCharacters ("-+0123456789.").getDoubleValue();
        };
    }

    f.slider.setRange (mn, mx, step);
    f.slider.setDoubleClickReturnValue (true, def);
    f.slider.textFromValueFunction = text;
    f.slider.setValue (def, juce::dontSendNotification);

    f.apply = apply;
    f.read = read;
    f.slider.onValueChange = [this, i]
    {
        if (updatingControls)
            return;
        const float v = (float) faders[(size_t) i].slider.getValue();
        auto fn = faders[(size_t) i].apply;
        applyToSelected ([fn, v] (PadSlice& s) { fn (s, v); });
    };
}

TongaEditor::TongaEditor (TongaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wave (p), overview (p, wave)
{
    setLookAndFeel (&look);
    setSize (960, 720);

    addAndMakeVisible (wave);
    addAndMakeVisible (overview);

    // --- Encabezado ---
    logo.setImage (juce::ImageCache::getFromMemory (BinaryData::TONGA_SAMPLER_png,
                                                    BinaryData::TONGA_SAMPLER_pngSize));
    logo.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logo);

    title.setText ("Tonga Sampler 2", juce::dontSendNotification);
    title.setFont (juce::Font (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, yellow);
    addAndMakeVisible (title);

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Elegi un audio", juce::File(),
                                                       "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile())
                                      loadSample (f);
                              });
    };

    // --- Franja de informacion ---
    sampleLabel.setFont (juce::Font (15.0f));
    sampleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (sampleLabel);

    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setColour (juce::Label::textColourId, yellow);
    addAndMakeVisible (bpmLabel);

    addAndMakeVisible (bpmHalf);
    addAndMakeVisible (bpmDouble);
    bpmHalf.onClick = [this] { scaleOriginalBpm (0.5); };
    bpmDouble.onClick = [this] { scaleOriginalBpm (2.0); };

    sigCaption.setText ("COMPAS", juce::dontSendNotification);
    sigCaption.setFont (juce::Font (11.0f));
    sigCaption.setJustificationType (juce::Justification::centredRight);
    sigCaption.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (sigCaption);

    timeSigBox.addItemList (juce::StringArray { "2/4", "3/4", "4/4", "6/8", "9/8", "12/8" }, 1);
    addAndMakeVisible (timeSigBox);
    sigAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "timesig", timeSigBox);

    timeLabel.setJustificationType (juce::Justification::centredRight);
    timeLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (timeLabel);

    // --- Fila de control ---
    addAndMakeVisible (playButton);
    playButton.onClick = [this] { proc.toggleAudition(); };

    // Mantener (arriba) / Una vez hasta el final (abajo)
    holdToggle.drawTop = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour (c);
        const float cy = r.getCentreY();
        const float x0 = r.getCentreX() - 10.0f, x1 = r.getCentreX() + 10.0f;
        g.fillRect (x0, cy - 1.0f, x1 - x0, 2.0f);
        g.fillRect (x0, cy - 5.0f, 2.0f, 10.0f);
        g.fillRect (x1 - 2.0f, cy - 5.0f, 2.0f, 10.0f);
    };
    holdToggle.drawBottom = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour (c);
        const float cy = r.getCentreY();
        const float x0 = r.getCentreX() - 10.0f, x1 = r.getCentreX() + 10.0f;
        g.fillRect (x0, cy - 1.0f, x1 - x0 - 5.0f, 2.0f);
        juce::Path tri;
        tri.addTriangle (x1 - 8.0f, cy - 5.0f, x1 - 8.0f, cy + 5.0f, x1, cy);
        g.fillPath (tri);
    };
    holdToggle.onChange = [this] (bool bottom) { setBoolParam (proc.apvts, "hold", ! bottom); };
    addAndMakeVisible (holdToggle);

    auto textFn = [] (const juce::String& t)
    {
        return [t] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
        {
            g.setColour (c);
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            g.drawText (t, r.toNearestInt(), juce::Justification::centred);
        };
    };
    polyToggle.drawTop = textFn ("MONO");
    polyToggle.drawBottom = textFn ("POLY");
    polyToggle.onChange = [this] (bool bottom) { setBoolParam (proc.apvts, "poly", bottom); };
    addAndMakeVisible (polyToggle);

    addAndMakeVisible (stemsButton);
    stemsButton.onClick = [this]
    {
        if (proc.isStemBusy())
        {
            proc.cancelStemSeparation();
            return;
        }
        if (proc.hasStems())
        {
            stemPanelOpen = ! stemPanelOpen;
            return;
        }
        if (! proc.hasSample())
            return;

        juce::String why;
        if (! proc.stemsSupported (why))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Stems no disponibles", why);
            return;
        }
        proc.startStemSeparation();
    };

    addChildComponent (stemPanel);
    stemPanel.onRemove = [this]
    {
        proc.removeStems();
        stemPanelOpen = false;
    };

    styleIncDec (bpmBox, 56);
    bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "bpm", bpmBox);

    addAndMakeVisible (syncButton);
    syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "sync", syncButton);

    keyLabel.setText ("KEY", juce::dontSendNotification);
    detuneLabel.setText ("DETUNE", juce::dontSendNotification);
    for (auto* l : { &keyLabel, &detuneLabel })
    {
        l->setFont (juce::Font (11.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, yellow);
        addAndMakeVisible (*l);
    }

    styleIncDec (keyBox, 54);
    keyAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "gkey", keyBox);
    keyBox.textFromValueFunction = [] (double v) -> juce::String
    {
        return juce::String (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v));
    };
    keyBox.updateText();

    styleIncDec (detuneBox, 54);
    detuneAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "gdetune", detuneBox);
    detuneBox.textFromValueFunction = [] (double v) -> juce::String
    {
        return juce::String (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
    };
    detuneBox.updateText();

    // --- Perillas y cajas del pad seleccionado ---
    setupFader (0, "Nivel", true, 0.0, 2.0, 0.01, 1.0,
                [] (double v) -> juce::String { return juce::String (juce::roundToInt (v * 100.0)) + "%"; },
                [] (PadSlice& s, float v) { s.gain = v; },
                [] (const PadSlice& s) { return s.gain; });

    setupFader (1, "Filtro", true, -1.0, 1.0, 0.01, 0.0,
                [] (double v) -> juce::String
                {
                    if (std::abs (v) < 0.02) return "off";
                    return (v < 0.0 ? juce::String ("LP ") : juce::String ("HP "))
                           + juce::String (juce::roundToInt (std::abs (v) * 100.0));
                },
                [] (PadSlice& s, float v) { s.filter = v; },
                [] (const PadSlice& s) { return s.filter; });

    setupFader (2, "Ataque", true, 0.0, 2.0, 0.001, 0.0,
                [] (double v) -> juce::String
                {
                    if (v < 1.0) return juce::String (juce::roundToInt (v * 1000.0)) + " ms";
                    return juce::String (v, 2) + " s";
                },
                [] (PadSlice& s, float v) { s.attack = v; },
                [] (const PadSlice& s) { return s.attack; });
    faders[2].slider.setSkewFactorFromMidPoint (0.2);

    setupFader (3, "Release", true, 0.003, 3.0, 0.001, 0.02,
                [] (double v) -> juce::String
                {
                    if (v < 1.0) return juce::String (juce::roundToInt (v * 1000.0)) + " ms";
                    return juce::String (v, 2) + " s";
                },
                [] (PadSlice& s, float v) { s.release = v; },
                [] (const PadSlice& s) { return s.release; });
    faders[3].slider.setSkewFactorFromMidPoint (0.3);

    setupFader (4, "Velocidad", false, -50.0, 100.0, 1.0, 0.0,
                [] (double v) -> juce::String
                {
                    return juce::String (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v)) + "%";
                },
                [] (PadSlice& s, float v) { s.stretch = v; },
                [] (const PadSlice& s) { return s.stretch; });

    setupFader (5, "Tono del pad", false, -12.0, 12.0, 1.0, 0.0,
                [] (double v) -> juce::String
                {
                    return juce::String (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v));
                },
                [] (PadSlice& s, float v) { s.keyShift = juce::roundToInt (v); },
                [] (const PadSlice& s) { return (float) s.keyShift; });

    addAndMakeVisible (stretchHalf);
    addAndMakeVisible (stretchDouble);
    stretchHalf.onClick = [this] { scaleStretch (0.5); };
    stretchDouble.onClick = [this] { scaleStretch (2.0); };

    reverseButton.onClick = [this]
    {
        if (updatingControls)
            return;
        const bool r = reverseButton.getToggleState();
        applyToSelected ([r] (PadSlice& s) { s.reverse = r; });
    };
    addAndMakeVisible (reverseButton);

    // --- Fila de abajo: modo de corte ---
    modeBox.addItem ("Set Slicer", 1);
    modeBox.addItem ("Set Random", 2);
    modeBox.addItem ("Find Samples", 3);
    modeBox.addItem ("Transientes", 4);
    modeBox.setTextWhenNothingSelected ("Modo de corte");
    modeBox.onChange = [this]
    {
        const int id = modeBox.getSelectedId();
        if (id >= 1 && id <= 4)
            doSlicing (modeOfId[id]);
    };
    addAndMakeVisible (modeBox);

    for (auto* b : { &pageLeft, &pageRight, &divDec, &divInc })
        addChildComponent (*b);
    pageLeft.onClick = [this] { shiftSlicer (-1); };
    pageRight.onClick = [this] { shiftSlicer (1); };
    divDec.onClick = [this] { stepDivision (-1); };
    divInc.onClick = [this] { stepDivision (1); };

    divLabel.setJustificationType (juce::Justification::centred);
    divLabel.setFont (juce::Font (15.0f, juce::Font::bold));
    divLabel.setColour (juce::Label::textColourId, yellow);
    addChildComponent (divLabel);

    addChildComponent (rerollButton);
    rerollButton.onClick = [this] { doSlicing (TongaProcessor::modeRandom); };

    addAndMakeVisible (selectAllButton);
    addAndMakeVisible (clearButton);
    selectAllButton.onClick = [this]
    {
        if (selMask == 0xFFFFFFFFu)
            selMask = 1u << curPad;
        else
            selMask = 0xFFFFFFFFu;
        refreshPadStates();
    };
    clearButton.onClick = [this]
    {
        for (int i = 0; i < TongaProcessor::numPads; ++i)
            if (selMask & (1u << i))
                proc.clearPad (i);
        refreshPadStates();
        syncPadControls();
        wave.repaint();
    };

    hintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    hintLabel.setFont (juce::Font (12.0f));
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (gridButton);
    addChildComponent (saveGridButton);
    addChildComponent (cancelGridButton);
    gridButton.onClick = [this] { wave.setGridMode (true); };
    saveGridButton.onClick = [this] { wave.commitGrid(); syncPadControls(); };
    cancelGridButton.onClick = [this] { wave.setGridMode (false); };

    // --- Pads ---
    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        pads[i].index = i;
        pads[i].onDown = [this] (int pad, const juce::ModifierKeys& m) { padClicked (pad, m); };
        pads[i].onUp = [this] (int pad) { proc.releasePad (pad); };
        addAndMakeVisible (pads[i]);
    }

    if (proc.hasSample())
        wave.resetView();

    lastCounter = proc.playCounter.load();
    wave.setSelectedPad (curPad);
    syncPadControls();
    refreshPadStates();
    startTimerHz (30);
}

TongaEditor::~TongaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TongaEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgDark);

    g.setColour (TongaColours::inset);
    g.fillRoundedRectangle (infoStrip.toFloat(), 8.0f);

    g.setColour (TongaColours::panel);
    g.fillRoundedRectangle (knobPanel.toFloat(), 8.0f);
}

void TongaEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    // Encabezado: marca al centro, cargar audio a la derecha
    auto top = r.removeFromTop (36);
    loadButton.setBounds (top.removeFromRight (110).reduced (0, 4));
    {
        auto c = top.withSizeKeepingCentre (36 + 8 + 190, 36);
        logo.setBounds (c.removeFromLeft (36));
        c.removeFromLeft (8);
        title.setBounds (c);
    }

    // Franja de informacion
    r.removeFromTop (8);
    infoStrip = r.removeFromTop (44);
    {
        auto info = infoStrip.reduced (12, 6);
        timeLabel.setBounds (info.removeFromRight (170));
        info.removeFromRight (10);
        timeSigBox.setBounds (info.removeFromRight (64).reduced (0, 2));
        sigCaption.setBounds (info.removeFromRight (60));
        info.removeFromRight (12);
        bpmDouble.setBounds (info.removeFromRight (32).reduced (0, 2));
        info.removeFromRight (4);
        bpmHalf.setBounds (info.removeFromRight (32).reduced (0, 2));
        info.removeFromRight (6);
        bpmLabel.setBounds (info.removeFromRight (240));
        sampleLabel.setBounds (info);
    }

    // Fila de control: play, mantener, mono/poly ... BPM, sync, key/detune
    r.removeFromTop (8);
    {
        auto cr = r.removeFromTop (46);
        playButton.setBounds (cr.removeFromLeft (76));
        cr.removeFromLeft (8);
        holdToggle.setBounds (cr.removeFromLeft (46));
        cr.removeFromLeft (8);
        polyToggle.setBounds (cr.removeFromLeft (58));
        cr.removeFromLeft (14);
        stemsButton.setBounds (cr.removeFromLeft (132).reduced (0, 8));

        auto kd = cr.removeFromRight (200);
        auto row1 = kd.removeFromTop (22);
        keyLabel.setBounds (row1.removeFromLeft (60));
        keyBox.setBounds (row1);
        detuneLabel.setBounds (kd.removeFromLeft (60));
        detuneBox.setBounds (kd.removeFromBottom (22));

        cr.removeFromRight (14);
        syncButton.setBounds (cr.removeFromRight (76).reduced (0, 8));
        cr.removeFromRight (8);
        bpmBox.setBounds (cr.removeFromRight (120).reduced (0, 10));
    }

    // Franja con todo el audio
    r.removeFromTop (8);
    overview.setBounds (r.removeFromTop (26));
    r.removeFromTop (4);

    // De abajo hacia arriba: pads, fila de modo, perillas; lo que sobra es la onda
    auto padArea = r.removeFromBottom (4 * 40);
    r.removeFromBottom (8);
    auto bottomRow = r.removeFromBottom (34);
    r.removeFromBottom (8);
    auto knobRow = r.removeFromBottom (108);
    r.removeFromBottom (8);
    wave.setBounds (r);
    stemPanel.setBounds (r.getRight() - 478, r.getY() + 8, 470, 132);

    // Perillas
    knobPanel = knobRow;
    {
        auto kr = knobRow.reduced (10, 4);
        for (int i = 0; i < 4; ++i)
        {
            auto cell = kr.removeFromLeft (92);
            faders[(size_t) i].name.setBounds (cell.removeFromBottom (16));
            faders[(size_t) i].slider.setBounds (cell);
        }
        kr.removeFromLeft (10);
        reverseButton.setBounds (kr.removeFromLeft (84).reduced (0, 12));
        kr.removeFromLeft (20);

        auto grp = kr.removeFromLeft (210);
        faders[4].name.setBounds (grp.removeFromBottom (16));
        auto row = grp.withSizeKeepingCentre (210, 28);
        faders[4].slider.setBounds (row.removeFromLeft (136));
        row.removeFromLeft (8);
        stretchHalf.setBounds (row.removeFromLeft (30));
        row.removeFromLeft (4);
        stretchDouble.setBounds (row.removeFromLeft (30));

        kr.removeFromLeft (20);
        auto grp2 = kr.removeFromLeft (150);
        faders[5].name.setBounds (grp2.removeFromBottom (16));
        faders[5].slider.setBounds (grp2.withSizeKeepingCentre (130, 28));
    }

    // Fila de modo de corte
    {
        auto br = bottomRow;
        modeBox.setBounds (br.removeFromLeft (170));
        br.removeFromLeft (10);

        auto slot = br.removeFromLeft (150);
        auto slot2 = slot;
        rerollButton.setBounds (slot2.removeFromLeft (100));
        pageLeft.setBounds (slot.removeFromLeft (28).reduced (2, 6));
        slot.removeFromLeft (6);
        divDec.setBounds (slot.removeFromLeft (22).reduced (2, 8));
        divLabel.setBounds (slot.removeFromLeft (34));
        divInc.setBounds (slot.removeFromLeft (22).reduced (2, 8));
        slot.removeFromLeft (6);
        pageRight.setBounds (slot.removeFromLeft (28).reduced (2, 6));

        br.removeFromLeft (10);
        selectAllButton.setBounds (br.removeFromLeft (140));
        br.removeFromLeft (6);
        clearButton.setBounds (br.removeFromLeft (70));
        br.removeFromLeft (10);

        auto gArea = br.removeFromRight (176);
        auto gb = gArea;
        gridButton.setBounds (gb.removeFromRight (90));
        cancelGridButton.setBounds (gArea.removeFromRight (84));
        gArea.removeFromRight (6);
        saveGridButton.setBounds (gArea.removeFromRight (84));
        hintLabel.setBounds (br);
    }

    // Pads: 8 columnas x 4 filas, el 1 abajo a la izquierda
    const int cw = padArea.getWidth() / 8;
    const int ch = padArea.getHeight() / 4;
    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        const int col = i % 8;
        const int row = 3 - i / 8;
        pads[i].setBounds (padArea.getX() + col * cw, padArea.getY() + row * ch, cw, ch);
    }
}

//==============================================================================
bool TongaEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".ogg")
            return true;
    }
    return false;
}

void TongaEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0)
        loadSample (juce::File (files[0]));
}

void TongaEditor::loadSample (const juce::File& f)
{
    if (proc.loadFile (f))
    {
        wave.setGridMode (false);
        wave.resetView();
        curPad = 0;
        selMask = 1u;
        wave.setSelectedPad (0);
        syncPadControls();
        refreshPadStates();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "No se pudo abrir",
                                                "Ese archivo no se pudo leer (o es demasiado largo).");
    }
}

void TongaEditor::padClicked (int pad, const juce::ModifierKeys& mods)
{
    if (mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown())
    {
        selMask ^= (1u << pad);
        if (selMask == 0)
            selMask = 1u << pad;
    }
    else
    {
        selMask = 1u << pad;
    }

    curPad = pad;
    proc.triggerPad (pad);   // si el pad esta vacio, se asigna en el cursor al sonar
    wave.setSelectedPad (pad);
    syncPadControls();
    refreshPadStates();
}

void TongaEditor::refreshPadStates()
{
    const uint32_t valid = proc.getValidMask();
    const uint32_t playing = proc.playingMask.load();

    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        const bool sel = (selMask & (1u << i)) != 0;
        const bool pl = (playing & (1u << i)) != 0;
        const bool hv = (valid & (1u << i)) != 0;

        if (pads[i].selected != sel || pads[i].playing != pl || pads[i].hasSlice != hv)
        {
            pads[i].selected = sel;
            pads[i].playing = pl;
            pads[i].hasSlice = hv;
            pads[i].repaint();
        }
    }
}

void TongaEditor::syncPadControls()
{
    updatingControls = true;
    const auto s = proc.getSlice (curPad);
    for (auto& f : faders)
        f.slider.setValue ((double) f.read (s), juce::dontSendNotification);
    reverseButton.setToggleState (s.reverse, juce::dontSendNotification);
    updatingControls = false;
}

void TongaEditor::doSlicing (int mode)
{
    proc.applySlicing (mode);
    syncPadControls();
    refreshPadStates();
    wave.repaint();
}

void TongaEditor::applyToSelected (const std::function<void (PadSlice&)>& fn)
{
    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        if (selMask & (1u << i))
        {
            auto s = proc.getSlice (i);
            fn (s);
            proc.setSlice (i, s);
        }
    }
}

void TongaEditor::scaleStretch (double factor)
{
    auto& sl = faders[4].slider;
    const double f = (1.0 + sl.getValue() / 100.0) * factor;
    sl.setValue (juce::jlimit (-50.0, 100.0, (f - 1.0) * 100.0), juce::sendNotificationSync);
}

void TongaEditor::scaleOriginalBpm (double factor)
{
    if (! proc.hasSample())
        return;
    proc.setGrid (proc.getGridOffset(), proc.getOriginalBpm() * factor);
    if (proc.getLastMode() == TongaProcessor::modeSlicer)
        needReslice = true;
}

void TongaEditor::stepDivision (int delta)
{
    if (auto* p = proc.apvts.getParameter ("division"))
    {
        const int idx = juce::jlimit (0, 4, (int) proc.apvts.getRawParameterValue ("division")->load() + delta);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) idx));
        needReslice = true;
    }
}

void TongaEditor::shiftSlicer (int dir)
{
    if (! proc.hasSample())
        return;

    const int idx = juce::jlimit (0, 4, (int) proc.apvts.getRawParameterValue ("division")->load());
    const double beat = 60.0 / juce::jmax (1.0, proc.getOriginalBpm());
    const double page = beat * divisionBeats[idx] * (double) TongaProcessor::numPads;
    proc.setCursor (proc.getCursor() + (double) dir * page);
    wave.showTime (proc.getCursor());
    needReslice = true;
}

void TongaEditor::timerCallback()
{
    // Re-cortar si se cambio la division o se movio el slicer
    if (needReslice)
    {
        needReslice = false;
        if (proc.getLastMode() == TongaProcessor::modeSlicer)
            proc.applySlicing (TongaProcessor::modeSlicer);
    }

    // Seguir al pad que esta sonando
    const int c = proc.playCounter.load();
    if (c != lastCounter)
    {
        lastCounter = c;
        const int p = proc.lastPlayedPad.load();
        if (p != curPad)
        {
            curPad = p;
            if (juce::countNumberOfBits (selMask) <= 1)
                selMask = 1u << p;
            syncPadControls();
        }
        wave.setSelectedPad (p);
        wave.showPad (p);
    }

    // Estado de los parametros
    const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    bpmBox.setEnabled (! sync);
    holdToggle.setBottomActive (! (proc.apvts.getRawParameterValue ("hold")->load() > 0.5f));
    polyToggle.setBottomActive (proc.apvts.getRawParameterValue ("poly")->load() > 0.5f);

    // Modo de corte y sus controles (el slicer solo muestra sus flechas si esta elegido)
    const int lm = proc.getLastMode();
    const int wantedId = (lm >= 0 && lm < 4) ? idOfMode[lm] : 0;
    if (modeBox.getSelectedId() != wantedId)
        modeBox.setSelectedId (wantedId, juce::dontSendNotification);

    const bool slicerOn = (lm == TongaProcessor::modeSlicer);
    for (auto* b : { &pageLeft, &pageRight, &divDec, &divInc })
        b->setVisible (slicerOn);
    divLabel.setVisible (slicerOn);
    rerollButton.setVisible (lm == TongaProcessor::modeRandom);
    if (slicerOn)
    {
        const int di = juce::jlimit (0, 4, (int) proc.apvts.getRawParameterValue ("division")->load());
        divLabel.setText (divisionNames[di], juce::dontSendNotification);
    }

    // Stems: boton, panel y avisos
    {
        const bool busy = proc.isStemBusy();
        const bool has = proc.hasStems();

        if (busy)
            stemsButton.setButtonText ("Cancelar " + juce::String (juce::roundToInt (proc.getStemProgress() * 100.0f)) + "%");
        else if (has)
            stemsButton.setButtonText ("Stems");
        else
            stemsButton.setButtonText ("Separar stems");

        if (has && ! hadStems)
            stemPanelOpen = true;
        hadStems = has;

        stemsButton.setToggleState (has && stemPanelOpen, juce::dontSendNotification);
        const bool showPanel = has && stemPanelOpen;
        if (showPanel && ! stemPanel.isVisible())
            stemPanel.toFront (false);
        stemPanel.setVisible (showPanel);

        // Si termino sin stems (error o cancelado), avisar una sola vez
        if (wasBusy && ! busy && ! has)
        {
            const juce::String msg = proc.getStemMessage();
            if (msg.isNotEmpty() && msg != "Cancelado")
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "No se pudieron separar los stems", msg);
        }
        wasBusy = busy;
    }

    // Textos
    bpmLabel.setText ("BPM " + juce::String (proc.getAppliedBpm(), 1)
                      + "   (orig " + juce::String (proc.getOriginalBpm(), 1) + ")",
                      juce::dontSendNotification);

    sampleLabel.setText (proc.hasSample() ? proc.getSampleName() : juce::String ("Sin audio"),
                         juce::dontSendNotification);

    const double posSec = proc.isAuditioning() ? proc.getAuditionPos() : proc.getCursor();
    timeLabel.setText ("TIME  " + fmtTime (posSec) + " / " + fmtTime (proc.getLengthSec()),
                       juce::dontSendNotification);

    playButton.playing = proc.isAuditioning();
    playButton.repaint();

    const bool g = wave.isGridMode();
    gridButton.setVisible (! g);
    saveGridButton.setVisible (g);
    cancelGridButton.setVisible (g);

    juce::String hint;
    if (g)
        hint = "A y B = inicio de 2 compases seguidos";
    else if (proc.isStemBusy())
        hint = "Separando stems... puede tardar unos minutos";
    else if (proc.hasSample() && lm < 0 && proc.getValidMask() == 0)
        hint = "Click en la onda = cursor. Pad vacio = se asigna ahi";
    hintLabel.setText (hint, juce::dontSendNotification);

    refreshPadStates();
    wave.repaint();
    overview.repaint();
}
