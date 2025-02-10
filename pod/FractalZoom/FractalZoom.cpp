/***************************************************************
    FractalZoom.cpp for Daisy Pod
    ------------------------------------------------------------
    Controls:
      - Knob1 => Loop Length in [0.5..10] seconds
      - Knob2 => Eval Rate   in [1..30] times per second
      - Encoder => Slew time in [0..2] seconds, step by 0.05
      - Button1 => Zoom in (increase ZoomFactor by 0.05)
      - Button2 => Zoom out (decrease ZoomFactor by 0.05)

    Features:
      - fBm-based frequency generation (1D Perlin + fractal).
      - Quantization to major scale using just intonation over two octaves.
      - Slew-limited pitch transitions for smooth audio changes.
      - LEDs indicate Zoom Level (LED1) and Eval Rate (LED2).
      - Buttons allow dynamic zooming over time.

    LEDs:
      - LED1 => Red brightness indicating ZoomFactor (in octaves)
      - LED2 => Green brightness indicating Eval Rate

    Audio:
      - Decimated fractal evaluation based on EvalRate.
      - Two sine oscillators (Left, Right) with a small domain offset.
      - Slew-limited pitch changes for smooth transitions.
***************************************************************/

#include "daisy_pod.h"
#include "daisysp.h"
#include <cmath>

using namespace daisy;
using namespace daisysp;

// --------------------------------------------------------
// Constants and Hard-Coded Values
// --------------------------------------------------------

// Zoom factor limits
static const float kMinZoom = 0.125f; // 3 octaves below
static const float kMaxZoom = 32.f;   // 5 octaves above

static float gZoomFactor = 1.f; // initial zoom factor

// fBm parameters
static const int   kFbmOctaves  = 7;
static const float kVoiceOffset = 0.4f;

// Base frequency for quantization (A1 = 55 Hz)
static float gBaseFreq = 55.f;

// Loop time and evaluation
static float gLoopLength  = 2.f;    // initial loop length in seconds
static float gEvalRate    = 3.f;    // initial eval rate in Hz
static float gEvalInterval= 1.f / gEvalRate;

// Slew time
static float gSlewSec     = 0.02f;   // initial slew time in seconds

// Loop timer
static float gLoopT       = 0.f;

// Fractal eval timer
static float gEvalTimer   = 0.f;

// Sample rate (initialized in main)
static float gSampleRate;

// --------------------------------------------------------
// Just-Intonation Major Scale (two octaves)
// --------------------------------------------------------
// Define 7 notes for one octave in just intonation major scale
static const float majorJust7_scale[7] = {
    1.0f,        // 0 = root (C)
    9.f/8.f,     // 1 = major second (D)
    5.f/4.f,     // 2 = major third (E)
    4.f/3.f,     // 3 = perfect fourth (F)
    3.f/2.f,     // 4 = perfect fifth (G)
    5.f/3.f,     // 5 = major sixth (A)
    15.f/8.f     // 6 = major seventh (B)
};

// Quantization function: map fractVal in [-2..+2] to frequency in major just intonation scale over four octaves
static float QuantizeJustMajor(float fractVal)
{
    // clamp fractVal in [-2..2]
    if(fractVal < -2.f) fractVal = -2.f;
    if(fractVal >  2.f) fractVal =  2.f;

    // shift to [0..4] (normalized input range)
    float shifted = fractVal + 2.f; // now in [0..4]

    // map [0..4] to [0..28) (4 octaves = 7 notes per octave * 4 octaves = 28 total steps)
    float scaled = shifted * (28.f / 4.f); // => [0..28)
    int step     = (int)floorf(scaled);   // determine the step
    if(step < 0)    step = 0;
    if(step > 27)   step = 27; // restrict step range to the 28 notes in 4 octaves

    // Calculate the ratio for the note:
    // If step < 7 => ratio=majorJust7_scale[step]
    // If step < 14 => ratio=2*majorJust7_scale[step-7]
    // If step < 21 => ratio=4*majorJust7_scale[step-14]
    // If step < 28 => ratio=8*majorJust7_scale[step-21] (4 octaves)
    float ratio;
    if(step < 7)
        ratio = majorJust7_scale[step];
    else if(step < 14)
        ratio = 2.f * majorJust7_scale[step - 7];
    else if(step < 21)
        ratio = 4.f * majorJust7_scale[step - 14];
    else
        ratio = 8.f * majorJust7_scale[step - 21];

    // Final frequency
    return gBaseFreq * ratio;
}

