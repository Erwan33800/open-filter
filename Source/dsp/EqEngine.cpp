#include "EqEngine.h"

#include <algorithm>
#include <cmath>

namespace openfilter::dsp
{
namespace
{
    double lerpLog (double from, double to, double a) noexcept
    {
        from = std::max (from, 1.0e-6);
        to   = std::max (to, 1.0e-6);
        return std::exp (std::log (from) + (std::log (to) - std::log (from)) * a);
    }

    double moveToward (double value, double target, double maxStep) noexcept
    {
        const double d = target - value;
        return std::abs (d) <= maxStep ? target : value + (d > 0.0 ? maxStep : -maxStep);
    }

    bool needsMidSide (Placement p) noexcept { return p == Placement::Mid || p == Placement::Side; }
}

void EqEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate  = newSampleRate;
    maxBlock    = std::max (1, maxBlockSize);
    rampSamples = std::max (1.0, kRampSeconds * sampleRate);
    rampSteps   = std::max (1.0, rampSamples / kControlBlock);

    work[0].assign ((size_t) maxBlock, 0.0);
    work[1].assign ((size_t) maxBlock, 0.0);

    for (auto& b : bands)
        b = Band {};

    gainCur = gainTarget = 1.0;
    gainSamplesLeft = 0.0;
    bypassMix = bypassTarget = 1.0;
    statesClear = true;
}

void EqEngine::reset() noexcept
{
    resetStates();
    gainSamplesLeft = 0.0;
    gainCur = gainTarget;
    bypassMix = bypassTarget;

    for (auto& b : bands)
    {
        b.mix = b.mixTarget;
        b.stepsLeft = 0.0;
        snap (b);
    }
}

void EqEngine::resetStates() noexcept
{
    for (auto& b : bands)
        for (auto& stage : b.biquads)
            for (auto& bq : stage)
                bq.reset();

    statesClear = true;
}

void EqEngine::snap (Band& b) noexcept
{
    b.curFreq = b.target.filter.freq;
    b.curGain = b.target.filter.gainDb;
    b.curQ    = b.target.filter.q;
    b.stepsLeft = 0.0;
    redesign (b);
}

void EqEngine::redesign (Band& b) noexcept
{
    if (b.designed && b.dFreq == b.curFreq && b.dGain == b.curGain && b.dQ == b.curQ)
        return;

    FilterParams p = b.target.filter;
    p.freq = b.curFreq;
    p.gainDb = b.curGain;
    p.q = b.curQ;

    const FilterStages s = designFilter (p, sampleRate);

    if (s.count != b.stages.count)
        for (auto& stage : b.biquads)
            for (auto& bq : stage)
                bq.reset();

    for (int k = 0; k < s.count; ++k)
        for (auto& bq : b.biquads[(size_t) k])
            bq.setCoeffs (s.stage[(size_t) k]);

    b.stages = s;
    b.designed = true;
    b.dFreq = b.curFreq;
    b.dGain = b.curGain;
    b.dQ = b.curQ;
}

void EqEngine::setBand (int index, const BandSettings& s) noexcept
{
    if (index < 0 || index >= kNumBands)
        return;

    Band& b = bands[(size_t) index];
    const bool active = s.used && s.on;

    if (! b.initialised)
    {
        b.target = s;
        b.initialised = true;
        b.mix = b.mixTarget = active ? 1.0 : 0.0;
        snap (b);
        return;
    }

    const FilterParams& f = s.filter;
    const FilterParams& old = b.target.filter;

    const bool structural = f.type != old.type
                         || clampSlope (f.slopeDb) != clampSlope (old.slopeDb)
                         || s.placement != b.target.placement;
    const bool moved = f.freq != old.freq || f.gainDb != old.gainDb || f.q != old.q;

    b.target = s;
    b.mixTarget = active ? 1.0 : 0.0;

    if (structural)
    {
        for (auto& stage : b.biquads)
            for (auto& bq : stage)
                bq.reset();

        b.designed = false;
        snap (b);
    }
    else if (moved)
    {
        b.stepsLeft = rampSteps;
    }
}

void EqEngine::setOutput (double gainDb, bool invertPhase) noexcept
{
    const double lin = gainDb <= -99.9 ? 0.0 : std::pow (10.0, gainDb / 20.0);
    const double target = invertPhase ? -lin : lin;

    if (target != gainTarget)
    {
        gainTarget = target;
        gainSamplesLeft = rampSamples;
    }
}

void EqEngine::setBypass (bool bypass) noexcept
{
    bypassTarget = bypass ? 0.0 : 1.0;
}

void EqEngine::controlStep (Band& b, double steps) noexcept
{
    if (b.stepsLeft > 0.0)
    {
        const double a = std::min (1.0, steps / b.stepsLeft);
        const FilterParams& t = b.target.filter;

        b.curFreq = lerpLog (b.curFreq, t.freq, a);
        b.curQ    = lerpLog (b.curQ, t.q, a);
        b.curGain += (t.gainDb - b.curGain) * a;
        b.stepsLeft -= steps;

        if (a >= 1.0 || b.stepsLeft <= 1.0e-9)
        {
            b.curFreq = t.freq;
            b.curQ    = t.q;
            b.curGain = t.gainDb;
            b.stepsLeft = 0.0;
        }
    }

    redesign (b);
}

void EqEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numChannels < 1 || numSamples <= 0)
        return;

    for (int done = 0; done < numSamples;)
    {
        const int len = std::min (numSamples - done, maxBlock);
        processChunk (channels, std::min (numChannels, 2), done, len);
        done += len;
    }
}

void EqEngine::processChunk (float* const* channels, int numChannels, int offset, int length) noexcept
{
    const bool bypassSteady = bypassMix == bypassTarget;

    // Bypass complet : sortie = entrée, états remis à zéro pour repartir proprement à la sortie du bypass.
    if (bypassSteady && bypassTarget == 0.0)
    {
        if (! statesClear)
            resetStates();
        return;
    }

    bool bandsIdle = true;

    for (const auto& b : bands)
        if (b.initialised && (b.mix > 0.0 || b.mixTarget > 0.0))
            bandsIdle = false;

    const bool gainIdle = gainSamplesLeft <= 0.0 && gainCur == 1.0 && gainTarget == 1.0;

    // Rien à faire : le tampon reste intact, donc identique à l'entrée au bit près.
    if (bandsIdle && gainIdle && bypassSteady)
        return;

    statesClear = false;

    double* w[2] = { work[0].data(), work[1].data() };

    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < length; ++i)
            w[c][i] = (double) channels[c][offset + i];

    const double mixStepPerSample = 1.0 / rampSamples;

    for (int start = 0; start < length; start += kControlBlock)
    {
        const int len = std::min (kControlBlock, length - start);
        double* p0 = w[0] + start;
        double* p1 = numChannels > 1 ? w[1] + start : nullptr;
        bool inMidSide = false;

        const auto toMidSide = [&]()
        {
            for (int i = 0; i < len; ++i)
            {
                const double l = p0[i], r = p1[i];
                p0[i] = (l + r) * 0.5;
                p1[i] = (l - r) * 0.5;
            }
            inMidSide = true;
        };

        const auto toLeftRight = [&]()
        {
            for (int i = 0; i < len; ++i)
            {
                const double m = p0[i], s = p1[i];
                p0[i] = m + s;
                p1[i] = m - s;
            }
            inMidSide = false;
        };

        for (auto& b : bands)
        {
            if (! b.initialised || (b.mix <= 0.0 && b.mixTarget <= 0.0))
                continue;

            controlStep (b, (double) len / kControlBlock);

            const double mixStart = b.mix;
            const double mixEnd = moveToward (mixStart, b.mixTarget, mixStepPerSample * len);
            b.mix = mixEnd;

            if (b.stages.count == 0)
                continue; // bande neutre (0 dB) : le signal passe sans être touché

            // Canaux traités : en mono, toujours l'unique canal ; en stéréo, selon le placement.
            bool slot[2] = { true, numChannels > 1 };
            const Placement place = b.target.placement;

            if (numChannels > 1)
            {
                slot[0] = place != Placement::Right && place != Placement::Side;
                slot[1] = place != Placement::Left && place != Placement::Mid;

                // On ne convertit en Mid/Side que si une bande active en a besoin.
                if (needsMidSide (place) && ! inMidSide)
                    toMidSide();
                else if (! needsMidSide (place) && inMidSide)
                    toLeftRight();
            }

            for (int c = 0; c < numChannels; ++c)
            {
                if (! slot[c])
                    continue;

                double* x = c == 0 ? p0 : p1;

                if (mixStart >= 1.0 && mixEnd >= 1.0)
                {
                    for (int i = 0; i < len; ++i)
                    {
                        double y = x[i];

                        for (int k = 0; k < b.stages.count; ++k)
                            y = b.biquads[(size_t) k][(size_t) c].process (y);

                        x[i] = y;
                    }
                }
                else
                {
                    for (int i = 0; i < len; ++i)
                    {
                        const double dry = x[i];
                        double y = dry;

                        for (int k = 0; k < b.stages.count; ++k)
                            y = b.biquads[(size_t) k][(size_t) c].process (y);

                        const double m = mixStart + (mixEnd - mixStart) * (double) (i + 1) / (double) len;
                        x[i] = dry + m * (y - dry);
                    }
                }
            }
        }

        if (inMidSide)
            toLeftRight();

        // Gain de sortie (avec inversion de phase), lissé échantillon par échantillon.
        if (gainSamplesLeft > 0.0 || gainCur != 1.0)
        {
            for (int i = 0; i < len; ++i)
            {
                if (gainSamplesLeft > 0.0)
                {
                    gainCur += (gainTarget - gainCur) / gainSamplesLeft;
                    gainSamplesLeft -= 1.0;

                    if (gainSamplesLeft <= 0.0)
                        gainCur = gainTarget;
                }

                p0[i] *= gainCur;

                if (p1 != nullptr)
                    p1[i] *= gainCur;
            }
        }
    }

    // Retour en float, avec le fondu du bypass global.
    const double bypassStep = 1.0 / rampSamples;

    for (int i = 0; i < length; ++i)
    {
        bypassMix = moveToward (bypassMix, bypassTarget, bypassStep);

        for (int c = 0; c < numChannels; ++c)
        {
            float& out = channels[c][offset + i];

            if (bypassMix >= 1.0)
            {
                out = (float) w[c][i];
            }
            else
            {
                const double dry = (double) out;
                out = (float) (dry + bypassMix * (w[c][i] - dry));
            }
        }
    }
}

}
