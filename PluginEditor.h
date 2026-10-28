#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>
#include <functional>

namespace TongaColours
{
    inline const juce::Colour yellow { 0xffffd200 };
    inline const juce::Colour pink   { 0xfff98aa0 };
    inline const juce::Colour bgDark { 0xff121217 };
    inline const juce::Colour panel  { 0xff1b1b22 };
    inline const juce::Colour button { 0xff25252d };
}

//==============================================================================
// Estilo minimalista: botones planos, faders finos, todo en amarillo / gris
class TongaLook : public juce::LookAndFeel_V4
{
public:
    TongaLook()
    {
        using namespace TongaColours;
        setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        setColour (juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonColourId, button);
        setColour (juce::TextButton::buttonOnColourId, yellow);
        setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::ComboBox::backgroundColourId, button);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::textColourId, juce::Colours::white);
        setColour (juce::ComboBox::arrowColourId, juce::Colours::grey);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, yellow);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        auto area = b.getLocalBounds().toFloat().reduced (0.5f);
        juce::Colour c = b.getToggleState() ? TongaColours::yellow : TongaColours::button;
        if (! b.isEnabled()) c = c.withAlpha (0.4f);
        if (over) c = c.brighter (0.12f);
        if (down) c = c.brighter (0.25f);
        g.setColour (c);
        g.fillRoundedRectangle (area, 6.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           const juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        const bool vertical = (style == juce::Slider::LinearVertical);
        const double base = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) ? 0.0 : s.getMinimum();
        const float basePos = (float) s.getPositionOfValue (base);
        const juce::Colour track (0xff2a2a33);
        const juce::Colour accent = s.isEnabled() ? TongaColours::yellow : juce::Colour (0xff6b6b3a);

        if (vertical)
        {
            const float cx = (float) x + (float) width * 0.5f;
            g.setColour (track);
            g.fillRoundedRectangle (cx - 2.0f, (float) y, 4.0f, (float) height, 2.0f);
            g.setColour (accent.withAlpha (0.8f));
            g.fillRoundedRectangle (cx - 2.0f, juce::jmin (sliderPos, basePos), 4.0f,
                                    std::abs (basePos - sliderPos), 2.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (cx - 10.0f, sliderPos - 3.0f, 20.0f, 6.0f, 3.0f);
        }
        else
        {
            const float cy = (float) y + (float) height * 0.5f;
            g.setColour (track);
            g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) width, 4.0f, 2.0f);
            g.setColour (accent.withAlpha (0.8f));
            g.fillRoundedRectangle (juce::jmin (sliderPos, basePos), cy - 2.0f,
                                    std::abs (basePos - sliderPos), 4.0f, 2.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (sliderPos - 3.0f, cy - 10.0f, 6.0f, 20.0f, 3.0f);
        }
    }
};

//==============================================================================
// Boton de play / pausa dibujado a mano
class PlayButton : public juce::Button
{
public:
    PlayButton() : juce::Button ("play") {}
    bool playing = false;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (over ? juce::Colour (0xff34343f) : TongaColours::button);
        g.fillEllipse (b);
        g.setColour (TongaColours::yellow);
        const auto c = b.getCentre();
        if (playing)
        {
            g.fillRect (c.x - 6.0f, c.y - 7.0f, 4.0f, 14.0f);
            g.fillRect (c.x + 2.0f, c.y - 7.0f, 4.0f, 14.0f);
        }
        else
        {
            juce::Path p;
            p.addTriangle (c.x - 5.0f, c.y - 8.0f, c.x - 5.0f, c.y + 8.0f, c.x + 8.0f, c.y);
            g.fillPath (p);
        }
    }
};

//==============================================================================
// Vista de la onda: click = poner el cursor, arrastrar = mover, rueda = zoom.
// Se pueden arrastrar los bordes del pad seleccionado y, en modo grilla, las barras A y B.
class WaveView : public juce::Component
{
public:
    explicit WaveView (TongaProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setSelectedPad (int pad) { selectedPad = pad; repaint(); }
    void showPad (int pad);
    void resetView();

    void setGridMode (bool on);
    bool isGridMode() const { return gridMode; }
    void commitGrid();

private:
    enum DragKind { dragNone, dragScroll, dragGridA, dragGridB, dragPadStart, dragPadEnd };

    double xToSec (float x) const;
    float secToX (double s) const;
    void clampView();

    TongaProcessor& proc;
    double viewStart = 0.0, viewLen = 10.0;
    int selectedPad = 0;

    bool gridMode = false;
    double gridA = 0.0, gridB = 1.0;

    DragKind drag = dragNone;
    double dragStartView = 0.0;

    std::vector<float> mins, maxs;
};

//==============================================================================
class PadComp : public juce::Component
{
public:
    int index = 0;
    bool selected = false, playing = false, hasSlice = false;
    std::function<void (int, const juce::ModifierKeys&)> onDown;
    std::function<void (int)> onUp;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
};

//==============================================================================
class TongaEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    explicit TongaEditor (TongaProcessor&);
    ~TongaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    struct Fader
    {
        juce::Label name;
        juce::Slider slider;
        std::function<void (PadSlice&, float)> apply;
        std::function<float (const PadSlice&)> read;
    };

    void setupFader (int i, const juce::String& label, double mn, double mx, double step, double def,
                     std::function<juce::String (double)> text,
                     std::function<void (PadSlice&, float)> apply,
                     std::function<float (const PadSlice&)> read);

    void timerCallback() override;
    void loadSample (const juce::File& f);
    void padClicked (int pad, const juce::ModifierKeys& mods);
    void refreshPadStates();
    void syncPadControls();
    void doSlicing (int mode);
    void applyToSelected (const std::function<void (PadSlice&)>& fn);

    TongaLook look;
    TongaProcessor& proc;

    WaveView wave;
    juce::ImageComponent logo;
    juce::Label title, sampleLabel, bpmLabel, hintLabel, masterLabel;
    juce::TextButton loadButton { "Cargar audio" };
    juce::TextButton syncButton { "Sync" };
    juce::ComboBox timeSigBox;
    juce::Slider bpmSlider;

    PlayButton playButton;
    juce::TextButton polyButton { "Poly" };
    juce::TextButton holdButton { "Mantener" };
    juce::TextButton gridButton { "Grilla" }, saveGridButton { "Guardar" }, cancelGridButton { "Cancelar" };
    juce::TextButton autoButton { "Auto" };
    juce::ComboBox divisionBox;
    juce::TextButton selectAllButton { "Todos" }, clearButton { "Limpiar" };

    std::array<Fader, 7> faders;
    juce::Slider masterSlider;
    juce::TextButton reverseButton { "Reversa" };

    PadComp pads[TongaProcessor::numPads];

    int curPad = 0;
    uint32_t selMask = 1u;
    int lastCounter = 0;
    bool updatingControls = false;
    bool needReslice = false;

    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt, polyAtt, holdAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sigAtt, divAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt, masterAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TongaEditor)
};