// --------------------------------------------------------
// Perlin + fBm (1D)
// --------------------------------------------------------
static uint8_t s_perm[512];
static const uint8_t s_permRef[256] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190, 6,148,
    247,120,234,75, 0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33, 88,237,149,56,87,174,20,125,136,171,168, 68,
    175, 74,165,71,134,139,48,27,166,77,146,158,231, 83,111,
    229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
    102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,
    208, 89,18,169,200,196,135,130,116,188,159,86,164,100,
    109,198,173,186, 3,64,52,217,226,250,124,123, 5,202,
    38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,
    17,182,189,28,42,223,183,170,213,119,248,152,  2,44,
    154,163,70,221,153,101,155,167, 43,172,  9,129,22,39,
    253,19, 98,108,110,79,113,224,232,178,185,112,104,218,
    246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107, 49,192,214,31,181,
    199,106,157,184,84,204,176,115,121, 50,45,127,  4,
    150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215
};

// Fade function as defined by Ken Perlin
static inline float fadef(float t)
{
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}

// Gradient function for 1D Perlin Noise
static inline float grad1D(int hash, float x)
{
    return ((hash & 1) ? x : -x);
}

// Initialize the permutation table
static void InitPerlin()
{
    for(int i = 0; i < 256; i++)
    {
        s_perm[i]     = s_permRef[i];
        s_perm[i+256] = s_permRef[i];
    }
}

// 1D Perlin Noise function
static float PerlinNoise1D(float x)
{
    int   xi  = (int)floorf(x);
    float xf  = x - (float)xi;
    int   X   = xi & 255;

    float u   = fadef(xf);

    int hashA = s_perm[X];
    int hashB = s_perm[X + 1];

    float g1  = grad1D(hashA, xf);
    float g2  = grad1D(hashB, xf - 1.f);

    return (1.f - u)*g1 + u*g2;
}

// 1D fractional Brownian motion
static float fBm1D(float x, int octaves, float lacunarity, float gain)
{
    float sum  = 0.f;
    float freq = 1.f;
    float amp  = 1.f;

    for(int i = 0; i < octaves; i++)
    {
        float n = PerlinNoise1D(x * freq);
        sum += n * amp;
        freq *= lacunarity;
        amp  *= gain;
    }
    return sum;
}

// --------------------------------------------------------
// Slew Limiter class for smooth pitch transitions
// --------------------------------------------------------
class SlewLimiter
{
  public:
    void Init(float samplerate)
    {
        sr_    = samplerate;
        value_ = 0.f;
        dest_  = 0.f;
        rise_  = 0.02f; // default rise time
        fall_  = 0.03f; // default fall time
    }
    void SetRiseFall(float t)
    {
        rise_ = t;
        fall_ = t;
    }
    void SetValue(float v)
    {
        value_ = v;
        dest_  = v;
    }
    void SetDest(float d)
    {
        dest_ = d;
    }
    float Process()
    {
        float diff = dest_ - value_;
        float time = (diff >= 0.f) ? rise_ : fall_;
        if(time < 1.0e-6f)
        {
            value_ = dest_;
            return value_;
        }
        float step = diff / (time * sr_);
        if(std::fabs(step) > std::fabs(diff))
            value_ = dest_;
        else
            value_ += step;
        return value_;
    }
  private:
    float sr_, value_, dest_, rise_, fall_;
};

// --------------------------------------------------------
// Daisy Pod & Global Objects
// --------------------------------------------------------
static DaisyPod pod;
static Parameter p_loopLength, p_evalRate;
static Oscillator oscLeft, oscRight;
static SlewLimiter slewL, slewR;

// Current frequencies after quantization
static float gCurrentFreqL = 440.f;
static float gCurrentFreqR = 440.f;

// --------------------------------------------------------
// Quantization Function: Just-Intonation Major Scale
// --------------------------------------------------------

// Already defined above as majorJust7_scale and QuantizeJustMajor

// --------------------------------------------------------
// Audio Callback
// --------------------------------------------------------
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    float dt = 1.f / gSampleRate;
    for(size_t i = 0; i < size; i++)
    {
        // Advance loop time
        gLoopT += dt;
        if(gLoopT > gLoopLength)
            gLoopT -= gLoopLength;

        // Evaluate fractal at decimated rate
        gEvalTimer += dt;
        if(gEvalTimer >= gEvalInterval)
        {
            gEvalTimer = 0.f;

            // domain for Left
            float domainL = gLoopT * gZoomFactor;
            // domain for Right with voice offset
            float domainR = domainL + kVoiceOffset;

            // Evaluate fractal
            float valL = fBm1D(domainL, kFbmOctaves, 4.3f, 0.5f);
            float valR = fBm1D(domainR, kFbmOctaves, 2.f, 0.7f);

            // Quantize to major just intonation
            float freqL = QuantizeJustMajor(valL);
            float freqR = QuantizeJustMajor(valR);

            // Set slew limiter destinations
            slewL.SetDest(freqL);
            slewR.SetDest(freqR);
        }

        // Get current frequencies from slew limiters
        float freqL = slewL.Process();
        float freqR = slewR.Process();

        // Set oscillator frequencies
        oscLeft.SetFreq(freqL);
        oscRight.SetFreq(freqR);

        // Generate audio signals
        float sigL = oscLeft.Process();
        float sigR = oscRight.Process();

        // Output to left and right channels
        out[0][i] = sigL;
        out[1][i] = sigR;
    }
}

