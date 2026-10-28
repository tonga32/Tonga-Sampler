#include "PluginEditor.h"
#include "BinaryData.h"

using TongaColours::yellow;
using TongaColours::pink;
using TongaColours::bgDark;

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
    g.fillAll (juce::Colour (0xff0e0e13));

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

    // Bandas de los pads asignados
    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        const auto s = proc.getSlice (i);
        if (! s.valid)
            continue;

        const float x0 = secToX (s.start);
        const float x1 = secToX (s.end);
        if (x1 < 0.0f || x0 > (float) w)
            continue;

        g.setColour (i == selectedPad ? yellow.withAlpha (0.16f) : pink.withAlpha (0.07f));
        g.fillRect (x0, 0.0f, juce::jmax (1.0f, x1 - x0), (float) h);

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (12.0f);
        g.drawText (juce::String (i + 1), (int) x0 + 3, 2, 26, 14, juce::Justification::left);
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
                    g.setColour (yellow.withAlpha (0.35f));
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
            g.setColour (yellow);
            g.fillRect (xs - 1.0f, 0.0f, 2.0f, (float) h);
            g.fillRect (xe - 1.0f, 0.0f, 2.0f, (float) h);

            juce::Path tri;
            tri.addTriangle (xs, 0.0f, xs + 11.0f, 0.0f, xs, 11.0f);
            tri.addTriangle (xe, 0.0f, xe - 11.0f, 0.0f, xe, 11.0f);
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
// PadComp
//==============================================================================
void PadComp::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (3.0f);

    juce::Colour base = hasSlice ? juce::Colour (0xff2e2e38) : juce::Colour (0xff1a1a20);
    if (playing)
        base = pink;

    g.setColour (base);
    g.fillRoundedRectangle (b, 8.0f);

    g.setColour (selected ? yellow : juce::Colour (0xff33333d));
    g.drawRoundedRectangle (b, 8.0f, selected ? 2.0f : 1.0f);

    g.setColour (playing ? juce::Colours::black : (hasSlice ? juce::Colours::white : juce::Colours::grey));
    g.setFont (juce::Font (15.0f, juce::Font::bold));
    g.drawText (juce::String (index + 1), getLocalBounds(), juce::Justification::centred);
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
// TongaEditor
//==============================================================================
void TongaEditor::setupFader (int i, const juce::String& label, double mn, double mx, double step, double def,
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

    f.slider.setSliderStyle (juce::Slider::LinearVertical);
    f.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    f.slider.setTextBoxIsEditable (false);
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
    addAndMakeVisible (f.slider);
}

