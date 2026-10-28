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
                                                             juce::NormalisableRange<float> (40.0f, 300.0f, 0.1f), 120.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "timesig", 1 }, "Compas",
                                                              juce::StringArray { "2/4", "3/4", "4/4", "6/8", "9/8", "12/8" }, 2));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "division", 1 }, "Division del slicer",
                                                              juce::StringArray { "1/4", "1/2", "1", "2", "4" }, 2));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "poly", 1 }, "Poly", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "hold", 1 }, "Mantener", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "volume", 1 }, "Volumen",
                                                             juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "gkey", 1 }, "Key", -12, 12, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gdetune", 1 }, "Detune",
                                                             juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f));
    return layout;
}

TongaProcessor::TongaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();

    syncParam     = apvts.getRawParameterValue ("sync");
    bpmParam      = apvts.getRawParameterValue ("bpm");
    divisionParam = apvts.getRawParameterValue ("division");
    polyParam     = apvts.getRawParameterValue ("poly");
    holdParam     = apvts.getRawParameterValue ("hold");
    volumeParam   = apvts.getRawParameterValue ("volume");
    gkeyParam     = apvts.getRawParameterValue ("gkey");
    gdetuneParam  = apvts.getRawParameterValue ("gdetune");

    buildTables (44100.0);
}

void TongaProcessor::buildTables (double sr)
{
    hop = juce::jmax (64, (int) (0.025 * sr));
    grainLen = hop * 2;
    hannWin.assign ((size_t) grainLen, 0.0f);
    firstWin.assign ((size_t) grainLen, 0.0f);

    const double pi = juce::MathConstants<double>::pi;
    for (int a = 0; a < grainLen; ++a)
    {
        hannWin[(size_t) a] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * (double) a / (double) grainLen));
        firstWin[(size_t) a] = a < hop ? 1.0f
                                       : (float) (0.5 + 0.5 * std::cos (pi * (double) (a - hop) / (double) hop));
    }
}

void TongaProcessor::prepareToPlay (double sampleRate, int)
{
    hostSampleRate = sampleRate;
    buildTables (sampleRate);
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
        samplePath = file.getFullPathName();
        sampleLengthSec.store ((double) len / fileSampleRate);
        for (auto& v : voices) v.active = false;
        for (auto& sl2 : slices) sl2 = PadSlice();
        cursorSec.store (0.0);
    }

    {
        const juce::ScopedLock pl (peakLock);
        peakMin = std::move (mins);
        peakMax = std::move (maxs);
    }

    if (detectGrid)
        detectTempo();
    return true;
}

