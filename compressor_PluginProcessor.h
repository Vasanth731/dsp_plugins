/*
  ==============================================================================

    SQUEEZE COMP - compressor pedal

    NO EXTRA MODULES REQUIRED. Uses only the modules present in the default
    Projucer audio plug-in template. All DSP is implemented locally below.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

//==============================================================================
/** Fractional-delay ring buffer. Used here for limiter look-ahead. */
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        buffer.assign ((size_t) juce::jmax (8, maxSamples), 0.0f);
        writePos = 0;
    }

    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    inline void write (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        if (++writePos >= (int) buffer.size())
            writePos = 0;
    }

    inline float read (float delaySamples) const noexcept
    {
        const int size = (int) buffer.size();
        const float d = juce::jlimit (1.0f, (float) size - 2.0f, delaySamples);

        // Integer wrap arithmetic: wrapping in floats can round to exactly
        // size on a large buffer, giving an index one past the end.
        const int   iDelay = (int) d;
        const float frac   = d - (float) iDelay;

        int i0 = writePos - iDelay;
        while (i0 < 0) i0 += size;
        while (i0 >= size) i0 -= size;

        int i1 = i0 - 1;
        while (i1 < 0) i1 += size;

        return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
    }

    int getSize() const noexcept { return (int) buffer.size(); }

private:
    std::vector<float> buffer;
    int writePos = 0;
};

//==============================================================================
class OnePoleLP
{
public:
    void set (double sampleRate, double freq) noexcept
    {
        freq = juce::jlimit (1.0, sampleRate * 0.49, freq);
        b = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * freq / sampleRate);
        a = 1.0f - b;
    }

    void reset() noexcept { z = 0.0f; }

    inline float process (float x) noexcept
    {
        z = a * x + b * z;
        if (! std::isfinite (z) || std::abs (z) < 1.0e-18f) z = 0.0f;
        return z;
    }

private:
    float a = 1.0f, b = 0.0f, z = 0.0f;
};

class OnePoleHP
{
public:
    void set (double sampleRate, double freq) noexcept { lp.set (sampleRate, freq); }
    void reset() noexcept { lp.reset(); }
    inline float process (float x) noexcept { return x - lp.process (x); }

private:
    OnePoleLP lp;
};

//==============================================================================
/** Standard RBJ biquad, transposed direct form II. */
class Biquad
{
public:
    void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        if (! std::isfinite (z1) || std::abs (z1) < 1.0e-18f) z1 = 0.0f;
        if (! std::isfinite (z2) || std::abs (z2) < 1.0e-18f) z2 = 0.0f;
        return y;
    }

    void setLowPass (double sr, double f, double q) noexcept
    {
        double cw, al; prep (sr, f, q, cw, al);
        set ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5, 1.0 + al, -2.0 * cw, 1.0 - al);
    }

    void setHighPass (double sr, double f, double q) noexcept
    {
        double cw, al; prep (sr, f, q, cw, al);
        set ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5, 1.0 + al, -2.0 * cw, 1.0 - al);
    }

    void setPeak (double sr, double f, double q, double gainDb) noexcept
    {
        double cw, al; prep (sr, f, q, cw, al);
        const double A = std::pow (10.0, gainDb / 40.0);
        set (1.0 + al * A, -2.0 * cw, 1.0 - al * A, 1.0 + al / A, -2.0 * cw, 1.0 - al / A);
    }

    void setLowShelf (double sr, double f, double q, double gainDb) noexcept
    {
        double cw, al; prep (sr, f, q, cw, al);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double sq = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1.0) - (A - 1.0) * cw + sq),
             2.0 * A * ((A - 1.0) - (A + 1.0) * cw),
             A * ((A + 1.0) - (A - 1.0) * cw - sq),
             (A + 1.0) + (A - 1.0) * cw + sq,
             -2.0 * ((A - 1.0) + (A + 1.0) * cw),
             (A + 1.0) + (A - 1.0) * cw - sq);
    }

    void setHighShelf (double sr, double f, double q, double gainDb) noexcept
    {
        double cw, al; prep (sr, f, q, cw, al);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double sq = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1.0) + (A - 1.0) * cw + sq),
             -2.0 * A * ((A - 1.0) + (A + 1.0) * cw),
             A * ((A + 1.0) + (A - 1.0) * cw - sq),
             (A + 1.0) - (A - 1.0) * cw + sq,
             2.0 * ((A - 1.0) - (A + 1.0) * cw),
             (A + 1.0) - (A - 1.0) * cw - sq);
    }