// --------------------------------------------------------
// UpdateControls: Read knobs, buttons, encoder; set parameters and LEDs
// --------------------------------------------------------
static void UpdateControls()
{
    // read all controls
    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();
    int enc = pod.encoder.Increment();

    // Knob1 => Loop length in [0.5..10]
    gLoopLength  = p_loopLength.Process();

    // Knob2 => Eval rate in [1..30]
    gEvalRate    = p_evalRate.Process();
    gEvalInterval = 1.f / gEvalRate;

    // Encoder => adjust slew time in [0..2], step by 0.05
    if(enc != 0)
    {
        gSlewSec += 0.006f * (float)enc;
        // Clamp
        if(gSlewSec < 0.f)
            gSlewSec = 0.f;
        if(gSlewSec > 2.f)
            gSlewSec = 2.f;
    }

    // Update slew limiters with new slew time
    slewL.SetRiseFall(gSlewSec);
    slewR.SetRiseFall(gSlewSec);

    // Buttons => Zoom factor
    bool b1 = pod.button1.Pressed();
    bool b2 = pod.button2.Pressed();

    if(b1)
    {
        gZoomFactor += 0.01f;
        if(gZoomFactor > kMaxZoom)
            gZoomFactor = kMaxZoom;
    }
    if(b2)
    {
        gZoomFactor -= 0.01f;
        if(gZoomFactor < kMinZoom)
            gZoomFactor = kMinZoom;
    }

    // LED1 => Red brightness indicating ZoomFactor (in octaves)
    // We'll map log2(gZoomFactor) from log2(kMinZoom) to log2(kMaxZoom)
    {
        float minLog = log2f(kMinZoom); // log2(0.25) = -2
        float maxLog = log2f(kMaxZoom); // log2(16)  = 4
        float currentLog = log2f(gZoomFactor);
        float frac = (currentLog - minLog) / (maxLog - minLog);
        if(frac < 0.f) frac = 0.f;
        if(frac > 1.f) frac = 1.f;
        // LED1 red brightness = frac, green and blue = 0
        pod.led1.Set(frac, 0.f, 0.f);
    }

    // LED2 => Green brightness indicating EvalRate [1..30]
    {
        float frac = (gEvalRate - 1.f) / 29.f;
        if(frac < 0.f) frac = 0.f;
        if(frac > 1.f) frac = 1.f;
        // LED2 green brightness = frac, red and blue = 0
        pod.led2.Set(0.f, frac, 0.f);
    }

    // Update LEDs
    pod.led1.Update();
    pod.led2.Update();
}

// --------------------------------------------------------
// Main Function
// --------------------------------------------------------
int main(void)
{
    // 1) Initialize hardware
    pod.Init();

    // 2) Start ADC so knobs are scanned
    pod.StartAdc();

    // 3) Initialize Perlin Noise permutation table
    InitPerlin();

    // 4) Initialize Parameters
    //    Knob1 => Loop Length [0.5..10]
    //    Knob2 => Eval Rate [1..30]
    p_loopLength.Init(pod.knob1, 0.5f, 3.f, Parameter::LINEAR);
    p_evalRate.Init(pod.knob2,   1.f,  15.f, Parameter::LINEAR);

    // 5) Initialize oscillators
    float sr = pod.AudioSampleRate();
    gSampleRate = sr; // Initialize global sample rate
    oscLeft.Init(sr);
    oscLeft.SetWaveform(Oscillator::WAVE_SIN);
    oscLeft.SetAmp(0.5f);

    oscRight.Init(sr);
    oscRight.SetWaveform(Oscillator::WAVE_SIN);
    oscRight.SetAmp(0.5f);

    // 6) Initialize slew limiters
    slewL.Init(sr);
    slewR.Init(sr);
    slewL.SetValue(440.f);
    slewR.SetValue(440.f);

    // 7) Start audio callback
    pod.StartAudio(AudioCallback);

    // 8) Initialize loop and eval timers
    gLoopT = 0.f;
    gEvalTimer = 0.f;

    // 9) Main loop
    while(1)
    {
        UpdateControls();
        System::Delay(10); // 10 ms delay
    }
    return 0;
}
