#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Cuartos de nota por compas: 2/4, 3/4, 4/4, 6/8, 9/8, 12/8
    const double quartersPerBarTable[6] = { 2.0, 3.0, 4.0, 3.0, 4.5, 6.0 };

    // Envolvente de energia (promedio del valor absoluto por bloque de "hop" muestras)
    std::vector<float> envelopeOf (const juce::AudioBuffer<float>& b, int hop)
    {
        const int n = juce::jmax (0, b.getNumSamples() / hop);
        std::vector<float> env ((size_t) n, 0.0f);
        const int chs = juce::jmax (1, b.getNumChannels());

        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                const float* d = b.getReadPointer (ch) + (size_t) i * (size_t) hop;
                for (int k = 0; k < hop; ++k)
                    sum += std::abs (d[k]);
            }
            env[(size_t) i] = sum / (float) (hop * chs);
        }
        return env;
    }

    struct Onset  { double time; float strength; };
    struct Region { double start; double end; double score; };

    std::vector<Onset> findOnsets (const juce::AudioBuffer<float>& b, double sr)
    {
        const int hop = 256;
        auto env = envelopeOf (b, hop);
        const int n = (int) env.size();
        std::vector<Onset> res;
        if (n < 16)
            return res;

        std::vector<float> flux ((size_t) n, 0.0f);
        for (int i = 1; i < n; ++i)
            flux[(size_t) i] = juce::jmax (0.0f, env[(size_t) i] - env[(size_t) i - 1]);

        const int w = 12;
        const int minGap = juce::jmax (1, (int) (0.06 * sr / hop));
        int lastIdx = -minGap;

        for (int i = 1; i < n - 1; ++i)
        {
            const float f = flux[(size_t) i];
            if (f <= flux[(size_t) i - 1] || f < flux[(size_t) i + 1])
                continue;

            float mean = 0.0f;
            int cnt = 0;
            for (int k = juce::jmax (0, i - w); k <= juce::jmin (n - 1, i + w); ++k)
            {
                mean += flux[(size_t) k];
                ++cnt;
            }
            mean /= (float) cnt;

            if (f > mean * 2.0f + 1.0e-5f)
            {
                if (i - lastIdx < minGap)
                {
                    if (! res.empty() && f > res.back().strength)
                    {
                        res.back().time = (double) i * hop / sr;
                        res.back().strength = f;
                        lastIdx = i;
                    }
                    continue;
                }
                res.push_back ({ (double) i * hop / sr, f });
                lastIdx = i;
            }
        }
        return res;
    }

    // Busca zonas con sonido (no solo golpes): desde que arranca hasta que se apaga.
    std::vector<Region> findRegions (const juce::AudioBuffer<float>& b, double sr)
    {
        const int hop = 512;
        auto env = envelopeOf (b, hop);
        const int n = (int) env.size();
        std::vector<Region> merged;
        if (n < 8)
            return merged;

        std::vector<float> sm ((size_t) n, 0.0f);
        float maxv = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = env[(size_t) juce::jmax (0, i - 1)];
            const float c = env[(size_t) i];
            const float d = env[(size_t) juce::jmin (n - 1, i + 1)];
            sm[(size_t) i] = (a + c + d) / 3.0f;
            maxv = juce::jmax (maxv, sm[(size_t) i]);
        }
        if (maxv <= 0.0f)
            return merged;

        const float thr = 0.15f * maxv;
        std::vector<Region> regions;
        bool inRun = false;
        int rs = 0;
        double sc = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const bool above = sm[(size_t) i] > thr;
            if (above && ! inRun) { inRun = true; rs = i; sc = 0.0; }
            if (above) sc += sm[(size_t) i];
            if (inRun && (! above || i == n - 1))
            {
                inRun = false;
                const int re = above ? n : i;
                regions.push_back ({ (double) rs * hop / sr, (double) re * hop / sr, sc });
            }
        }

        for (auto& r : regions)
        {
            if (! merged.empty() && r.start - merged.back().end < 0.06)
            {
                merged.back().end = r.end;
                merged.back().score += r.score;
            }
            else
            {
                merged.push_back (r);
            }
        }

        std::vector<Region> kept;
        for (auto& r : merged)
            if (r.end - r.start >= 0.08)
                kept.push_back (r);
        return kept;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TongaProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sync", 1 }, "Sync DAW", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "bpm", 1 }, "BPM",
                                                             juce::NormalisableRange<float> (40.0f, 300.0f, 0.01f), 120.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "timesig", 1 }, "Compas",
                                                              juce::StringArray { "2/4", "3/4", "4/4", "6/8", "9/8", "12/8" }, 2));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "size", 1 }, "Tamano del corte",
                                                             juce::NormalisableRange<float> (0.1f, 2.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "volume", 1 }, "Volumen",
                                                             juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f));
    return layout;
}

