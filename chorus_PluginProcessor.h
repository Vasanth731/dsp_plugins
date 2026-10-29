/*
  ==============================================================================

    VELVET CHORUS - chorus pedal

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
/** Fractional-delay ring buffer with linear interpolation. */
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

    /** delaySamples of 1.0 returns the most recently written sample. */
    inline float read (float delaySamples) const noexcept
    {
        const int size = (int) buffer.size();
        const float d = juce::jlimit (1.0f, (float) size - 2.0f, delaySamples);

        // Integer wrap arithmetic. Wrapping in floats is unsafe: for a large
        // buffer, (writePos - d) + size can round to exactly size, giving an
        // index one past the end.
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
/** One-pole lowpass. */
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

/** One-pole highpass (signal minus its own lowpass). */
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

    void setLowPass (double sampleRate, double freq, double q) noexcept
    {
        double cosw, alpha;
        prep (sampleRate, freq, q, cosw, alpha);
        set ((1.0 - cosw) * 0.5, 1.0 - cosw, (1.0 - cosw) * 0.5,
             1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    void setHighPass (double sampleRate, double freq, double q) noexcept
    {
        double cosw, alpha;
        prep (sampleRate, freq, q, cosw, alpha);
        set ((1.0 + cosw) * 0.5, -(1.0 + cosw), (1.0 + cosw) * 0.5,
             1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    void setPeak (double sampleRate, double freq, double q, double gainDb) noexcept
    {
        double cosw, alpha;
        prep (sampleRate, freq, q, cosw, alpha);
        const double A = std::pow (10.0, gainDb / 40.0);
        set (1.0 + alpha * A, -2.0 * cosw, 1.0 - alpha * A,
             1.0 + alpha / A, -2.0 * cosw, 1.0 - alpha / A);
    }

private:
    static void prep (double sr, double f, double q, double& cosw, double& alpha) noexcept
    {
        f = juce::jlimit (10.0, sr * 0.49, f);
        q = juce::jmax (0.05, q);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
        cosw = std::cos (w0);
        alpha = std::sin (w0) / (2.0 * q);
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
/** First-order allpass with a settable break frequency. Passes everything at
    unit gain but rotates phase around the break point - cascade several and
    sweep them and you get a phaser. */
class AllpassOnePole
{
public:
    void setSampleRate (double sr) noexcept { fs = sr; }
    void reset() noexcept { z = 0.0f; }

    inline void setFrequency (float hz) noexcept
    {
        hz = juce::jlimit (20.0f, (float) (fs * 0.45), hz);
        const float t = std::tan (juce::MathConstants<float>::pi * hz / (float) fs);
        a = (t - 1.0f) / (t + 1.0f);
    }

    inline float process (float x) noexcept
    {
        const float y = a * x + z;
        z = x - a * y;
        if (! std::isfinite (z) || std::abs (z) < 1.0e-18f) z = 0.0f;
        return y;
    }

private:
    double fs = 48000.0;
    float a = 0.0f, z = 0.0f;
};

//==============================================================================
/** Ramps a value toward a target one sample at a time, so parameter changes
    never produce a click. */
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

    float getCurrent() const noexcept { return current; }

private:
    float coeff = 0.999f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** Low-frequency oscillator with selectable shape. Duty skews the waveform:
    it sets pulse width on a square and moves the peak on a triangle. */
class Lfo
{
public:
    enum Shape { sine = 0, triangle, square, saw, ramp, numShapes };

    void setSampleRate (double sr) noexcept { fs = sr; }
    void setRate (float hz) noexcept { inc = (float) (juce::jlimit (0.001, 100.0, (double) hz) / fs); }
    void setShape (int s) noexcept { shape = juce::jlimit (0, (int) numShapes - 1, s); }
    void setDuty (float d) noexcept { duty = juce::jlimit (0.05f, 0.95f, d); }
    void reset() noexcept { phase = 0.0f; }

    /** Advances and returns the current value, in -1 to +1. */
    inline float next() noexcept
    {
        phase += inc;
        while (phase >= 1.0f) phase -= 1.0f;
        return valueAt (phase);
    }

    /** Value at an offset from the current phase, without advancing. */
    inline float at (float offset) const noexcept
    {
        float p = phase + offset;
        while (p >= 1.0f) p -= 1.0f;
        while (p < 0.0f)  p += 1.0f;
        return valueAt (p);
    }

    float getPhase() const noexcept { return phase; }

private:
    inline float valueAt (float p) const noexcept
    {
        switch (shape)
        {
            case triangle:
                // duty moves the peak, turning the triangle into a ramp
                return (p < duty) ? (2.0f * p / duty - 1.0f)
                                  : (1.0f - 2.0f * (p - duty) / (1.0f - duty));
            case square:
                return (p < duty) ? 1.0f : -1.0f;
            case saw:
                return 1.0f - 2.0f * p;
            case ramp:
                return 2.0f * p - 1.0f;
            case sine:
            default:
                return std::sin (2.0f * juce::MathConstants<float>::pi * p);
        }
    }

    double fs = 48000.0;
    float inc = 0.0f, phase = 0.0f, duty = 0.5f;
    int shape = sine;
};

//==============================================================================
class PedalEngine
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        const int maxSamples = (int) (sr * 0.12) + 8;
        lineL.prepare (maxSamples); lineR.prepare (maxSamples);
        lfo.setSampleRate (sr);
        toneL.set (sr, 10000.0); toneR.set (sr, 10000.0);
        dcL.set (sr, 25.0); dcR.set (sr, 25.0);
        smDelay.reset (sr, 60.0, 0.012f * (float) sr);
        smDepth.reset (sr, 60.0, 0.0f);
        smMix.reset (sr, 30.0, 0.5f);
        smLevel.reset (sr, 30.0, 1.0f);
        clear();
    }

    void clear() noexcept
    {
        lineL.clear(); lineR.clear(); lfo.reset();
        toneL.reset(); toneR.reset(); dcL.reset(); dcR.reset();
        fbL = fbR = 0.0f;
    }

    void setParam (int i, float v) noexcept { if (i >= 0 && i < numParams) p[i] = v; }
    void setHostTempo (double) noexcept {}

    void updateParameters()
    {
        const int type = juce::jlimit (0, 3, (int) p[0]);
        numVoices = (type == 0) ? 2 : (type == 1) ? 4 : (type == 2) ? 3 : 1;
        shapeType = type;
        wetOnly   = (type == 3);

        lfo.setRate (p[1]);
        smDelay.setTarget (juce::jlimit (1.0f, (float) lineL.getSize() * 0.4f,
                                         p[3] * 0.001f * (float) sampleRate));
        smDepth.setTarget (juce::jlimit (0.0f, 1.0f, p[2] * 0.01f) * 0.85f);
        feedback = juce::jlimit (0.0f, 0.95f, p[4] * 0.01f * 0.9f);
        width    = juce::jlimit (0.0f, 1.0f, p[5] * 0.01f);
        toneL.set (sampleRate, (double) juce::jlimit (500.0f, 18000.0f, p[6]));
        toneR.set (sampleRate, (double) juce::jlimit (500.0f, 18000.0f, p[6]));
        smMix.setTarget (wetOnly ? 1.0f : juce::jlimit (0.0f, 1.0f, p[7] * 0.01f));
        smLevel.setTarget (juce::Decibels::decibelsToGain (p[8]));
    }

    inline void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float centre = smDelay.next();
        const float depth  = smDepth.next();
        const float mix    = smMix.next();
        const float level  = smLevel.next();

        lfo.next();
        const float chanOffset = width * 0.5f;

        float accL = 0.0f, accR = 0.0f;

        for (int v = 0; v < numVoices; ++v)
        {
            const float voiceOffset = (float) v / (float) numVoices;
            const float mL = shaped (lfo.at (voiceOffset));
            const float mR = shaped (lfo.at (voiceOffset + chanOffset));
            accL += lineL.read (centre + centre * depth * mL);
            accR += lineR.read (centre + centre * depth * mR);
        }

        const float norm = 1.0f / (float) numVoices;
        float wetL = dcL.process (toneL.process (accL * norm));
        float wetR = dcR.process (toneR.process (accR * norm));

        fbL = std::tanh (wetL * feedback * 1.2f) * 0.85f;
        fbR = std::tanh (wetR * feedback * 1.2f) * 0.85f;
        lineL.write (inL + fbL);
        lineR.write (inR + fbR);

        outL = (inL * (1.0f - mix) + wetL * mix) * level;
        outR = (inR * (1.0f - mix) + wetR * mix) * level;
    }

private:
    inline float shaped (float s) const noexcept
    {
        // Ensemble runs two LFOs at different speeds; the beating between them
        // is what stops a string-machine chorus sounding mechanical.
        if (shapeType == 2)
            return 0.78f * s + 0.22f * std::sin (3.0f * std::asin (juce::jlimit (-1.0f, 1.0f, s)));
        return s;
    }

    static constexpr int numParams = 9;
    float p[numParams] = { 0, 0.7f, 35, 12, 0, 60, 9000, 50, 0 };

    DelayLine lineL, lineR;
    OnePoleLP toneL, toneR;
    OnePoleHP dcL, dcR;
    Lfo lfo;
    SmoothedParam smDelay, smDepth, smMix, smLevel;
    double sampleRate = 48000.0;
    int numVoices = 2, shapeType = 0;
    bool wetOnly = false;
    float feedback = 0.0f, width = 0.6f, fbL = 0.0f, fbR = 0.0f;
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
