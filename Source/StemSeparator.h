#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

// Resultado de separar un audio: cada stem es estereo y se guarda en 16 bits para ahorrar memoria.
// Orden de los stems del modelo: 0 bateria, 1 bajo, 2 otros, 3 voz.
struct StemData
{
    int numStems = 0;
    int numFrames = 0;
    std::vector<std::vector<int16_t>> data;   // [stem * 2 + canal]
};

namespace StemSeparator
{
    // Archivos que tienen que estar junto al plugin
    juce::File findModel();
    juce::File findRuntime();

    // true si estan el modelo y onnxruntime.dll; si no, explica que falta
    bool isAvailable (juce::String& whyNot);

    // Archivo de stems guardado junto al audio (como Serato): "cancion.mp3.tonga-stems".
    // Si no se puede escribir ahi, se usa la carpeta de datos de Tonga Sampler 2.
    bool saveCache (const juce::File& audio, double sampleRate, const StemData& data, juce::String& whereOrError);
    bool loadCache (const juce::File& audio, double sampleRate, int expectedFrames, StemData& out);

    // Separa el audio (cualquier frecuencia de muestreo, mono o estereo).
    // Bloquea hasta terminar: llamarlo desde un hilo aparte.
    bool separate (const juce::AudioBuffer<float>& audio, double audioSampleRate, StemData& out,
                   std::atomic<float>& progress, std::atomic<bool>& cancel, juce::String& error);
}