TongaProcessor::TongaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();

    syncParam   = apvts.getRawParameterValue ("sync");
    bpmParam    = apvts.getRawParameterValue ("bpm");
    sizeParam   = apvts.getRawParameterValue ("size");
    volumeParam = apvts.getRawParameterValue ("volume");
}

void TongaProcessor::prepareToPlay (double sampleRate, int)
{
    hostSampleRate = sampleRate;
}

bool TongaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* TongaProcessor::createEditor()
{
    return new TongaEditor (*this);
}

//==============================================================================
double TongaProcessor::getQuartersPerBar() const
{
    const int idx = juce::jlimit (0, 5, (int) apvts.getRawParameterValue ("timesig")->load());
    return quartersPerBarTable[idx];
}

double TongaProcessor::getBarSeconds() const
{
    return getQuartersPerBar() * 60.0 / juce::jmax (1.0, originalBpm.load());
}

void TongaProcessor::setGrid (double offsetSec, double newOriginalBpm)
{
    newOriginalBpm = juce::jlimit (40.0, 300.0, newOriginalBpm);
    originalBpm.store (newOriginalBpm);
    gridOffset.store (juce::jmax (0.0, offsetSec));

    if (auto* p = apvts.getParameter ("bpm"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) newOriginalBpm));
}

//==============================================================================
juce::String TongaProcessor::getSampleName() const
{
    return juce::File (samplePath).getFileName();
}

void TongaProcessor::detectTempo()
{
    const int hop = 512;
    auto env = envelopeOf (sample, hop);
    const int n = (int) env.size();
    if (n < 128)
        return;

    std::vector<float> flux ((size_t) n, 0.0f);
    float fmax = 0.0f;
    for (int i = 1; i < n; ++i)
    {
        flux[(size_t) i] = juce::jmax (0.0f, env[(size_t) i] - env[(size_t) i - 1]);
        fmax = juce::jmax (fmax, flux[(size_t) i]);
    }

    const double fps = fileSampleRate / hop;
    const int nAn = juce::jmin (n, (int) (fps * 90.0));
    const int minLag = juce::jmax (1, (int) (fps * 60.0 / 180.0));
    const int maxLag = juce::jmin (nAn - 1, (int) (fps * 60.0 / 70.0));

    double bestScore = 0.0;
    int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = lag; i < nAn; ++i)
            s += (double) flux[(size_t) i] * (double) flux[(size_t) (i - lag)];
        s /= (double) (nAn - lag);
        if (s > bestScore) { bestScore = s; bestLag = lag; }
    }

    double bpm = 120.0;
    if (bestLag > 0)
    {
        bpm = 60.0 * fps / (double) bestLag;
        while (bpm < 80.0)   bpm *= 2.0;
        while (bpm >= 160.0) bpm /= 2.0;
    }
    bpm = std::round (bpm * 10.0) / 10.0;

    double offset = 0.0;
    for (int i = 1; i < n; ++i)
    {
        if (flux[(size_t) i] > 0.4f * fmax)
        {
            offset = (double) i * hop / fileSampleRate;
            break;
        }
    }

    setGrid (offset, bpm);
}