TongaEditor::TongaEditor (TongaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wave (p)
{
    setLookAndFeel (&look);
    setSize (960, 640);

    addAndMakeVisible (wave);

    logo.setImage (juce::ImageCache::getFromMemory (BinaryData::TONGA_SAMPLER_png,
                                                    BinaryData::TONGA_SAMPLER_pngSize));
    logo.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logo);

    title.setText ("Tonga Sampler 2", juce::dontSendNotification);
    title.setFont (juce::Font (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, yellow);
    addAndMakeVisible (title);

    sampleLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    sampleLabel.setFont (juce::Font (12.0f));
    addAndMakeVisible (sampleLabel);

    // --- Encabezado ---
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

    addAndMakeVisible (syncButton);
    syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "sync", syncButton);

    timeSigBox.addItemList (juce::StringArray { "2/4", "3/4", "4/4", "6/8", "9/8", "12/8" }, 1);
    addAndMakeVisible (timeSigBox);
    sigAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "timesig", timeSigBox);

    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    addAndMakeVisible (bpmSlider);
    bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "bpm", bpmSlider);

    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setColour (juce::Label::textColourId, yellow);
    addAndMakeVisible (bpmLabel);

    // --- Transporte ---
    addAndMakeVisible (playButton);
    playButton.onClick = [this] { proc.toggleAudition(); };

    addAndMakeVisible (polyButton);
    polyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "poly", polyButton);

    addAndMakeVisible (holdButton);
    holdAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "hold", holdButton);

    addAndMakeVisible (gridButton);
    addChildComponent (saveGridButton);
    addChildComponent (cancelGridButton);
    gridButton.onClick = [this] { wave.setGridMode (true); };
    saveGridButton.onClick = [this] { wave.commitGrid(); syncPadControls(); };
    cancelGridButton.onClick = [this] { wave.setGridMode (false); };

    hintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    hintLabel.setFont (juce::Font (12.0f));
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (autoButton);
    autoButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Slicer (por division)");
        m.addItem (2, "Transientes");
        m.addItem (3, "Samples");
        m.addItem (4, "Random");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&autoButton),
                         [this] (int result)
                         {
                             if (result > 0)
                                 doSlicing (result - 1);
                         });
    };

    divisionBox.addItemList (juce::StringArray { "1/4 negra", "1/2 negra", "1 negra",
                                                 "1/2 compas", "1 compas", "2 compases" }, 1);
    addAndMakeVisible (divisionBox);
    divAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "division", divisionBox);
    divisionBox.onChange = [this] { needReslice = true; };

    addAndMakeVisible (selectAllButton);
    addAndMakeVisible (clearButton);
    selectAllButton.onClick = [this]
    {
        if (selMask == 0xFFFFu)
            selMask = 1u << curPad;
        else
            selMask = 0xFFFFu;
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

    // --- Faders del pad seleccionado ---
    setupFader (0, "Nivel", 0.0, 2.0, 0.01, 1.0,
                [] (double v) -> juce::String { return juce::String (juce::roundToInt (v * 100.0)) + "%"; },
                [] (PadSlice& s, float v) { s.gain = v; },
                [] (const PadSlice& s) { return s.gain; });

    setupFader (1, "Filtro", -1.0, 1.0, 0.01, 0.0,
                [] (double v) -> juce::String
                {
                    if (std::abs (v) < 0.02) return "off";
                    return (v < 0.0 ? juce::String ("LP ") : juce::String ("HP "))
                           + juce::String (juce::roundToInt (std::abs (v) * 100.0));
                },
                [] (PadSlice& s, float v) { s.filter = v; },
                [] (const PadSlice& s) { return s.filter; });

    setupFader (2, "Ataque", 0.0, 2.0, 0.001, 0.0,
                [] (double v) -> juce::String
                {
                    if (v < 1.0) return juce::String (juce::roundToInt (v * 1000.0)) + " ms";
                    return juce::String (v, 2) + " s";
                },
                [] (PadSlice& s, float v) { s.attack = v; },
                [] (const PadSlice& s) { return s.attack; });
    faders[2].slider.setSkewFactorFromMidPoint (0.2);

    setupFader (3, "Release", 0.003, 3.0, 0.001, 0.02,
                [] (double v) -> juce::String
                {
                    if (v < 1.0) return juce::String (juce::roundToInt (v * 1000.0)) + " ms";
                    return juce::String (v, 2) + " s";
                },
                [] (PadSlice& s, float v) { s.release = v; },
                [] (const PadSlice& s) { return s.release; });
    faders[3].slider.setSkewFactorFromMidPoint (0.3);

    setupFader (4, "Velocidad", -50.0, 100.0, 1.0, 0.0,
                [] (double v) -> juce::String
                {
                    return juce::String (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v)) + "%";
                },
                [] (PadSlice& s, float v) { s.stretch = v; },
                [] (const PadSlice& s) { return s.stretch; });

    setupFader (5, "Tono", -12.0, 12.0, 1.0, 0.0,
                [] (double v) -> juce::String
                {
                    return juce::String (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
                },
                [] (PadSlice& s, float v) { s.keyShift = juce::roundToInt (v); },
                [] (const PadSlice& s) { return (float) s.keyShift; });

    setupFader (6, "Fino", -100.0, 100.0, 1.0, 0.0,
                [] (double v) -> juce::String
                {
                    return juce::String (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
                },
                [] (PadSlice& s, float v) { s.detune = v; },
                [] (const PadSlice& s) { return s.detune; });

    masterLabel.setText ("Master", juce::dontSendNotification);
    masterLabel.setJustificationType (juce::Justification::centred);
    masterLabel.setFont (juce::Font (12.0f));
    masterLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (masterLabel);

    masterSlider.setSliderStyle (juce::Slider::LinearVertical);
    masterSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    masterSlider.setTextBoxIsEditable (false);
    addAndMakeVisible (masterSlider);
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "volume", masterSlider);
    masterSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    masterSlider.updateText();

    reverseButton.setClickingTogglesState (true);
    reverseButton.onClick = [this]
    {
        if (updatingControls)
            return;
        const bool r = reverseButton.getToggleState();
        applyToSelected ([r] (PadSlice& s) { s.reverse = r; });
    };
    addAndMakeVisible (reverseButton);

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
}

void TongaEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    // Encabezado
    auto head = r.removeFromTop (44);
    logo.setBounds (head.removeFromLeft (44).reduced (2));
    head.removeFromLeft (8);
    auto tl = head.removeFromLeft (230);
    title.setBounds (tl.removeFromTop (26));
    sampleLabel.setBounds (tl);
    loadButton.setBounds (head.removeFromRight (96).reduced (0, 8));
    head.removeFromRight (8);
    timeSigBox.setBounds (head.removeFromRight (70).reduced (0, 8));
    head.removeFromRight (8);
    syncButton.setBounds (head.removeFromRight (64).reduced (0, 8));
    head.removeFromRight (8);
    bpmSlider.setBounds (head.removeFromRight (170).reduced (0, 10));
    head.removeFromRight (8);
    bpmLabel.setBounds (head.removeFromRight (220));

    // Transporte
    r.removeFromTop (6);
    auto tr = r.removeFromTop (36);
    playButton.setBounds (tr.removeFromLeft (36));
    tr.removeFromLeft (8);
    polyButton.setBounds (tr.removeFromLeft (64).reduced (0, 4));
    tr.removeFromLeft (6);
    holdButton.setBounds (tr.removeFromLeft (90).reduced (0, 4));
    tr.removeFromLeft (14);

    auto gb = tr.removeFromLeft (172);
    gridButton.setBounds (gb.removeFromLeft (110).reduced (0, 4));
    saveGridButton.setBounds (gridButton.getBounds().withWidth (80));
    cancelGridButton.setBounds (saveGridButton.getBounds().translated (86, 0));

    clearButton.setBounds (tr.removeFromRight (70).reduced (0, 4));
    tr.removeFromRight (6);
    selectAllButton.setBounds (tr.removeFromRight (70).reduced (0, 4));
    tr.removeFromRight (14);
    divisionBox.setBounds (tr.removeFromRight (110).reduced (0, 4));
    tr.removeFromRight (6);
    autoButton.setBounds (tr.removeFromRight (70).reduced (0, 4));
    hintLabel.setBounds (tr);

    // Onda
    r.removeFromTop (6);
    wave.setBounds (r.removeFromTop (210));

    // Faders
    r.removeFromTop (8);
    auto fr = r.removeFromTop (150);
    for (int i = 0; i < 7; ++i)
    {
        auto col = fr.removeFromLeft (100);
        faders[(size_t) i].name.setBounds (col.removeFromTop (16));
        faders[(size_t) i].slider.setBounds (col);
    }
    {
        auto col = fr.removeFromLeft (100);
        masterLabel.setBounds (col.removeFromTop (16));
        masterSlider.setBounds (col);
    }
    {
        auto rest = fr.reduced (16, 0);
        rest.removeFromTop (18);
        reverseButton.setBounds (rest.removeFromTop (30));
    }

    // Pads: 8 columnas x 2 filas, el 1 abajo a la izquierda
    r.removeFromTop (8);
    const int cw = r.getWidth() / 8;
    const int ch = r.getHeight() / 2;
    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        const int col = i % 8;
        const int row = 1 - i / 8;
        pads[i].setBounds (r.getX() + col * cw, r.getY() + row * ch, cw, ch);
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

void TongaEditor::timerCallback()
{
    // Re-cortar si se cambio la division (solo en modo Slicer)
    if (needReslice)
    {
        needReslice = false;
        if (proc.getLastMode() == TongaProcessor::modeSlicer && proc.getValidMask() != 0)
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

    const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    bpmSlider.setVisible (! sync);

    const bool poly = proc.apvts.getRawParameterValue ("poly")->load() > 0.5f;
    polyButton.setButtonText (poly ? "Poly" : "Mono");

    bpmLabel.setText (juce::String (proc.getAppliedBpm(), 1) + " BPM   |   original " + juce::String (proc.getOriginalBpm(), 1),
                      juce::dontSendNotification);

    sampleLabel.setText (proc.hasSample() ? proc.getSampleName() : juce::String ("Sin audio"),
                         juce::dontSendNotification);

    playButton.playing = proc.isAuditioning();
    playButton.repaint();

    const bool g = wave.isGridMode();
    gridButton.setVisible (! g);
    saveGridButton.setVisible (g);
    cancelGridButton.setVisible (g);

    juce::String hint;
    if (g)
        hint = "A y B = 2 compases seguidos";
    else if (proc.hasSample() && proc.getValidMask() == 0)
        hint = "Pad vacio = asigna en el cursor";
    hintLabel.setText (hint, juce::dontSendNotification);

    refreshPadStates();
    wave.repaint();
}