private:
    static void prep (double sr, double f, double q, double& cw, double& al) noexcept
    {
        f = juce::jlimit (10.0, sr * 0.49, f);
        q = juce::jmax (0.05, q);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
        cw = std::cos (w0);
        al = std::sin (w0) / (2.0 * q);
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        const double inv = 1.0 / na0;
        b0 = (float) (nb0 * inv); b1 = (float) (nb1 * inv); b2 = (float) (nb2 * inv);
        a1 = (float) (na1 * inv); a2 = (float) (na2 * inv);
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

//==============================================================================
class SmoothedParam
{
public:
    void reset (double sampleRate, double timeMs, float initial) noexcept
    {
        const double samples = juce::jmax (1.0, sampleRate * timeMs * 0.001);
        coeff = (float) std::exp (-1.0 / samples);
        current = target = initial;
    }

    void setTarget (float t) noexcept { target = t; }
    void snap (float v) noexcept { current = target = v; }

    inline float next() noexcept
    {
        current = target + (current - target) * coeff;
        return current;
    }

private:
    float coeff = 0.999f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** Envelope follower with separate attack and release times. Feed it a
    magnitude; it tracks upward at the attack rate and downward at the release
    rate, which is what gives a compressor its characteristic feel. */
class EnvFollower
{
public:
    void setSampleRate (double sr) noexcept { fs = sr; }

    void setTimes (float attackMs, float releaseMs) noexcept
    {
        aCoeff = timeToCoeff (attackMs);
        rCoeff = timeToCoeff (releaseMs);
    }

    void reset() noexcept { env = 0.0f; }

    inline float process (float magnitude) noexcept
    {
        const float c = (magnitude > env) ? aCoeff : rCoeff;
        env = magnitude + c * (env - magnitude);
        if (! std::isfinite (env) || env < 1.0e-18f) env = 0.0f;
        return env;
    }

    float getEnv() const noexcept { return env; }

private:
    float timeToCoeff (float ms) const noexcept
    {
        const double samples = juce::jmax (1.0, fs * (double) juce::jmax (0.01f, ms) * 0.001);
        return (float) std::exp (-1.0 / samples);
    }

    double fs = 48000.0;
    float env = 0.0f, aCoeff = 0.0f, rCoeff = 0.0f;
};

//==============================================================================
/** Decibel helpers. Working in dB keeps a compressor's maths readable: ratio
    and knee are both defined as straight-line segments on a dB-in/dB-out
    graph, which is awkward to express in linear gain. */
inline float linToDb (float x) noexcept
{
    return 20.0f * std::log10 (juce::jmax (1.0e-6f, std::abs (x)));
}

inline float dbToLin (float db) noexcept
{
    return std::pow (10.0f, db * 0.05f);
}

//==============================================================================
/** Per-sample 4x oversampler. A clipper generates harmonics far above the
    input bandwidth; without oversampling those fold back down as aliasing,
    which is the metallic ring that makes naive distortion sound cheap.

    Call it with the nonlinearity as a lambda. */
class Oversampler4x
{
public:
    static constexpr int factor = 4;

    void prepare (double baseSampleRate) noexcept
    {
        const double osRate = baseSampleRate * (double) factor;
        const double cutoff = baseSampleRate * 0.42;
        const double qs[numSections] = { 0.51764, 0.70711, 1.93185 };

        for (int i = 0; i < numSections; ++i)
        {
            up[i].setLowPass (osRate, cutoff, qs[i]);
            down[i].setLowPass (osRate, cutoff, qs[i]);
        }

        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < numSections; ++i) { up[i].reset(); down[i].reset(); }
    }

    template <typename Nonlinearity>
    inline float process (float x, Nonlinearity&& fn) noexcept
    {
        float kept = 0.0f;

        for (int k = 0; k < factor; ++k)
        {
            // Zero-stuff, compensating for the energy the zeros remove
            float v = (k == 0) ? x * (float) factor : 0.0f;

            for (int i = 0; i < numSections; ++i) v = up[i].process (v);

            v = fn (v);

            for (int i = 0; i < numSections; ++i) v = down[i].process (v);

            if (k == 0) kept = v;
        }

        return kept;
    }

private:
    static constexpr int numSections = 3;
    Biquad up[numSections], down[numSections];
};

//==============================================================================
class PedalEngine
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        detL.setSampleRate (sr); detR.setSampleRate (sr);
        toneL.setHighShelf (sr, 2000.0, 0.707, 0.0);
        toneR.setHighShelf (sr, 2000.0, 0.707, 0.0);
        dcL.set (sr, 15.0); dcR.set (sr, 15.0);
        smBlend.reset (sr, 30.0, 1.0f);
        smLevel.reset (sr, 30.0, 1.0f);
        clear();
    }

    void clear() noexcept
    {
        detL.reset(); detR.reset();
        toneL.reset(); toneR.reset(); dcL.reset(); dcR.reset();
        gainDbL = gainDbR = 0.0f;
    }

    void setParam (int i, float v) noexcept { if (i >= 0 && i < numParams) p[i] = v; }
    void setHostTempo (double) noexcept {}

    void updateParameters()
    {
        // Sustain is the pedal-style control: it drives the detector harder,
        // which is the same thing as lowering the threshold. Turning it up
        // squashes more and holds notes longer.
        sustain = juce::jlimit (0.0f, 10.0f, p[0]);
        threshold = juce::jlimit (-60.0f, 0.0f, p[1]) - sustain * 2.6f;

        ratio = juce::jlimit (1.0f, 20.0f, p[2]);
        knee  = juce::jlimit (0.0f, 24.0f, p[5]);

        attackMs  = juce::jlimit (0.1f, 100.0f, p[3]);
        releaseMs = juce::jlimit (10.0f, 1000.0f, p[4]);
        detL.setTimes (attackMs, releaseMs);
        detR.setTimes (attackMs, releaseMs);

        // Tone is a tilt around 2 kHz: one knob, brighter one way, darker the other
        const double toneDb = ((double) juce::jlimit (0.0f, 10.0f, p[6]) - 5.0) * 1.6;
        toneL.setHighShelf (sampleRate, 2000.0, 0.707, toneDb);
        toneR.setHighShelf (sampleRate, 2000.0, 0.707, toneDb);

        // Automatic make-up: without it, turning Sustain up just makes the
        // pedal quieter, which is the opposite of what the control implies.
        // Half the theoretical gain loss is restored; Level trims the rest.
        // threshold is negative and (1/ratio - 1) is negative, so their
        // product is the positive gain the compressor takes out at full level.
        const float autoMakeupDb = juce::jlimit (0.0f, 24.0f,
                                                 threshold * (1.0f / ratio - 1.0f) * 0.5f);

        smBlend.setTarget (juce::jlimit (0.0f, 1.0f, p[7] * 0.01f));
        smLevel.setTarget (dbToLin (juce::jlimit (-24.0f, 24.0f, p[8]) + autoMakeupDb));
    }

    inline void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float blend = smBlend.next();
        const float level = smLevel.next();

        // Detector runs off the louder channel so the stereo image never shifts
        const float mag = juce::jmax (std::abs (inL), std::abs (inR));
        const float envL = detL.process (mag);
        const float envDb = linToDb (envL);

        const float reduction = computeGainDb (envDb);

        gainDbL = reduction;
        const float g = dbToLin (gainDbL);

        float wetL = dcL.process (toneL.process (inL * g));
        float wetR = dcR.process (toneR.process (inR * g));

        outL = (inL * (1.0f - blend) + wetL * blend) * level;
        outR = (inR * (1.0f - blend) + wetR * blend) * level;
    }

    float getGainReductionDb() const noexcept { return gainDbL; }

