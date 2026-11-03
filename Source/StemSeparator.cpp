#include "StemSeparator.h"
#include "onnxruntime_c_api.h"
#include <map>

namespace
{
    constexpr int kSegment = 343980;          // largo (en muestras) que espera el modelo: 7.8 s a 44.1 kHz
    constexpr double kModelRate = 44100.0;
    const char* const kModelName = "htdemucs_fp16weights.onnx";

    juce::File moduleFile()
    {
        // En un plugin esto es la ruta del propio .vst3 (la DLL)
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    }

    juce::File appDataDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Tonga Sampler 2");
    }

    // La DLL de onnxruntime se carga una sola vez y queda cargada mientras viva el programa
    juce::DynamicLibrary& runtimeLib()
    {
        static auto* lib = new juce::DynamicLibrary();
        return *lib;
    }

    int16_t toI16 (float v)
    {
        v = juce::jlimit (-1.0f, 1.0f, v);
        return (int16_t) juce::roundToInt (v * 32767.0f);
    }

    // ---- Archivo de stems guardado (cache) ----
    constexpr int kCacheMagic = 0x4D545354;   // "TSTM"
    constexpr int kCacheVersion = 1;

    juce::File cacheNextTo (const juce::File& audio)
    {
        return audio.getSiblingFile (audio.getFileName() + ".tonga-stems");
    }

    juce::File cacheFallback (const juce::File& audio)
    {
        return appDataDir().getChildFile ("stems-cache")
                           .getChildFile (audio.getFileName() + "-"
                                          + juce::String::toHexString (audio.getFullPathName().hashCode64())
                                          + ".tonga-stems");
    }

    bool writeCacheTo (const juce::File& target, const juce::File& audio, double sr, const StemData& d)
    {
        target.getParentDirectory().createDirectory();
        const juce::File tmp = target.getSiblingFile (target.getFileName() + ".tmp");
        tmp.deleteFile();

        bool good = false;
        {
            juce::FileOutputStream os (tmp);
            if (os.failedToOpen())
                return false;

            os.writeInt (kCacheMagic);
            os.writeInt (kCacheVersion);
            os.writeInt (d.numStems);
            os.writeInt (d.numFrames);
            os.writeDouble (sr);
            os.writeInt64 (audio.getSize());
            os.writeInt64 (audio.getLastModificationTime().toMilliseconds());

            good = true;
            for (auto& v : d.data)
            {
                if (! os.write (v.data(), v.size() * sizeof (int16_t)))
                {
                    good = false;
                    break;
                }
            }
            os.flush();
            if (os.getStatus().failed())
                good = false;
        }

        if (! good)
        {
            tmp.deleteFile();
            return false;
        }
        return tmp.moveFileTo (target);
    }

    bool readCacheFrom (const juce::File& f, const juce::File& audio, double sr, int expectedFrames, StemData& out)
    {
        if (! f.existsAsFile())
            return false;

        juce::FileInputStream is (f);
        if (is.failedToOpen())
            return false;

        if (is.readInt() != kCacheMagic || is.readInt() != kCacheVersion)
            return false;

        const int ns = is.readInt();
        const int nf = is.readInt();
        const double fsr = is.readDouble();
        const juce::int64 asize = is.readInt64();
        const juce::int64 amod = is.readInt64();

        // Tiene que ser exactamente el mismo audio que cuando se separo
        if (ns < 1 || ns > 8 || nf != expectedFrames || std::abs (fsr - sr) > 0.5)
            return false;
        if (asize != audio.getSize() || amod != audio.getLastModificationTime().toMilliseconds())
            return false;

        out.init (ns, nf, true);

        for (auto& v : out.data)
        {
            const size_t bytes = (size_t) nf * sizeof (int16_t);
            if ((size_t) is.read (v.data(), (int) bytes) != bytes)
            {
                out = StemData();
                return false;
            }
        }
        return true;
    }


    // Deja en %APPDATA%\Tonga Sampler 2\stems-log.txt cuanto tardo cada separacion (sirve para medir la velocidad)
    struct SepLog
    {
        double t0 = juce::Time::getMillisecondCounterHiRes();
        double loadedAt = 0.0;
        int threads = 0;
        int frames = 0;
        int chunks = 0;
        int totalChunks = 0;
        bool completed = false;

        ~SepLog()
        {
            if (frames <= 0 || threads <= 0)
                return;

            const double totalSec = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            const double loadSec = loadedAt > 0.0 ? (loadedAt - t0) / 1000.0 : 0.0;
            const double audioSec = (double) frames / 44100.0;
            const double procSec = juce::jmax (0.001, totalSec - loadSec);

            juce::String text;
            text << juce::Time::getCurrentTime().toString (true, true) << "\n"
                 << "  CPU: " << juce::SystemStats::getCpuModel() << "\n"
                 << "  Nucleos fisicos: " << juce::SystemStats::getNumPhysicalCpus()
                 << "  logicos: " << juce::SystemStats::getNumCpus() << "  hilos usados: " << threads << "\n"
                 << "  Audio: " << juce::String (audioSec, 1) << " s   tramos: " << chunks << " de " << totalChunks
                 << (completed ? "  (completo)" : "  (incompleto)") << "\n"
                 << "  Carga del modelo: " << juce::String (loadSec, 1) << " s   separacion: " << juce::String (procSec, 1) << " s\n";
            if (chunks > 0)
                text << "  Por tramo: " << juce::String (procSec / (double) chunks, 2) << " s   "
                     << "velocidad: " << juce::String (audioSec * ((double) chunks / (double) juce::jmax (1, totalChunks)) / procSec, 2)
                     << "x tiempo real\n";
            text << "\n";

            auto f = appDataDir().getChildFile ("stems-log.txt");
            f.getParentDirectory().createDirectory();
            f.appendText (text);
        }
    };

    // Libera todo lo de onnxruntime al salir, pase lo que pase
    struct OrtHandles
    {
        const OrtApi* api = nullptr;
        OrtEnv* env = nullptr;
        OrtSessionOptions* opts = nullptr;
        OrtSession* session = nullptr;
        OrtMemoryInfo* mem = nullptr;
        OrtValue* input = nullptr;

        ~OrtHandles()
        {
            if (api == nullptr)
                return;
            if (input != nullptr)   api->ReleaseValue (input);
            if (mem != nullptr)     api->ReleaseMemoryInfo (mem);
            if (session != nullptr) api->ReleaseSession (session);
            if (opts != nullptr)    api->ReleaseSessionOptions (opts);
            if (env != nullptr)     api->ReleaseEnv (env);
        }
    };
}

