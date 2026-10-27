#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

// Un corte (slice) asignado a un pad. Las posiciones estan en segundos del audio original.
struct PadSlice
{
    bool valid = false;
    double start = 0.0;
    double end = 0.0;
    float gain = 1.0f;
    bool reverse = false;
};

class TongaProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numPads = 16;
    enum SliceMode { modeSlicer = 0, modeTransient = 1, modeSamples = 2, modeRandom = 3 };

    TongaProcessor();
    ~TongaProcessor() override = default;

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tonga Sampler 2"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Audio cargado ---
    bool loadFile (const juce::File& file, bool detectGrid = true);
    bool hasSample() const { return sampleLengthSec.load() > 0.0; }
    double getLengthSec() const { return sampleLengthSec.load(); }
    juce::String getSampleName() const;
    void getPeaks (double startSec, double lenSec, int columns,
                   std::vector<float>& mins, std::vector<float>& maxs);

    // --- Grilla / tempo ---
    double getOriginalBpm() const { return originalBpm.load(); }
    double getGridOffset() const { return gridOffset.load(); }
    double getQuartersPerBar() const;
    double getBarSeconds() const;
    double getAppliedBpm() const { return appliedBpm.load(); }
    void setGrid (double offsetSec, double newOriginalBpm);

    // --- Pads ---
    PadSlice getSlice (int pad);
    void setSlice (int pad, const PadSlice& s);
    void clearPad (int pad);
    uint32_t getValidMask();
    void applySlicing (int mode);
    int getLastMode() const { return lastMode.load(); }

    void triggerPad (int pad);

    // Informacion para la interfaz
    std::atomic<int> lastPlayedPad { 0 };
    std::atomic<int> playCounter { 0 };
    std::atomic<uint32_t> playingMask { 0 };
    std::atomic<double> lastPlayPosSec { -1.0 };

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Voice
    {
        bool active = false;
        double pos = 0.0;
        double startS = 0.0;
        double endS = 0.0;
        float gain = 1.0f;
        bool rev = false;
        int delay = 0;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void detectTempo();
    void startVoice (int pad, int delaySamples);

    juce::AudioFormatManager formatManager;
    juce::CriticalSection dataLock;

    juce::AudioBuffer<float> sample;
    double fileSampleRate = 44100.0;
    double hostSampleRate = 44100.0;
    std::atomic<double> sampleLengthSec { 0.0 };
    juce::String samplePath;

    static constexpr int peakBlock = 128;
    std::vector<float> peakMin, peakMax;

    std::array<PadSlice, numPads> slices;
    std::array<Voice, numPads> voices;

    std::atomic<double> originalBpm { 120.0 };
    std::atomic<double> gridOffset { 0.0 };
    std::atomic<double> appliedBpm { 120.0 };
    std::atomic<int> lastMode { modeSlicer };
    std::atomic<uint32_t> pendingMask { 0 };

    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* bpmParam = nullptr;
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* volumeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TongaProcessor)
};