private:
    /** Classic soft-knee gain computer, worked out on the dB-in/dB-out line. */
    inline float computeGainDb (float envDb) const noexcept
    {
        const float over = envDb - threshold;
        const float slope = 1.0f / ratio - 1.0f;

        if (knee > 0.0f && over > -knee * 0.5f && over < knee * 0.5f)
        {
            // Quadratic blend through the knee region
            const float t = over + knee * 0.5f;
            return slope * t * t / (2.0f * knee);
        }

        return (over <= 0.0f) ? 0.0f : slope * over;
    }

    static constexpr int numParams = 9;
    float p[numParams] = { 4, -18, 4, 15, 150, 6, 5, 100, 0 };

    EnvFollower detL, detR;
    Biquad toneL, toneR;
    OnePoleHP dcL, dcR;
    SmoothedParam smBlend, smLevel;
    double sampleRate = 48000.0;
    float sustain = 4.0f, threshold = -18.0f, ratio = 4.0f, knee = 6.0f;
    float attackMs = 15.0f, releaseMs = 150.0f;
    float gainDbL = 0.0f, gainDbR = 0.0f;
};
//==============================================================================
class NewProjectAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    NewProjectAudioProcessor();
    ~NewProjectAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;

private:
    //==============================================================================
    static constexpr int numEngineParams = 9;
    std::atomic<float>* paramPointers[numEngineParams] = {};
    std::atomic<float>* bypassParam = nullptr;

    PedalEngine engine;                // the engine is stereo-aware internally

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewProjectAudioProcessor)
};
