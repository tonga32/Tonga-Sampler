#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>
#include <functional>

namespace TongaColours
{
    inline const juce::Colour yellow { 0xffffd200 };
    inline const juce::Colour pink   { 0xfff98aa0 };
    inline const juce::Colour bgDark { 0xff17171c };
    inline const juce::Colour panel  { 0xff1f1f26 };
    inline const juce::Colour button { 0xff2a2a33 };
    inline const juce::Colour inset  { 0xff0b0b0f };

    // Cada pad tiene un color de la marca (del amarillo al rosa)
    inline juce::Colour padColour (int i)
    {
        return yellow.interpolatedWith (pink, (float) i / (float) (TongaProcessor::numPads - 1));
    }
}

//==============================================================================
// Estilo minimalista de Tonga: planos, perillas finas, amarillo y rosa
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
        setColour (juce::ComboBox::arrowColourId, yellow);
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
        g.fillRoundedRectangle (area, 5.0f);
    }

    // Perilla: arco gris de fondo, arco amarillo del valor y una rayita indicadora
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const juce::Colour accent = s.isEnabled() ? TongaColours::yellow : juce::Colour (0xff6b6b3a);

        g.setColour (TongaColours::inset);
        g.fillEllipse (cx - radius * 0.78f, cy - radius * 0.78f, radius * 1.56f, radius * 1.56f);

        juce::Path track;
        track.addCentredArc (cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff3a3a46));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const double base = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) ? 0.0 : s.getMinimum();
        const float baseAngle = startAngle + (float) s.valueToProportionOfLength (base) * (endAngle - startAngle);
        if (std::abs (angle - baseAngle) > 0.001f)
        {
            juce::Path val;
            val.addCentredArc (cx, cy, radius, radius, 0.0f,
                               juce::jmin (angle, baseAngle), juce::jmax (angle, baseAngle), true);
            g.setColour (accent);
            g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -radius * 0.72f, 3.0f, radius * 0.42f, 1.5f);
        g.setColour (accent);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (cx, cy));
    }

    // Flechitas chicas para los controles tipo +/- (como en Serato)
    juce::Button* createSliderButton (juce::Slider&, bool isIncrement) override
    {
        return new juce::ArrowButton (isIncrement ? "up" : "down", isIncrement ? 0.75f : 0.25f, TongaColours::pink);
    }
};

//==============================================================================
// Boton de play / pausa (caja oscura con triangulo, como Serato)
class PlayButton : public juce::Button
{
public:
    PlayButton() : juce::Button ("play") {}
    bool playing = false;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (over ? juce::Colour (0xff15151b) : TongaColours::inset);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (TongaColours::yellow);
        const auto c = b.getCentre();
        if (playing)
        {
            g.fillRect (c.x - 7.0f, c.y - 8.0f, 5.0f, 16.0f);
            g.fillRect (c.x + 2.0f, c.y - 8.0f, 5.0f, 16.0f);
        }
        else
        {
            juce::Path p;
            p.addTriangle (c.x - 6.0f, c.y - 9.0f, c.x - 6.0f, c.y + 9.0f, c.x + 9.0f, c.y);
            g.fillPath (p);
        }
    }
};

// Boton "Reversa" con triangulo hacia la izquierda
class ReverseButton : public juce::Button
{
public:
    ReverseButton() : juce::Button ("reverse") { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto b = getLocalBounds().toFloat();
        const bool on = getToggleState();
        const juce::Colour c = on ? TongaColours::yellow
                                  : (over ? juce::Colours::white : juce::Colour (0xff8a8a96));
        const auto centre = juce::Point<float> (b.getCentreX(), b.getCentreY() - 8.0f);
        juce::Path p;
        p.addTriangle (centre.x + 10.0f, centre.y - 12.0f, centre.x + 10.0f, centre.y + 12.0f,
                       centre.x - 12.0f, centre.y);
        g.setColour (c);
        g.fillPath (p);
        g.setFont (12.0f);
        g.drawText ("Reversa", getLocalBounds().removeFromBottom (18), juce::Justification::centred);
    }
};

// Dos opciones apiladas, la activa en amarillo (Mono/Poly y Mantener/Una vez)
class StackedToggle : public juce::Component
{
public:
    using DrawFn = std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)>;
    DrawFn drawTop, drawBottom;
    std::function<void (bool)> onChange;   // true = quedo activa la parte de abajo
    bool bottomActive = false;

    void setBottomActive (bool b)
    {
        if (b != bottomActive)
        {
            bottomActive = b;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (TongaColours::inset);
        g.fillRoundedRectangle (b, 6.0f);

        auto top = b.removeFromTop (b.getHeight() * 0.5f);
        auto bot = b;
        const juce::Colour on = TongaColours::yellow;
        const juce::Colour off (0xff6c6c78);
        if (drawTop)    drawTop (g, top.reduced (4.0f, 2.0f), bottomActive ? off : on);
        if (drawBottom) drawBottom (g, bot.reduced (4.0f, 2.0f), bottomActive ? on : off);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const bool bottom = e.y >= getHeight() / 2;
        if (bottom != bottomActive)
        {
            bottomActive = bottom;
            repaint();
            if (onChange)
                onChange (bottom);
        }
    }
};