//==============================================================================
juce::File StemSeparator::findRuntime()
{
    const juce::File candidates[] = { moduleFile().getSiblingFile ("onnxruntime.dll"),
                                      appDataDir().getChildFile ("onnxruntime.dll") };
    for (auto& f : candidates)
        if (f.existsAsFile())
            return f;
    return candidates[0];
}

juce::File StemSeparator::findModel()
{
    const juce::File candidates[] = {
        moduleFile().getParentDirectory().getParentDirectory().getChildFile ("Resources").getChildFile (kModelName),
        moduleFile().getSiblingFile (kModelName),
        appDataDir().getChildFile (kModelName)
    };
    for (auto& f : candidates)
        if (f.existsAsFile())
            return f;
    return candidates[0];
}

bool StemSeparator::isAvailable (juce::String& whyNot)
{
   #if ! JUCE_WINDOWS
    whyNot = "Los stems por ahora solo funcionan en Windows.";
    return false;
   #else
    if (! findRuntime().existsAsFile())
    {
        whyNot = "Falta onnxruntime.dll junto al plugin.";
        return false;
    }
    if (! findModel().existsAsFile())
    {
        whyNot = "Falta el modelo de stems (" + juce::String (kModelName) + ") dentro de la carpeta del plugin.";
        return false;
    }
    return true;
   #endif
}