bool TongaProcessor::loadFile (const juce::File& file, bool detectGrid)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 60.0 * 15.0))
        return false; // mas de 15 minutos: no

    const int chs = (int) juce::jmin ((unsigned int) 2, reader->numChannels);
    const int len = (int) reader->lengthInSamples;

    juce::AudioBuffer<float> tmp (chs, len);
    reader->read (&tmp, 0, len, 0, true, chs > 1);

    // Picos para dibujar la onda
    const int nb = (len + peakBlock - 1) / peakBlock;
    std::vector<float> mins ((size_t) nb, 0.0f), maxs ((size_t) nb, 0.0f);
    for (int b = 0; b < nb; ++b)
    {
        const int s0 = b * peakBlock;
        const int s1 = juce::jmin (len, s0 + peakBlock);
        float mn = 0.0f, mx = 0.0f;
        for (int ch = 0; ch < chs; ++ch)
        {
            const float* d = tmp.getReadPointer (ch);
            for (int i = s0; i < s1; ++i)
            {
                mn = juce::jmin (mn, d[i]);
                mx = juce::jmax (mx, d[i]);
            }
        }
        mins[(size_t) b] = mn;
        maxs[(size_t) b] = mx;
    }

    {
        const juce::ScopedLock sl (dataLock);
        sample = std::move (tmp);
        fileSampleRate = reader->sampleRate;
        peakMin = std::move (mins);
        peakMax = std::move (maxs);
        samplePath = file.getFullPathName();
        sampleLengthSec.store ((double) len / fileSampleRate);
        for (auto& v : voices) v.active = false;
        for (auto& s : slices) s = PadSlice();
    }

    if (detectGrid)
    {
        detectTempo();
        applySlicing (modeSlicer);
    }
    return true;
}

void TongaProcessor::getPeaks (double startSec, double lenSec, int columns,
                               std::vector<float>& mins, std::vector<float>& maxs)
{
    mins.assign ((size_t) juce::jmax (0, columns), 0.0f);
    maxs.assign ((size_t) juce::jmax (0, columns), 0.0f);

    const juce::ScopedLock sl (dataLock);
    const int nb = (int) peakMin.size();
    if (nb == 0 || columns <= 0 || lenSec <= 0.0)
        return;

    const double dur = sampleLengthSec.load();
    const double perCol = lenSec / (double) columns;

    for (int c = 0; c < columns; ++c)
    {
        const double s0 = startSec + (double) c * perCol;
        const double s1 = s0 + perCol;
        if (s1 < 0.0 || s0 >= dur)
            continue;

        int b0 = (int) (juce::jmax (0.0, s0) * fileSampleRate / peakBlock);
        int b1 = (int) (s1 * fileSampleRate / peakBlock);
        b0 = juce::jlimit (0, nb - 1, b0);
        b1 = juce::jlimit (b0, nb - 1, b1);

        float mn = 0.0f, mx = 0.0f;
        for (int b = b0; b <= b1; ++b)
        {
            mn = juce::jmin (mn, peakMin[(size_t) b]);
            mx = juce::jmax (mx, peakMax[(size_t) b]);
        }
        mins[(size_t) c] = mn;
        maxs[(size_t) c] = mx;
    }
}

//==============================================================================
PadSlice TongaProcessor::getSlice (int pad)
{
    const juce::ScopedLock sl (dataLock);
    return slices[(size_t) juce::jlimit (0, numPads - 1, pad)];
}

void TongaProcessor::setSlice (int pad, const PadSlice& s)
{
    const juce::ScopedLock sl (dataLock);
    slices[(size_t) juce::jlimit (0, numPads - 1, pad)] = s;
}

void TongaProcessor::clearPad (int pad)
{
    const juce::ScopedLock sl (dataLock);
    auto& s = slices[(size_t) juce::jlimit (0, numPads - 1, pad)];
    s.valid = false;
    voices[(size_t) juce::jlimit (0, numPads - 1, pad)].active = false;
}

uint32_t TongaProcessor::getValidMask()
{
    const juce::ScopedLock sl (dataLock);
    uint32_t m = 0;
    for (int i = 0; i < numPads; ++i)
        if (slices[(size_t) i].valid)
            m |= (1u << i);
    return m;
}