//==============================================================================
// Vista de la onda: click = poner el cursor, arrastrar = mover, rueda = zoom.
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
    void showTime (double sec);
    void resetView();

    double getViewStart() const { return viewStart; }
    double getViewLen() const { return viewLen; }
    void setViewStart (double s);

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

// Franja con todo el audio y un recuadro de la zona que se ve abajo
class OverviewView : public juce::Component
{
public:
    OverviewView (TongaProcessor& p, WaveView& w) : proc (p), wave (w) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { moveTo (e); }
    void mouseDrag (const juce::MouseEvent& e) override { moveTo (e); }

private:
    void moveTo (const juce::MouseEvent& e);

    TongaProcessor& proc;
    WaveView& wave;
    juce::String cacheKey;
    std::vector<float> mins, maxs;
};

//==============================================================================
// Panel de volumenes de los stems (aparece sobre la onda cuando ya estan separados)
class StemPanel : public juce::Component
{
public:
    explicit StemPanel (juce::AudioProcessorValueTreeState& apvts);

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onRemove;

private:
    struct Knob
    {
        juce::Label name;
        juce::Slider slider;
    };

    std::array<Knob, 4> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> atts;
    juce::TextButton removeButton { "Sin stems" };
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
    // Un control de ajuste por pad: perilla o caja con flechitas
    struct Fader
    {
        juce::Label name;
        juce::Slider slider;
        std::function<void (PadSlice&, float)> apply;
        std::function<float (const PadSlice&)> read;
    };

    void setupFader (int i, const juce::String& label, bool knob, double mn, double mx, double step, double def,
                     std::function<juce::String (double)> text,
                     std::function<void (PadSlice&, float)> apply,
                     std::function<float (const PadSlice&)> read);
    void styleIncDec (juce::Slider& s, int textW);

    void timerCallback() override;
    void loadSample (const juce::File& f);
    void padClicked (int pad, const juce::ModifierKeys& mods);
    void refreshPadStates();
    void syncPadControls();
    void doSlicing (int mode);
    void applyToSelected (const std::function<void (PadSlice&)>& fn);
    void scaleStretch (double factor);
    void scaleOriginalBpm (double factor);
    void stepDivision (int delta);
    void shiftSlicer (int dir);

    TongaLook look;
    TongaProcessor& proc;

    WaveView wave;
    OverviewView overview;

    // Encabezado
    juce::ImageComponent logo;
    juce::Label title;
    juce::TextButton loadButton { "Cargar audio" };

    // Franja de informacion
    juce::Rectangle<int> infoStrip, knobPanel;
    juce::Label sampleLabel, bpmLabel, sigCaption, timeLabel;
    juce::TextButton bpmHalf { "1/2" }, bpmDouble { "x2" };
    juce::ComboBox timeSigBox;

    // Fila de control
    PlayButton playButton;
    StackedToggle holdToggle, polyToggle;
    juce::Slider bpmBox, keyBox, detuneBox;
    juce::Label keyLabel, detuneLabel;
    juce::TextButton syncButton { "SYNC" };
    juce::TextButton stemsButton { "Separar stems" };
    StemPanel stemPanel { proc.apvts };

    // Perillas y cajas del pad seleccionado
    std::array<Fader, 6> faders;       // 0-3 perillas, 4 velocidad, 5 tono del pad
    ReverseButton reverseButton;
    juce::TextButton stretchHalf { "1/2" }, stretchDouble { "x2" };

    // Fila de abajo
    juce::ComboBox modeBox;
    juce::ArrowButton pageLeft { "pl", 0.5f, TongaColours::pink };
    juce::ArrowButton pageRight { "pr", 0.0f, TongaColours::pink };
    juce::ArrowButton divDec { "dd", 0.5f, TongaColours::yellow };
    juce::ArrowButton divInc { "di", 0.0f, TongaColours::yellow };
    juce::Label divLabel, hintLabel;
    juce::TextButton rerollButton { "Otra vez" };
    juce::TextButton selectAllButton { "Seleccionar todos" }, clearButton { "Limpiar" };
    juce::TextButton gridButton { "Grilla" }, saveGridButton { "Guardar" }, cancelGridButton { "Cancelar" };

    PadComp pads[TongaProcessor::numPads];

    int curPad = 0;
    uint32_t selMask = 1u;
    int lastCounter = 0;
    bool updatingControls = false;
    bool needReslice = false;
    bool stemPanelOpen = false;
    bool hadStems = false;
    bool wasBusy = false;

    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sigAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt, keyAtt, detuneAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TongaEditor)
};