bool StemSeparator::saveCache (const juce::File& audio, double sampleRate, const StemData& data, juce::String& whereOrError)
{
    if (writeCacheTo (cacheNextTo (audio), audio, sampleRate, data))
    {
        whereOrError = cacheNextTo (audio).getFullPathName();
        return true;
    }

    // Carpeta del audio sin permiso de escritura: usar la de Tonga Sampler 2
    if (writeCacheTo (cacheFallback (audio), audio, sampleRate, data))
    {
        whereOrError = cacheFallback (audio).getFullPathName();
        return true;
    }

    whereOrError = "no hay permiso o espacio para escribir en " + audio.getParentDirectory().getFullPathName();
    return false;
}

bool StemSeparator::loadCache (const juce::File& audio, double sampleRate, int expectedFrames, StemData& out)
{
    if (readCacheFrom (cacheNextTo (audio), audio, sampleRate, expectedFrames, out))
        return true;
    return readCacheFrom (cacheFallback (audio), audio, sampleRate, expectedFrames, out);
}

//==============================================================================
bool StemSeparator::separate (const juce::AudioBuffer<float>& audio, double audioSR, StemData& out, int startFrame,
                              std::atomic<float>& progress, std::atomic<bool>& cancel,
                              const std::function<void()>& onChunkDone, juce::String& error)
{
    juce::String why;
    if (! isAvailable (why))
    {
        error = why;
        return false;
    }

   #if ! JUCE_WINDOWS
    return false;
   #else
    const int srcN = audio.getNumSamples();
    const int chs = audio.getNumChannels();
    if (srcN < 2000 || chs < 1)
    {
        error = "El audio es demasiado corto para separar.";
        return false;
    }
    if (std::abs (audioSR - kModelRate) > 0.5)
    {
        error = "Para separar stems el audio tiene que estar a 44.1 kHz.";
        return false;
    }
    if (out.numFrames != srcN || out.numStems < 1 || out.done == nullptr)
    {
        error = "Los stems no estan preparados.";
        return false;
    }

    // --- 1) Cargar onnxruntime.dll por su ruta completa (evita usar otra copia del sistema) ---
    auto& lib = runtimeLib();
    if (lib.getNativeHandle() == nullptr && ! lib.open (findRuntime().getFullPathName()))
    {
        error = "No se pudo cargar onnxruntime.dll.";
        return false;
    }

    using GetBaseFn = const OrtApiBase* (ORT_API_CALL*) (void);
    auto getBase = (GetBaseFn) lib.getFunction ("OrtGetApiBase");
    if (getBase == nullptr)
    {
        error = "onnxruntime.dll no es valida.";
        return false;
    }

    const OrtApi* api = getBase()->GetApi (ORT_API_VERSION);
    if (api == nullptr)
    {
        error = "La version de onnxruntime.dll no coincide con la esperada.";
        return false;
    }

    SepLog sepLog;
    sepLog.frames = srcN;

    OrtHandles h;
    h.api = api;

    auto ok = [&] (OrtStatus* st) -> bool
    {
        if (st == nullptr)
            return true;
        error = juce::String ("onnxruntime: ") + juce::String::fromUTF8 (api->GetErrorMessage (st));
        api->ReleaseStatus (st);
        return false;
    };

    // --- 2) Sesion con el modelo (usa todos los nucleos fisicos) ---
    if (! ok (api->CreateEnv (ORT_LOGGING_LEVEL_WARNING, "tonga", &h.env)))
        return false;
    if (! ok (api->CreateSessionOptions (&h.opts)))
        return false;

    // Deja libre una parte del procesador (para que el DAW siga fluido) y sin "espera activa"
    const int cores = juce::jmax (1, juce::SystemStats::getNumPhysicalCpus());
    const int threads = juce::jmax (1, (cores * 3) / 4);
    sepLog.threads = threads;
    ok (api->SetIntraOpNumThreads (h.opts, threads));
    ok (api->SetInterOpNumThreads (h.opts, 1));
    ok (api->AddSessionConfigEntry (h.opts, "session.intra_op.allow_spinning", "0"));
    ok (api->SetSessionGraphOptimizationLevel (h.opts, ORT_ENABLE_ALL));

    progress.store (0.01f);
    if (! ok (api->CreateSession (h.env, findModel().getFullPathName().toWideCharPointer(), h.opts, &h.session)))
        return false;

    sepLog.loadedAt = juce::Time::getMillisecondCounterHiRes();

    OrtAllocator* alloc = nullptr;
    if (! ok (api->GetAllocatorWithDefaultOptions (&alloc)))
        return false;

    char* inRaw = nullptr;
    char* outRaw = nullptr;
    if (! ok (api->SessionGetInputName (h.session, 0, alloc, &inRaw)))
        return false;
    if (! ok (api->SessionGetOutputName (h.session, 0, alloc, &outRaw)))
    {
        api->AllocatorFree (alloc, inRaw);
        return false;
    }
    const juce::String inputName (juce::String::fromUTF8 (inRaw));
    const juce::String outputName (juce::String::fromUTF8 (outRaw));
    api->AllocatorFree (alloc, inRaw);
    api->AllocatorFree (alloc, outRaw);

    // --- 3) Tensor de entrada reutilizable ---
    std::vector<float> inBuf ((size_t) 2 * kSegment, 0.0f);
    const int64_t shape[3] = { 1, 2, kSegment };
    if (! ok (api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &h.mem)))
        return false;
    if (! ok (api->CreateTensorWithDataAsOrtValue (h.mem, inBuf.data(), inBuf.size() * sizeof (float),
                                                   shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &h.input)))
        return false;

    const float* srcL = audio.getReadPointer (0);
    const float* srcR = chs > 1 ? audio.getReadPointer (1) : srcL;

    // Tramos: se pisan un poco (1/8) y se mezclan con pesos triangulares
    const int overlapLen = kSegment / 8;
    const int stride = kSegment - overlapLen;
    const int half = kSegment / 2;
    std::vector<float> weight ((size_t) kSegment);
    for (int i = 0; i < kSegment; ++i)
        weight[(size_t) i] = (i < half) ? (float) (i + 1) / (float) half
                                        : (float) (kSegment - i) / (float) half;

    const int totalChunks = 1 + (srcN > kSegment ? (int) std::ceil ((double) (srcN - kSegment) / (double) stride) : 0);
    sepLog.totalChunks = totalChunks;

    // Orden: empieza en el tramo del cursor, sigue hacia adelante y despues vuelve hacia atras
    const int firstChunk = juce::jlimit (0, totalChunks - 1, startFrame / stride);
    std::vector<int> order;
    for (int k = firstChunk; k < totalChunks; ++k)
        order.push_back (k);
    for (int k = firstChunk - 1; k >= 0; --k)
        order.push_back (k);

    const char* inNames[1] = { inputName.toRawUTF8() };
    const char* outNames[1] = { outputName.toRawUTF8() };

    const int nStems = out.numStems;
    const int nCh = nStems * 2;
    std::map<int, std::vector<float>> headStore, tailStore;   // puntas que esperan al tramo vecino

    int chunksDone = 0;
    for (const int k : order)
    {
        if (cancel.load())
        {
            error = "Cancelado";
            return false;
        }

        const int pos = k * stride;
        const int n = juce::jmin (kSegment, srcN - pos);
        std::fill (inBuf.begin(), inBuf.end(), 0.0f);
        std::copy (srcL + pos, srcL + pos + n, inBuf.begin());
        std::copy (srcR + pos, srcR + pos + n, inBuf.begin() + kSegment);

        OrtValue* output = nullptr;
        if (! ok (api->Run (h.session, nullptr, inNames, &h.input, 1, outNames, 1, &output)))
            return false;

        float* res = nullptr;
        if (! ok (api->GetTensorMutableData (output, (void**) &res)) || res == nullptr)
        {
            api->ReleaseValue (output);
            return false;
        }

        if (chunksDone == 0)
        {
            // Validar la forma de la salida: (1, stems, 2, 343980)
            OrtTensorTypeAndShapeInfo* info = nullptr;
            size_t nd = 0;
            std::vector<int64_t> dims;
            if (ok (api->GetTensorTypeAndShape (output, &info)))
            {
                api->GetDimensionsCount (info, &nd);
                dims.resize (nd);
                if (nd > 0)
                    api->GetDimensions (info, dims.data(), nd);
                api->ReleaseTensorTypeAndShapeInfo (info);
            }

            if (nd != 4 || dims[1] != nStems || dims[2] != 2 || dims[3] != kSegment)
            {
                error = "El modelo de stems tiene una forma de salida inesperada.";
                api->ReleaseValue (output);
                return false;
            }
        }

        // Escribe una parte del tramo tal cual salio del modelo
        auto putRaw = [&] (int g0, int localStart, int count)
        {
            const int n2 = juce::jmin (count, srcN - g0);
            if (n2 <= 0)
                return;
            for (int c = 0; c < nCh; ++c)
            {
                const float* r = res + (size_t) c * kSegment + localStart;
                int16_t* d = out.data[(size_t) c].data() + g0;
                for (int i = 0; i < n2; ++i)
                    d[i] = toI16 (r[i]);
            }
            out.markDone (g0, g0 + n2);
        };

        // Mezcla la punta final de un tramo con la punta inicial del siguiente
        auto blend = [&] (int g0, const float* tBase, size_t tStride, const float* hBase, size_t hStride)
        {
            const int n2 = juce::jmin (overlapLen, srcN - g0);
            if (n2 <= 0)
                return;
            for (int c = 0; c < nCh; ++c)
            {
                const float* t = tBase + (size_t) c * tStride;
                const float* hd = hBase + (size_t) c * hStride;
                int16_t* d = out.data[(size_t) c].data() + g0;
                for (int i = 0; i < n2; ++i)
                {
                    const float wt = weight[(size_t) (stride + i)];
                    const float wh = weight[(size_t) i];
                    d[i] = toI16 ((t[i] * wt + hd[i] * wh) / (wt + wh));
                }
            }
            out.markDone (g0, g0 + n2);
        };

        const bool last = (k == totalChunks - 1);

        // Parte del medio: solo la cubre este tramo
        putRaw (pos + overlapLen, overlapLen, (last ? kSegment : stride) - overlapLen);

        // Punta inicial (se comparte con el tramo anterior)
        if (k == 0)
        {
            putRaw (pos, 0, overlapLen);
        }
        else
        {
            auto it = tailStore.find (k - 1);
            if (it != tailStore.end())
            {
                blend (pos, it->second.data(), (size_t) overlapLen, res, (size_t) kSegment);
                tailStore.erase (it);
            }
            else
            {
                std::vector<float> hv ((size_t) nCh * overlapLen);
                for (int c = 0; c < nCh; ++c)
                    std::copy (res + (size_t) c * kSegment, res + (size_t) c * kSegment + overlapLen,
                               hv.begin() + (size_t) c * overlapLen);
                headStore[k] = std::move (hv);
            }
        }

        // Punta final (se comparte con el tramo siguiente)
        if (! last)
        {
            auto it = headStore.find (k + 1);
            if (it != headStore.end())
            {
                blend ((k + 1) * stride, res + stride, (size_t) kSegment, it->second.data(), (size_t) overlapLen);
                headStore.erase (it);
            }
            else
            {
                std::vector<float> tv ((size_t) nCh * overlapLen);
                for (int c = 0; c < nCh; ++c)
                    std::copy (res + (size_t) c * kSegment + stride, res + (size_t) c * kSegment + stride + overlapLen,
                               tv.begin() + (size_t) c * overlapLen);
                tailStore[k] = std::move (tv);
            }
        }

        api->ReleaseValue (output);

        ++chunksDone;
        sepLog.chunks = chunksDone;
        progress.store (0.02f + 0.98f * (float) chunksDone / (float) totalChunks);
        if (onChunkDone)
            onChunkDone();
    }

    progress.store (1.0f);
    sepLog.completed = true;
    return true;
   #endif
}