void TongaProcessor::applySlicing (int mode)
{
    lastMode.store (mode);
    if (! hasSample())
        return;

    const double dur = getLengthSec();
    const double orig = juce::jmax (1.0, originalBpm.load());
    const double beat = 60.0 / orig;
    std::array<PadSlice, numPads> out;

    if (mode == modeSlicer)
    {
        double start0 = juce::jlimit (0.0, dur, gridOffset.load());
        double region = dur - start0;
        if (region < 0.05) { start0 = 0.0; region = dur; }

        const double size = (double) sizeParam->load();
        const double step = region / (double) numPads;

        for (int i = 0; i < numPads; ++i)
        {
            const double len = step * size;
            const double centre = start0 + step * ((double) i + 0.5);
            const double s = juce::jmax (0.0, centre - len * 0.5);
            const double e = juce::jmin (dur, centre + len * 0.5);
            if (e - s > 0.002)
            {
                out[(size_t) i].valid = true;
                out[(size_t) i].start = s;
                out[(size_t) i].end = e;
            }
        }
    }
    else if (mode == modeTransient)
    {
        auto onsets = findOnsets (sample, fileSampleRate);
        std::sort (onsets.begin(), onsets.end(),
                   [] (const Onset& a, const Onset& b) { return a.strength > b.strength; });
        if ((int) onsets.size() > numPads)
            onsets.resize ((size_t) numPads);

        std::vector<double> times;
        for (auto& o : onsets) times.push_back (o.time);
        std::sort (times.begin(), times.end());

        for (size_t i = 0; i < times.size(); ++i)
        {
            const double t = times[i];
            const double s = juce::jmax (0.0, t - 0.004);
            const double e = (i + 1 < times.size()) ? times[i + 1] : juce::jmin (dur, t + 4.0 * beat);
            if (e - s > 0.002)
            {
                out[i].valid = true;
                out[i].start = s;
                out[i].end = e;
            }
        }
    }
    else if (mode == modeSamples)
    {
        auto regions = findRegions (sample, fileSampleRate);
        std::sort (regions.begin(), regions.end(),
                   [] (const Region& a, const Region& b) { return a.score > b.score; });
        if ((int) regions.size() > numPads)
            regions.resize ((size_t) numPads);
        std::sort (regions.begin(), regions.end(),
                   [] (const Region& a, const Region& b) { return a.start < b.start; });

        for (size_t i = 0; i < regions.size(); ++i)
        {
            out[i].valid = true;
            out[i].start = juce::jmax (0.0, regions[i].start - 0.005);
            out[i].end = juce::jmin (dur, regions[i].end + 0.01);
        }
    }
    else // modeRandom
    {
        juce::Random rng;
        const double fractions[4] = { 0.25, 0.5, 1.0, 2.0 };
        for (int i = 0; i < numPads; ++i)
        {
            double len = beat * fractions[rng.nextInt (4)];
            len = juce::jmin (len, dur * 0.5);
            const double s = rng.nextDouble() * juce::jmax (0.0, dur - len);
            out[(size_t) i].valid = true;
            out[(size_t) i].start = s;
            out[(size_t) i].end = s + len;
        }
    }

    const juce::ScopedLock sl (dataLock);
    for (int i = 0; i < numPads; ++i)
    {
        out[(size_t) i].gain = slices[(size_t) i].gain;
        out[(size_t) i].reverse = slices[(size_t) i].reverse;
        slices[(size_t) i] = out[(size_t) i];
    }
}

//==============================================================================
void TongaProcessor::triggerPad (int pad)
{
    if (pad >= 0 && pad < numPads)
        pendingMask.fetch_or (1u << pad);
}

void TongaProcessor::startVoice (int pad, int delaySamples)
{
    // Se llama con dataLock tomado (desde processBlock)
    const auto& sl = slices[(size_t) pad];
    if (! sl.valid || sample.getNumSamples() == 0)
        return;

    auto& v = voices[(size_t) pad];
    v.startS = juce::jmax (0.0, sl.start * fileSampleRate);
    v.endS = juce::jmin ((double) sample.getNumSamples(), sl.end * fileSampleRate);
    if (v.endS - v.startS < 2.0)
        return;

    v.gain = sl.gain;
    v.rev = sl.reverse;
    v.pos = v.rev ? v.endS - 1.0 : v.startS;
    v.delay = juce::jmax (0, delaySamples);
    v.active = true;

    lastPlayedPad.store (pad);
    playCounter.fetch_add (1);
}

void TongaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Tempo del DAW
    double hostBpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                hostBpm = *b;

    const bool sync = syncParam->load() > 0.5f;
    const double applied = (sync && hostBpm > 1.0) ? hostBpm : (double) bpmParam->load();
    appliedBpm.store (applied);

    const uint32_t uiTriggers = pendingMask.exchange (0);

    const juce::ScopedTryLock tryLock (dataLock);
    if (! tryLock.isLocked())
        return;

    // Eventos: UI y MIDI (notas 36 a 51 = pads 1 a 16)
    for (int i = 0; i < numPads; ++i)
        if (uiTriggers & (1u << i))
            startVoice (i, 0);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int pad = m.getNoteNumber() - 36;
            if (pad >= 0 && pad < numPads)
                startVoice (pad, meta.samplePosition);
        }
    }

    const int n = buffer.getNumSamples();
    const int numSampleSamples = sample.getNumSamples();
    if (numSampleSamples == 0)
        return;

    const double ratio = applied / juce::jmax (1.0, originalBpm.load());
    const double step = ratio * fileSampleRate / hostSampleRate;
    const float master = volumeParam->load();

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const int chs = sample.getNumChannels();
    const float* s0 = sample.getReadPointer (0);
    const float* s1 = chs > 1 ? sample.getReadPointer (1) : s0;

    uint32_t playing = 0;

    for (int pad = 0; pad < numPads; ++pad)
    {
        auto& v = voices[(size_t) pad];
        if (! v.active)
            continue;

        for (int i = 0; i < n; ++i)
        {
            if (v.delay > 0)
            {
                --v.delay;
                continue;
            }

            const double p = v.pos;
            if (p < v.startS || p >= v.endS)
            {
                v.active = false;
                break;
            }

            const int i0 = juce::jlimit (0, numSampleSamples - 1, (int) p);
            const int i1 = juce::jmin (i0 + 1, numSampleSamples - 1);
            const float fr = (float) (p - (double) i0);

            const float l = s0[i0] + (s0[i1] - s0[i0]) * fr;
            const float r = s1[i0] + (s1[i1] - s1[i0]) * fr;

            const float dist = (float) juce::jmin (p - v.startS, v.endS - p);
            const float fade = juce::jmin (1.0f, dist / 64.0f);
            const float g = v.gain * master * fade;

            outL[i] += l * g;
            if (outR != nullptr)
                outR[i] += r * g;

            v.pos += v.rev ? -step : step;
        }

        if (v.active)
            playing |= (1u << pad);
    }

    playingMask.store (playing);

    const int lp = lastPlayedPad.load();
    if (lp >= 0 && lp < numPads && voices[(size_t) lp].active)
        lastPlayPosSec.store (voices[(size_t) lp].pos / fileSampleRate);
    else
        lastPlayPosSec.store (-1.0);
}

//==============================================================================
void TongaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    juce::String sl;
    {
        const juce::ScopedLock lock (dataLock);
        for (auto& s : slices)
            sl << (s.valid ? 1 : 0) << "," << s.start << "," << s.end << ","
               << s.gain << "," << (s.reverse ? 1 : 0) << ";";
        state.setProperty ("samplePath", samplePath, nullptr);
    }

    state.setProperty ("origBpm", originalBpm.load(), nullptr);
    state.setProperty ("gridOffset", gridOffset.load(), nullptr);
    state.setProperty ("slices", sl, nullptr);
    state.setProperty ("lastMode", lastMode.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TongaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr)
        return;

    auto vt = juce::ValueTree::fromXml (*xml);
    if (! vt.isValid())
        return;

    apvts.replaceState (vt);

    const juce::String path = vt.getProperty ("samplePath", "").toString();
    if (path.isNotEmpty() && juce::File (path).existsAsFile())
        loadFile (juce::File (path), false);

    originalBpm.store ((double) vt.getProperty ("origBpm", 120.0));
    gridOffset.store ((double) vt.getProperty ("gridOffset", 0.0));
    lastMode.store ((int) vt.getProperty ("lastMode", 0));

    const juce::String sl = vt.getProperty ("slices", "").toString();
    if (sl.isNotEmpty() && hasSample())
    {
        auto items = juce::StringArray::fromTokens (sl, ";", "");
        const juce::ScopedLock lock (dataLock);
        for (int i = 0; i < juce::jmin (numPads, items.size()); ++i)
        {
            auto f = juce::StringArray::fromTokens (items[i], ",", "");
            if (f.size() >= 5)
            {
                auto& s = slices[(size_t) i];
                s.valid = f[0].getIntValue() != 0;
                s.start = f[1].getDoubleValue();
                s.end = f[2].getDoubleValue();
                s.gain = (float) f[3].getDoubleValue();
                s.reverse = f[4].getIntValue() != 0;
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TongaProcessor();
}