void TongaProcessor::getPeaks (double startSec, double lenSec, int columns,
                               std::vector<float>& mins, std::vector<float>& maxs)
{
    mins.assign ((size_t) juce::jmax (0, columns), 0.0f);
    maxs.assign ((size_t) juce::jmax (0, columns), 0.0f);

    const juce::ScopedLock sl (peakLock);
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
    const int p = juce::jlimit (0, numPads - 1, pad);
    slices[(size_t) p] = PadSlice();
    voices[(size_t) p].active = false;
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
        const double steps[5] = { beat * 0.25, beat * 0.5, beat, beat * 2.0, beat * 4.0 };
        const int di = juce::jlimit (0, 4, (int) divisionParam->load());
        const double step = steps[di];
        const double off = gridOffset.load();
        const double cur = cursorSec.load();

        double start0 = off + std::ceil ((cur - off) / step - 1.0e-6) * step;
        while (start0 < 0.0)
            start0 += step;

        for (int i = 0; i < numPads; ++i)
        {
            const double st = start0 + step * (double) i;
            if (st >= dur - 0.002)
                break;
            const double en = juce::jmin (dur, st + step);
            if (en - st > 0.002)
            {
                out[(size_t) i].valid = true;
                out[(size_t) i].start = st;
                out[(size_t) i].end = en;
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
        slices[(size_t) i].valid = out[(size_t) i].valid;
        slices[(size_t) i].start = out[(size_t) i].start;
        slices[(size_t) i].end = out[(size_t) i].end;
    }
}

//==============================================================================
void TongaProcessor::triggerPad (int pad)
{
    if (pad >= 0 && pad < numPads)
        pendingMask.fetch_or (1u << pad);
}

void TongaProcessor::releasePad (int pad)
{
    if (pad >= 0 && pad < numPads)
        pendingRelease.fetch_or (1u << pad);
}

void TongaProcessor::toggleAudition()
{
    if (! hasSample())
        return;
    auditionCmd.store (auditioning.load() ? 2 : 1);
}

void TongaProcessor::initVoice (Voice& v, double startS, double endS, bool rev, int delaySamples)
{
    v.active = true;
    v.releasing = false;
    v.ended = false;
    v.rev = rev;
    v.startS = startS;
    v.endS = endS;
    v.anchor = rev ? endS - 1.0 : startS;
    v.delay = juce::jmax (0, delaySamples);
    v.sinceSpawn = 0;
    v.spawned = 0;
    v.lastGrain = -1;
    v.env = 0.0f;
    v.relInc = 0.0f;
    for (int c = 0; c < 2; ++c)
    {
        v.ic1[c] = 0.0f;
        v.ic2[c] = 0.0f;
    }
    for (auto& g : v.grains)
        g.on = false;
}

void TongaProcessor::releaseVoice (Voice& v, double seconds)
{
    if (! v.active || v.releasing)
        return;

    const double samples = juce::jmax (0.003, seconds) * hostSampleRate;
    v.relInc = v.env / (float) samples;
    if (v.relInc <= 0.0f)
    {
        v.active = false;
        return;
    }
    v.releasing = true;
}

bool TongaProcessor::startVoice (int pad, int delaySamples)
{
    // Se llama con dataLock tomado (desde processBlock)
    const int total = sample.getNumSamples();
    if (total == 0)
        return false;

    auto& sl = slices[(size_t) pad];

    // Pad vacio: se asigna desde el cursor hasta el final del audio
    if (! sl.valid)
    {
        const double dur = sampleLengthSec.load();
        const double c = juce::jlimit (0.0, juce::jmax (0.0, dur - 0.05), cursorSec.load());
        sl.valid = true;
        sl.start = c;
        sl.end = dur;
    }

    const double startS = juce::jmax (0.0, sl.start * fileSampleRate);
    const double endS = juce::jmin ((double) total, sl.end * fileSampleRate);
    if (endS - startS < 2.0)
        return false;

    // Mono: un solo sample a la vez, el nuevo corta a los anteriores
    if (polyParam->load() < 0.5f)
        for (int o = 0; o < numPads; ++o)
            if (o != pad && voices[(size_t) o].active)
                releaseVoice (voices[(size_t) o], 0.005);

    initVoice (voices[(size_t) pad], startS, endS, sl.reverse, delaySamples);

    lastPlayedPad.store (pad);
    playCounter.fetch_add (1);
    return true;
}

void TongaProcessor::renderVoice (Voice& v, const PadSlice& sp, float* outL, float* outR, int n,
                                  double tempoRatio, float master, double globalSemis)
{
    const double srcPerOut = fileSampleRate / hostSampleRate;
    const double speed = juce::jlimit (0.25, 4.0, tempoRatio * (1.0 + (double) sp.stretch / 100.0));
    const double pitch = std::pow (2.0, ((double) sp.keyShift + globalSemis) / 12.0);
    const double dir = v.rev ? -1.0 : 1.0;
    const double gStep = pitch * srcPerOut * dir;
    const double anchorStep = (double) hop * speed * srcPerOut * dir;
    const float gain = sp.gain * master;
    const float attInc = (float) (1.0 / (juce::jmax (0.0015, (double) sp.attack) * hostSampleRate));

    // Filtro (pasa bajos a la izquierda, pasa altos a la derecha)
    const bool useFilter = std::abs (sp.filter) >= 0.02f;
    const bool hp = sp.filter > 0.0f;
    float fa1 = 0.0f, fa2 = 0.0f, fa3 = 0.0f, fk = 1.4142f;
    if (useFilter)
    {
        double fc = hp ? 20.0 * std::pow (8000.0 / 20.0, (double) sp.filter)
                       : 20000.0 * std::pow (200.0 / 20000.0, (double) -sp.filter);
        fc = juce::jmin (fc, hostSampleRate * 0.45);
        const double gg = std::tan (juce::MathConstants<double>::pi * fc / hostSampleRate);
        const double kk = 1.4142;
        const double a1d = 1.0 / (1.0 + gg * (gg + kk));
        fa1 = (float) a1d;
        fa2 = (float) (gg * a1d);
        fa3 = (float) (gg * gg * a1d);
        fk = (float) kk;
    }

    auto svf = [&v, fa1, fa2, fa3, fk, hp] (float x, int c)
    {
        const float v3 = x - v.ic2[c];
        const float v1 = fa1 * v.ic1[c] + fa2 * v3;
        const float v2 = v.ic2[c] + fa2 * v.ic1[c] + fa3 * v3;
        v.ic1[c] = 2.0f * v1 - v.ic1[c];
        v.ic2[c] = 2.0f * v2 - v.ic2[c];
        return hp ? (x - fk * v1 - v2) : v2;
    };

    const int total = sample.getNumSamples();
    const int chs = sample.getNumChannels();
    const float* s0 = sample.getReadPointer (0);
    const float* s1 = chs > 1 ? sample.getReadPointer (1) : s0;

    // Alinea el grano nuevo con la continuacion del anterior (evita el sonido "temblando")
    auto bestOffset = [&] (const Grain& prev) -> double
    {
        const int maxD = juce::jmax (8, (int) (0.005 * hostSampleRate));
        const int len = hop;
        const int stepJ = 4;
        double bestC = -1.0e30;
        double bestD = 0.0;

        auto evalD = [&] (double d)
        {
            double cross = 0.0, e1 = 1.0e-9, e2 = 1.0e-9;
            for (int j = 0; j < len; j += stepJ)
            {
                const double pp = prev.pos + (double) j * gStep;
                const double pc = v.anchor + d + (double) j * gStep;
                if (pp < v.startS || pp >= v.endS || pc < v.startS || pc >= v.endS)
                    continue;
                const float a = s0[(int) pp];
                const float b = s0[(int) pc];
                cross += (double) a * (double) b;
                e1 += (double) a * (double) a;
                e2 += (double) b * (double) b;
            }
            const double c = cross / std::sqrt (e1 * e2);
            if (c > bestC + 1.0e-9)
            {
                bestC = c;
                bestD = d;
            }
        };

        for (int k = 0; k <= maxD; k += 2)
        {
            evalD ((double) k);
            if (k > 0)
                evalD ((double) -k);
        }
        return bestD;
    };

    for (int i = 0; i < n; ++i)
    {
        if (v.delay > 0)
        {
            --v.delay;
            continue;
        }

        // Nuevo grano cada "hop" muestras
        if (! v.ended && v.sinceSpawn <= 0)
        {
            if (v.anchor < v.startS || v.anchor >= v.endS)
            {
                v.ended = true;
            }
            else
            {
                double startPos = v.anchor;
                if (v.spawned > 0 && v.lastGrain >= 0 && v.grains[v.lastGrain].on)
                    startPos += bestOffset (v.grains[v.lastGrain]);
                startPos = juce::jlimit (v.startS, v.endS - 1.0, startPos);

                for (int gi = 0; gi < 3; ++gi)
                {
                    auto& gr = v.grains[gi];
                    if (! gr.on)
                    {
                        gr.on = true;
                        gr.first = (v.spawned == 0);
                        gr.pos = startPos;
                        gr.age = 0;
                        v.lastGrain = gi;
                        break;
                    }
                }
                ++v.spawned;
                v.anchor += anchorStep;
                v.sinceSpawn = hop;
            }
        }
        --v.sinceSpawn;

        float l = 0.0f, r = 0.0f;
        bool any = false;

        for (auto& gr : v.grains)
        {
            if (! gr.on)
                continue;
            any = true;

            const double p = gr.pos;
            if (p >= v.startS && p < v.endS)
            {
                const int i0 = juce::jlimit (0, total - 1, (int) p);
                const int i1 = juce::jmin (i0 + 1, total - 1);
                const float fr = (float) (p - (double) i0);
                const float dist = (float) juce::jmin (p - v.startS, v.endS - p);
                const float w = (gr.first ? firstWin[(size_t) gr.age] : hannWin[(size_t) gr.age])
                                * juce::jmin (1.0f, dist / 64.0f);
                l += w * (s0[i0] + (s0[i1] - s0[i0]) * fr);
                r += w * (s1[i0] + (s1[i1] - s1[i0]) * fr);
            }

            gr.pos += gStep;
            if (++gr.age >= grainLen)
                gr.on = false;
        }

        if (! any && v.ended)
        {
            v.active = false;
            break;
        }

        if (useFilter)
        {
            l = svf (l, 0);
            r = svf (r, 1);
        }

        if (v.releasing)
        {
            v.env -= v.relInc;
            if (v.env <= 0.0f)
            {
                v.active = false;
                break;
            }
        }
        else if (v.env < 1.0f)
        {
            v.env = juce::jmin (1.0f, v.env + attInc);
        }

        const float g = gain * v.env;
        outL[i] += l * g;
        if (outR != nullptr)
            outR[i] += r * g;
    }
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

    // Si la interfaz esta usando los datos en este instante, se saltea este bloque.
    // Los pedidos pendientes (tocar / soltar un pad) NO se pierden: quedan para el siguiente.
    const juce::ScopedTryLock tryLock (dataLock);
    if (! tryLock.isLocked())
        return;

    const uint32_t uiTriggers = pendingMask.exchange (0);
    const uint32_t uiReleases = pendingRelease.exchange (0);
    const int auditionNow = auditionCmd.exchange (0);

    const bool hold = holdParam->load() > 0.5f;
    const int total = sample.getNumSamples();

    uint32_t started = 0;   // pads que arrancaron en este bloque
    uint32_t deferred = 0;  // "soltar" que llegaron junto con el "tocar": se aplican en el bloque siguiente

    // Eventos: UI y MIDI (notas 36 a 67 = pads 1 a 32)
    for (int i = 0; i < numPads; ++i)
        if (uiTriggers & (1u << i))
            if (startVoice (i, 0))
                started |= (1u << i);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int pad = m.getNoteNumber() - 36;
            if (pad >= 0 && pad < numPads)
                if (startVoice (pad, meta.samplePosition))
                    started |= (1u << pad);
        }
        else if (m.isNoteOff() && hold)
        {
            const int pad = m.getNoteNumber() - 36;
            if (pad >= 0 && pad < numPads)
            {
                if (started & (1u << pad))
                    deferred |= (1u << pad);
                else
                    releaseVoice (voices[(size_t) pad], (double) slices[(size_t) pad].release);
            }
        }
    }

    if (hold)
    {
        for (int i = 0; i < numPads; ++i)
        {
            if (uiReleases & (1u << i))
            {
                if (started & (1u << i))
                    deferred |= (1u << i);
                else
                    releaseVoice (voices[(size_t) i], (double) slices[(size_t) i].release);
            }
        }
    }

    if (deferred != 0)
        pendingRelease.fetch_or (deferred);

    // Play / pausa del audio completo
    if (total > 0)
    {
        const double dur = sampleLengthSec.load();
        auto& av = voices[(size_t) numPads];

        if (auditionNow == 1)
        {
            const double c = juce::jlimit (0.0, juce::jmax (0.0, dur - 0.01), cursorSec.load());
            initVoice (av, c * fileSampleRate, (double) total, false, 0);
        }
        else if (auditionNow == 2 && av.active)
        {
            cursorSec.store (juce::jlimit (0.0, dur, av.anchor / fileSampleRate));
            releaseVoice (av, 0.01);
        }
    }

    if (total == 0)
        return;

    const double ratio = applied / juce::jmax (1.0, originalBpm.load());
    const float master = volumeParam->load();
    const double globalSemis = (double) gkeyParam->load() + (double) gdetuneParam->load() / 100.0;

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const int n = buffer.getNumSamples();

    uint32_t playing = 0;
    for (int pad = 0; pad < numPads; ++pad)
    {
        auto& v = voices[(size_t) pad];
        if (! v.active)
            continue;

        renderVoice (v, slices[(size_t) pad], outL, outR, n, ratio, master, globalSemis);
        if (v.active)
            playing |= (1u << pad);
    }

    auto& av = voices[(size_t) numPads];
    if (av.active)
        renderVoice (av, auditionParams, outL, outR, n, ratio, master, globalSemis);

    playingMask.store (playing);
    auditioning.store (av.active);
    if (av.active)
        auditionPosSec.store (av.anchor / fileSampleRate);

    const int lp = lastPlayedPad.load();
    if (lp >= 0 && lp < numPads && voices[(size_t) lp].active)
        lastPlayPosSec.store (voices[(size_t) lp].anchor / fileSampleRate);
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
               << s.gain << "," << (s.reverse ? 1 : 0) << ","
               << s.keyShift << "," << s.stretch << ","
               << s.filter << "," << s.attack << "," << s.release << ";";
        state.setProperty ("samplePath", samplePath, nullptr);
    }

    state.setProperty ("origBpm", originalBpm.load(), nullptr);
    state.setProperty ("gridOffset", gridOffset.load(), nullptr);
    state.setProperty ("cursor", cursorSec.load(), nullptr);
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
    lastMode.store ((int) vt.getProperty ("lastMode", -1));
    if (hasSample())
        setCursor ((double) vt.getProperty ("cursor", 0.0));

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
                if (f.size() == 10)
                {
                    s.keyShift = f[5].getIntValue();
                    s.stretch = (float) f[6].getDoubleValue();
                    s.filter = (float) f[7].getDoubleValue();
                    s.attack = (float) f[8].getDoubleValue();
                    s.release = (float) f[9].getDoubleValue();
                }
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TongaProcessor();
}
