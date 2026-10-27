#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    const juce::Colour yellow (0xffffd200);
    const juce::Colour pink   (0xfff98aa0);
    const juce::Colour bgDark (0xff17171c);
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

    if (s.start < viewStart || s.end > viewStart + viewLen)
        viewStart = (s.start + s.end) * 0.5 - viewLen * 0.5;

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
    g.fillAll (juce::Colour (0xff101015));

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

    // Bandas de los pads
    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        const auto s = proc.getSlice (i);
        if (! s.valid)
            continue;

        const float x0 = secToX (s.start);
        const float x1 = secToX (s.end);
        if (x1 < 0.0f || x0 > (float) w)
            continue;

        g.setColour (i == selectedPad ? yellow.withAlpha (0.20f) : pink.withAlpha (0.08f));
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
                    g.setColour (juce::Colours::white.withAlpha (0.10f));
                    for (int j = 1; (double) j * beatS < barS - 1.0e-6; ++j)
                    {
                        const double t = tBar + (double) j * beatS;
                        if (t >= 0.0 && t >= viewStart && t <= viewStart + viewLen)
                            g.drawVerticalLine ((int) secToX (t), 0.0f, (float) h);
                    }
                }

                if (tBar >= 0.0 && tBar >= viewStart)
                {
                    g.setColour (yellow.withAlpha (0.45f));
                    g.drawVerticalLine ((int) secToX (tBar), 0.0f, (float) h);
                }
            }
        }
    }

    // Modo grilla: manijas A y B
    if (gridMode)
    {
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
        // Bordes del pad seleccionado
        const auto s = proc.getSlice (selectedPad);
        if (s.valid)
        {
            g.setColour (yellow);
            g.fillRect (secToX (s.start) - 1.0f, 0.0f, 2.0f, (float) h);
            g.fillRect (secToX (s.end) - 1.0f, 0.0f, 2.0f, (float) h);
        }
    }

    // Cabezal de reproduccion
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
    lastDragSec = xToSec (x);
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
            const double newA = juce::jmax (0.0, gridA + (t - gridA));
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

void WaveView::mouseUp (const juce::MouseEvent&)
{
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

    juce::Colour base = hasSlice ? juce::Colour (0xff2b2b35) : juce::Colour (0xff1d1d24);
    if (playing)
        base = pink;

    g.setColour (base);
    g.fillRoundedRectangle (b, 8.0f);

    g.setColour (selected ? yellow : juce::Colour (0xff3a3a46));
    g.drawRoundedRectangle (b, 8.0f, selected ? 2.5f : 1.0f);

    g.setColour (playing ? juce::Colours::black : (hasSlice ? juce::Colours::white : juce::Colours::grey));
    g.setFont (juce::Font (16.0f, juce::Font::bold));
    g.drawText (juce::String (index + 1), getLocalBounds(), juce::Justification::centred);
}

void PadComp::mouseDown (const juce::MouseEvent& e)
{
    if (onDown)
        onDown (index, e.mods);
}

//==============================================================================
// TongaEditor
//==============================================================================
TongaEditor::TongaEditor (TongaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wave (p)
{
    setSize (960, 640);

    addAndMakeVisible (wave);

    logo.setImage (juce::ImageCache::getFromMemory (BinaryData::TONGA_SAMPLER_png,
                                                    BinaryData::TONGA_SAMPLER_pngSize));
    logo.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (logo);

    title.setText ("Tonga Sampler 2", juce::dontSendNotification);
    title.setFont (juce::Font (26.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, yellow);
    addAndMakeVisible (title);

    sampleLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (sampleLabel);

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

    addAndMakeVisible (syncToggle);
    syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "sync", syncToggle);

    timeSigBox.addItemList (juce::StringArray { "2/4", "3/4", "4/4", "6/8", "9/8", "12/8" }, 1);
    addAndMakeVisible (timeSigBox);
    sigAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "timesig", timeSigBox);

    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    addAndMakeVisible (bpmSlider);
    bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "bpm", bpmSlider);

    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setColour (juce::Label::textColourId, yellow);
    addAndMakeVisible (bpmLabel);

    hintLabel.setColour (juce::Label::textColourId, pink);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (gridButton);
    addChildComponent (saveGridButton);
    addChildComponent (cancelGridButton);
    gridButton.onClick = [this] { wave.setGridMode (true); };
    saveGridButton.onClick = [this] { wave.commitGrid(); syncPadControls(); };
    cancelGridButton.onClick = [this] { wave.setGridMode (false); };

    const char* modeNames[4] = { "Slicer", "Transientes", "Samples", "Random" };
    for (int i = 0; i < 4; ++i)
    {
        modeButtons[i].setButtonText (modeNames[i]);
        modeButtons[i].setClickingTogglesState (false);
        modeButtons[i].setColour (juce::TextButton::buttonOnColourId, yellow);
        modeButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        modeButtons[i].onClick = [this, i] { doSlicing (i); };
        addAndMakeVisible (modeButtons[i]);
    }

    sizeLabel.setText ("Tamano corte", juce::dontSendNotification);
    addAndMakeVisible (sizeLabel);
    sizeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sizeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible (sizeSlider);
    sizeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "size", sizeSlider);
    sizeSlider.onValueChange = [this] { needReslice = true; };

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
        wave.repaint();
    };

    padGainLabel.setText ("Vol. pad", juce::dontSendNotification);
    addAndMakeVisible (padGainLabel);
    padGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    padGainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    padGainSlider.setRange (0.0, 2.0, 0.01);
    padGainSlider.setValue (1.0, juce::dontSendNotification);
    padGainSlider.onValueChange = [this]
    {
        if (updatingControls) return;
        const float v = (float) padGainSlider.getValue();
        applyToSelected ([v] (PadSlice& s) { s.gain = v; });
    };
    addAndMakeVisible (padGainSlider);

    reverseToggle.onClick = [this]
    {
        if (updatingControls) return;
        const bool r = reverseToggle.getToggleState();
        applyToSelected ([r] (PadSlice& s) { s.reverse = r; });
    };
    addAndMakeVisible (reverseToggle);

    volLabel.setText ("Master", juce::dontSendNotification);
    addAndMakeVisible (volLabel);
    volSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    addAndMakeVisible (volSlider);
    volAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "volume", volSlider);

    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        pads[i].index = i;
        pads[i].onDown = [this] (int pad, const juce::ModifierKeys& m) { padClicked (pad, m); };
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
}

void TongaEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgDark);
}

void TongaEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto head = r.removeFromTop (64);
    logo.setBounds (head.removeFromLeft (64));
    head.removeFromLeft (10);
    auto left = head.removeFromLeft (250);
    title.setBounds (left.removeFromTop (36));
    sampleLabel.setBounds (left);
    loadButton.setBounds (head.removeFromLeft (100).reduced (0, 16));
    head.removeFromLeft (8);
    syncToggle.setBounds (head.removeFromLeft (100).reduced (0, 16));
    head.removeFromLeft (8);
    timeSigBox.setBounds (head.removeFromLeft (80).reduced (0, 16));
    head.removeFromLeft (8);
    bpmSlider.setBounds (head.reduced (0, 16));

    r.removeFromTop (8);
    wave.setBounds (r.removeFromTop (260));
    r.removeFromTop (6);

    auto gridRow = r.removeFromTop (30);
    bpmLabel.setBounds (gridRow.removeFromRight (360));
    gridButton.setBounds (gridRow.removeFromLeft (120));
    saveGridButton.setBounds (gridButton.getBounds());
    cancelGridButton.setBounds (gridButton.getBounds().translated (128, 0).withWidth (100));
    saveGridButton.setBounds (gridButton.getBounds().withWidth (100));
    gridRow.removeFromLeft (112);
    hintLabel.setBounds (gridRow);

    r.removeFromTop (8);

    auto padArea = r.removeFromLeft (400);
    const int cw = padArea.getWidth() / 4;
    const int ch = padArea.getHeight() / 4;
    for (int i = 0; i < TongaProcessor::numPads; ++i)
    {
        const int col = i % 4;
        const int row = 3 - i / 4;
        pads[i].setBounds (padArea.getX() + col * cw, padArea.getY() + row * ch, cw, ch);
    }

    r.removeFromLeft (16);
    const int rowH = 36;

    auto row1 = r.removeFromTop (rowH).reduced (0, 2);
    const int bw = row1.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        modeButtons[i].setBounds (row1.removeFromLeft (bw).reduced (2, 0));

    auto row2 = r.removeFromTop (rowH);
    sizeLabel.setBounds (row2.removeFromLeft (100));
    sizeSlider.setBounds (row2);

    auto row3 = r.removeFromTop (rowH).reduced (0, 2);
    selectAllButton.setBounds (row3.removeFromLeft (row3.getWidth() / 2).reduced (2, 0));
    clearButton.setBounds (row3.reduced (2, 0));

    auto row4 = r.removeFromTop (rowH);
    padGainLabel.setBounds (row4.removeFromLeft (100));
    reverseToggle.setBounds (row4.removeFromRight (90));
    padGainSlider.setBounds (row4);

    auto row5 = r.removeFromTop (rowH);
    volLabel.setBounds (row5.removeFromLeft (100));
    volSlider.setBounds (row5);
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
    proc.triggerPad (pad);
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
    padGainSlider.setValue (s.gain, juce::dontSendNotification);
    reverseToggle.setToggleState (s.reverse, juce::dontSendNotification);
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
            if (s.valid)
            {
                fn (s);
                proc.setSlice (i, s);
            }
        }
    }
}

void TongaEditor::timerCallback()
{
    // Re-cortar si se movio el control de tamano (en modo Slicer)
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

    const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    bpmSlider.setEnabled (! sync);

    bpmLabel.setText ("BPM aplicado: " + juce::String (proc.getAppliedBpm(), 1)
                      + "   |   BPM original del audio: " + juce::String (proc.getOriginalBpm(), 1),
                      juce::dontSendNotification);

    sampleLabel.setText (proc.hasSample() ? proc.getSampleName() : juce::String ("Sin audio"),
                         juce::dontSendNotification);

    const bool g = wave.isGridMode();
    gridButton.setVisible (! g);
    saveGridButton.setVisible (g);
    cancelGridButton.setVisible (g);
    hintLabel.setText (g ? "Mueve A y B: son el inicio de 2 compases seguidos" : "",
                       juce::dontSendNotification);

    for (int i = 0; i < 4; ++i)
        modeButtons[i].setToggleState (proc.getLastMode() == i, juce::dontSendNotification);

    refreshPadStates();
    wave.repaint();
}
