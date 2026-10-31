#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

// Resultado de separar un audio: cada stem es estereo y se guarda en 16 bits para ahorrar memoria.
// Orden de los stems del modelo: 0 bateria, 1 bajo, 2 otros, 3 voz.
// "done" marca, muestra por muestra, que partes ya estan separadas (se llena de a tramos).
struct StemData
{
    int numStems = 0;
    int numFrames = 0;
    std::vector<std::vector<int16_t>> data;          // [stem * 2 + canal]
    std::unique_ptr<std::atomic<uint8_t>[]> done;

    void init (int stems, int frames, bool allDone)
    {
        numStems = stems;
        numFrames = frames;
        data.assign ((size_t) stems * 2, std::vector<int16_t> ((size_t) frames, 0));
        done.reset (new std::atomic<uint8_t>[(size_t) frames]);
        for (int i = 0; i < frames; ++i)
            done[(size_t) i].store (allDone ? 1 : 0, std::memory_order_relaxed);
    }

    bool isDone (int i) const { return done[(size_t) i].load (std::memory_order_acquire) != 0; }

    void markDone (int from, int to)
    {
        for (int i = from; i < to; ++i)
            done[(size_t) i].store (1, std::memory_order_release);
    }
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

    // Separa el audio (estereo, 44.1 kHz) y va llenando 'out' (ya preparado con init) de a tramos,
    // empezando por el tramo donde esta 'startFrame' y siguiendo hacia adelante y despues hacia atras.
    // 'onChunkDone' se llama cada vez que termina un tramo. Bloquea hasta terminar: usar un hilo aparte.
    bool separate (const juce::AudioBuffer<float>& audio, double audioSampleRate, StemData& out, int startFrame,
                   std::atomic<float>& progress, std::atomic<bool>& cancel,
                   const std::function<void()>& onChunkDone, juce::String& error);
}
