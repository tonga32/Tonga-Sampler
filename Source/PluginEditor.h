#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <functional>

//==============================================================================
// Vista de la onda: arrastrar = mover, rueda = zoom, se pueden mover los bordes del pad
// y (en modo grilla) las dos barras de compas.
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
    double lastDragSec = 0.0;

    std::vector<float> mins, maxs;
};

//==============================================================================
class PadComp : public juce::Component
{
public:
    int index = 0;
    bool selected = false, playing = false, hasSlice = false;
    std::function<void (int, const juce::ModifierKeys&)> onDown;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
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
    void timerCallback() override;
    void loadSample (const juce::File& f);
    void padClicked (int pad, const juce::ModifierKeys& mods);
    void refreshPadStates();
    void syncPadControls();
    void doSlicing (int mode);
    void applyToSelected (const std::function<void (PadSlice&)>& fn);

    TongaProcessor& proc;

    WaveView wave;
    juce::ImageComponent logo;
    juce::Label title, sampleLabel, bpmLabel, sizeLabel, padGainLabel, volLabel, hintLabel;
    juce::TextButton loadButton { "Cargar audio" };
    juce::ToggleButton syncToggle { "Sync DAW" };
    juce::ComboBox timeSigBox;
    juce::Slider bpmSlider, sizeSlider, volSlider, padGainSlider;
    juce::ToggleButton reverseToggle { "Reversa" };

    juce::TextButton gridButton { "Editar grilla" }, saveGridButton { "Guardar" }, cancelGridButton { "Cancelar" };
    juce::TextButton modeButtons[4];
    juce::TextButton selectAllButton { "Seleccionar todos" }, clearButton { "Limpiar" };

    PadComp pads[TongaProcessor::numPads];

    int curPad = 0;
    uint32_t selMask = 1u;
    int lastCounter = 0;
    bool updatingControls = false;
    bool needReslice = false;

    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sigAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt, sizeAtt, volAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TongaEditor)
};
