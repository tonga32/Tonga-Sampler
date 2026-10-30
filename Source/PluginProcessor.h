#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "StemSeparator.h"

// Un corte (slice) asignado a un pad, con sus ajustes propios.
// Las posiciones estan en segundos del audio original.
struct PadSlice
{
    bool valid = false;
    double start = 0.0;
    double end = 0.0;
    float gain = 1.0f;
    bool reverse = false;
    int keyShift = 0;        // semitonos
    float stretch = 0.0f;    // % de velocidad (0 = original)
    float filter = 0.0f;     // -1 = pasa bajos, 0 = apagado, +1 = pasa altos
    float attack = 0.0f;     // segundos
    float release = 0.02f;   // segundos
};

class TongaProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int numPads = 32;
    enum SliceMode { modeSlicer = 0, modeTransient = 1, modeSamples = 2, modeRandom = 3 };

    TongaProcessor();
    ~TongaProcessor() override;

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

    // --- Cursor y reproduccion del audio completo ---
    double getCursor() const { return cursorSec.load(); }
    void setCursor (double s) { cursorSec.store (juce::jlimit (0.0, juce::jmax (0.0, getLengthSec()), s)); }
    void toggleAudition();
    bool isAuditioning() const { return auditioning.load(); }
    double getAuditionPos() const { return auditionPosSec.load(); }

    // --- Pads ---
    PadSlice getSlice (int pad);
    void setSlice (int pad, const PadSlice& s);
    void clearPad (int pad);
    uint32_t getValidMask();
    void applySlicing (int mode);
    int getLastMode() const { return lastMode.load(); }

    void triggerPad (int pad);
    void releasePad (int pad);

    // --- Stems (separacion con IA) ---
    bool stemsSupported (juce::String& whyNot) const { return StemSeparator::isAvailable (whyNot); }
    void startStemSeparation();
    void cancelStemSeparation() { stemCancel.store (true); }
    void removeStems();                         // vuelve al audio original
    bool isStemBusy() const { return stemBusy.load(); }
    bool hasStems() const { return stemsReady.load(); }
    float getStemProgress() const { return stemProgress.load(); }
    juce::String getStemMessage() const;
    void stemThreadLoop (juce::Thread& thread); // uso interno del hilo de trabajo

    // Informacion para la interfaz
    std::atomic<int> lastPlayedPad { 0 };
    std::atomic<int> playCounter { 0 };
    std::atomic<uint32_t> playingMask { 0 };
    std::atomic<double> lastPlayPosSec { -1.0 };

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Grain
    {
        bool on = false;
        bool first = false;
        double pos = 0.0;
        int age = 0;
    };

    struct Voice
    {
        bool active = false;
        bool releasing = false;
        bool ended = false;
        bool rev = false;
        double startS = 0.0;
        double endS = 0.0;
        double anchor = 0.0;
        int delay = 0;
        int sinceSpawn = 0;
        int spawned = 0;
        int lastGrain = -1;
        float env = 0.0f;
        float relInc = 0.0f;
        float ic1[2] = { 0.0f, 0.0f };
        float ic2[2] = { 0.0f, 0.0f };
        Grain grains[3];
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void setStemMessage (const juce::String& m);
    void doSeparate();
    void doRebuildMix();
    void detectTempo();
    void buildTables (double sr);
    bool startVoice (int pad, int delaySamples);
    void initVoice (Voice& v, double startS, double endS, bool rev, int delaySamples);
    void releaseVoice (Voice& v, double seconds);
    void renderVoice (Voice& v, const PadSlice& sp, float* outL, float* outR, int n,
                      double tempoRatio, float master, double globalSemis);

    juce::AudioFormatManager formatManager;
    juce::CriticalSection dataLock;
    juce::CriticalSection peakLock;

    juce::AudioBuffer<float> sample;
    double fileSampleRate = 44100.0;
    double hostSampleRate = 44100.0;
    std::atomic<double> sampleLengthSec { 0.0 };
    juce::String samplePath;

    static constexpr int peakBlock = 128;
    std::vector<float> peakMin, peakMax;

    std::array<PadSlice, numPads> slices;
    std::array<Voice, numPads + 1> voices;   // el ultimo es el del play del audio completo
    PadSlice auditionParams;

    int hop = 1102;
    int grainLen = 2204;
    std::vector<float> hannWin, firstWin;

    std::atomic<double> originalBpm { 120.0 };
    std::atomic<double> gridOffset { 0.0 };
    std::atomic<double> appliedBpm { 120.0 };
    std::atomic<double> cursorSec { 0.0 };
    std::atomic<int> lastMode { -1 };
    std::atomic<uint32_t> pendingMask { 0 };
    std::atomic<uint32_t> pendingRelease { 0 };
    std::atomic<int> auditionCmd { 0 };
    std::atomic<bool> auditioning { false };
    std::atomic<double> auditionPosSec { 0.0 };

    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* bpmParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* polyParam = nullptr;
    std::atomic<float>* holdParam = nullptr;
    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* gkeyParam = nullptr;
    std::atomic<float>* gdetuneParam = nullptr;
    std::atomic<float>* stemParam[4] = { nullptr, nullptr, nullptr, nullptr };

    // Stems
    std::unique_ptr<juce::Thread> stemThread;
    mutable juce::CriticalSection stemLock;
    StemData stems;
    juce::String stemMessage;
    juce::AudioBuffer<float> stemAudio;
    double stemAudioSR = 44100.0;
    int stemGeneration = 0;
    std::atomic<int> sampleGeneration { 0 };
    std::atomic<bool> stemBusy { false };
    std::atomic<bool> stemsReady { false };
    std::atomic<bool> stemCancel { false };
    std::atomic<bool> separateRequested { false };
    std::atomic<bool> mixDirty { false };
    std::atomic<float> stemProgress { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TongaProcessor)
};
