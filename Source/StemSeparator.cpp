#include "StemSeparator.h"
#include "onnxruntime_c_api.h"

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

    std::vector<float> resampleChannel (const float* in, int inLen, double speedRatio, int outLen)
    {
        std::vector<float> padded ((size_t) inLen + 64, 0.0f);
        std::copy (in, in + inLen, padded.begin());

        std::vector<float> out ((size_t) juce::jmax (0, outLen), 0.0f);
        juce::LagrangeInterpolator li;
        li.process (speedRatio, padded.data(), out.data(), outLen);
        return out;
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

        out.numStems = ns;
        out.numFrames = nf;
        out.data.assign ((size_t) ns * 2, std::vector<int16_t>());

        for (auto& v : out.data)
        {
            v.resize ((size_t) nf);
            const size_t bytes = (size_t) nf * sizeof (int16_t);
            if ((size_t) is.read (v.data(), (int) bytes) != bytes)
            {
                out = StemData();
                return false;
            }
        }
        return true;
    }

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
bool StemSeparator::separate (const juce::AudioBuffer<float>& audio, double audioSR, StemData& out,
                              std::atomic<float>& progress, std::atomic<bool>& cancel, juce::String& error)
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

    // --- 2) Sesion con el modelo ---
    if (! ok (api->CreateEnv (ORT_LOGGING_LEVEL_WARNING, "tonga", &h.env)))
        return false;
    if (! ok (api->CreateSessionOptions (&h.opts)))
        return false;

    const int threads = juce::jmax (1, juce::SystemStats::getNumPhysicalCpus() - 1);
    ok (api->SetIntraOpNumThreads (h.opts, threads));
    ok (api->SetSessionGraphOptimizationLevel (h.opts, ORT_ENABLE_ALL));

    progress.store (0.01f);
    if (! ok (api->CreateSession (h.env, findModel().getFullPathName().toWideCharPointer(), h.opts, &h.session)))
        return false;

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

    // --- 3) Audio a 44.1 kHz estereo ---
    const bool needResample = std::abs (audioSR - kModelRate) > 0.5;
    const double ratioIn = audioSR / kModelRate;
    const int len44 = needResample ? (int) std::floor ((double) srcN / ratioIn) : srcN;

    std::vector<float> srcL, srcR;
    if (needResample)
    {
        srcL = resampleChannel (audio.getReadPointer (0), srcN, ratioIn, len44);
        srcR = (chs > 1) ? resampleChannel (audio.getReadPointer (1), srcN, ratioIn, len44) : srcL;
    }
    else
    {
        srcL.assign (audio.getReadPointer (0), audio.getReadPointer (0) + srcN);
        if (chs > 1)
            srcR.assign (audio.getReadPointer (1), audio.getReadPointer (1) + srcN);
        else
            srcR = srcL;
    }

    if (cancel.load())
    {
        error = "Cancelado";
        return false;
    }

    // --- 4) Tensor de entrada reutilizable ---
    std::vector<float> inBuf ((size_t) 2 * kSegment, 0.0f);
    const int64_t shape[3] = { 1, 2, kSegment };
    if (! ok (api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &h.mem)))
        return false;
    if (! ok (api->CreateTensorWithDataAsOrtValue (h.mem, inBuf.data(), inBuf.size() * sizeof (float),
                                                   shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &h.input)))
        return false;

    // Pesos triangulares para mezclar los tramos que se pisan
    const int overlapLen = kSegment / 4;
    const int stride = kSegment - overlapLen;
    const int half = kSegment / 2;
    std::vector<float> weight ((size_t) kSegment);
    for (int i = 0; i < kSegment; ++i)
        weight[(size_t) i] = (i < half) ? (float) (i + 1) / (float) half
                                        : (float) (kSegment - i) / (float) half;

    std::vector<float> acc, wacc ((size_t) kSegment, 0.0f);
    int nStems = 0;

    const int totalChunks = 1 + (len44 > kSegment ? (int) std::ceil ((double) (len44 - kSegment) / (double) stride) : 0);
    const char* inNames[1] = { inputName.toRawUTF8() };
    const char* outNames[1] = { outputName.toRawUTF8() };

    int pos = 0;
    for (int chunk = 0;; ++chunk)
    {
        if (cancel.load())
        {
            error = "Cancelado";
            return false;
        }

        // Tramo de entrada (con ceros si falta audio al final)
        const int n = juce::jmin (kSegment, len44 - pos);
        std::fill (inBuf.begin(), inBuf.end(), 0.0f);
        std::copy (srcL.begin() + pos, srcL.begin() + pos + n, inBuf.begin());
        std::copy (srcR.begin() + pos, srcR.begin() + pos + n, inBuf.begin() + kSegment);

        OrtValue* output = nullptr;
        if (! ok (api->Run (h.session, nullptr, inNames, &h.input, 1, outNames, 1, &output)))
            return false;

        float* res = nullptr;
        if (! ok (api->GetTensorMutableData (output, (void**) &res)) || res == nullptr)
        {
            api->ReleaseValue (output);
            return false;
        }

        if (chunk == 0)
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

            if (nd != 4 || dims[2] != 2 || dims[3] != kSegment || dims[1] < 1 || dims[1] > 8)
            {
                error = "El modelo de stems tiene una forma de salida inesperada.";
                api->ReleaseValue (output);
                return false;
            }

            nStems = (int) dims[1];
            acc.assign ((size_t) nStems * 2 * kSegment, 0.0f);
            out.numStems = nStems;
            out.numFrames = len44;
            out.data.assign ((size_t) nStems * 2, std::vector<int16_t> ((size_t) len44, 0));
        }

        // Sumar este tramo con su peso
        for (int k = 0; k < nStems * 2; ++k)
        {
            const float* s = res + (size_t) k * kSegment;
            float* a = acc.data() + (size_t) k * kSegment;
            for (int i = 0; i < kSegment; ++i)
                a[i] += s[i] * weight[(size_t) i];
        }
        for (int i = 0; i < kSegment; ++i)
            wacc[(size_t) i] += weight[(size_t) i];

        api->ReleaseValue (output);

        // Lo que ya no se va a pisar con el tramo siguiente queda terminado
        const bool last = (pos + kSegment >= len44);
        const int finalCount = last ? (len44 - pos) : stride;

        for (int k = 0; k < nStems * 2; ++k)
        {
            const float* a = acc.data() + (size_t) k * kSegment;
            int16_t* dst = out.data[(size_t) k].data() + pos;
            for (int i = 0; i < finalCount; ++i)
            {
                const float wv = wacc[(size_t) i];
                dst[i] = toI16 (wv > 1.0e-9f ? a[i] / wv : 0.0f);
            }
        }

        progress.store (0.02f + 0.88f * (float) (chunk + 1) / (float) totalChunks);

        if (last)
            break;

        // Correr lo que quedo pendiente (la zona que se pisa) al principio
        for (int k = 0; k < nStems * 2; ++k)
        {
            float* a = acc.data() + (size_t) k * kSegment;
            std::copy (a + stride, a + kSegment, a);
            std::fill (a + overlapLen, a + kSegment, 0.0f);
        }
        std::copy (wacc.begin() + stride, wacc.begin() + kSegment, wacc.begin());
        std::fill (wacc.begin() + overlapLen, wacc.end(), 0.0f);

        pos += stride;
    }

    // --- 5) Volver a la frecuencia de muestreo original ---
    if (needResample)
    {
        const double ratioBack = kModelRate / audioSR;
        for (size_t k = 0; k < out.data.size(); ++k)
        {
            if (cancel.load())
            {
                error = "Cancelado";
                return false;
            }

            std::vector<float> f ((size_t) len44);
            for (int i = 0; i < len44; ++i)
                f[(size_t) i] = (float) out.data[k][(size_t) i] / 32768.0f;

            auto r = resampleChannel (f.data(), len44, ratioBack, srcN);
            out.data[k].assign ((size_t) srcN, 0);
            for (int i = 0; i < srcN; ++i)
                out.data[k][(size_t) i] = toI16 (r[(size_t) i]);

            progress.store (0.9f + 0.1f * (float) (k + 1) / (float) out.data.size());
        }
        out.numFrames = srcN;
    }

    progress.store (1.0f);
    return true;
   #endif
}
