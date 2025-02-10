/***************************************************************
   FractalZoom_fBm.cpp
   A demonstration for Daisy Patch, using:
   - Perlin Noise + fBm for real-time fractal generation
   - "Zoom factor" and "Zoom point" to shift/scale the domain
   - 5-second note playback triggered by MIDI
   - Step logic from 0..5 seconds (or variable length)
   - Quantization at playback

   Knobs:
   - Knob0 => Zoom Factor [1..3] (logarithmic)
   - Knob1 => Zoom Point offset in [0..5]
   - Knob2 => Slew Time in [0..1s]
   - Knob3 => VCA amplitude

   Each Note On => we read the current ZoomFactor/ZoomPoint,
   store them. Then for the next 5s, we evaluate:
       fBm((time + zoomPoint)*zoomFactor)
   in the audio callback. This fractal value is then mapped to
   a frequency domain for pitch, and we slews for a smooth effect.

   If performance is too high, you can reduce calls to fBm by
   stepping at a lower rate or reducing octaves.

***************************************************************/

#include "daisysp.h"
#include "daisy_patch.h"
#include <cmath>

//--------------------------------------------------
// Namespaces
//--------------------------------------------------
using namespace daisy;
using namespace daisysp;

//--------------------------------------------------
// We'll implement a 1D Perlin + fBm approach
// - We'll keep a static permutation table
// - We'll define PerlinNoise1D()
// - We'll define fBm1D()
//--------------------------------------------------

static uint8_t s_perm[512]; // we fill this at init

// A standard reference permutation for 256 values:
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

static void InitPerlinPermutation()
{
    // Duplicate s_permRef[] in s_perm[] 2x
    for(int i = 0; i < 256; i++)
    {
        s_perm[i]       = s_permRef[i];
        s_perm[i+256]   = s_permRef[i];
    }
}

// Fade function: 6t^5 - 15t^4 + 10t^3
static inline float Fade(float t)
{
    return t*t*t*(t*(t*6.f -15.f)+10.f);
}

// We only need a 1D gradient => sign flip
static inline float Grad1D(int hash, float x)
{
    // If (hash & 1) => +x else => -x
    return ((hash & 1) ? x : -x);
}

// Return noise in range ~[-1..1]
static float PerlinNoise1D(float x)
{
    int xi   = (int)floorf(x);
    float xf = x - (float)xi;
    int X    = xi & 255;

    float u = Fade(xf);

    int hashA = s_perm[X];
    int hashB = s_perm[X+1];

    float g1 = Grad1D(hashA, xf);
    float g2 = Grad1D(hashB, xf - 1.f);

    float val = (1.f - u)*g1 + u*g2;
    return val;
}

// Simple fBm with ~4..6 octaves typical
static float fBm1D(float x, int octaves, float lacunarity, float gain)
{
    float sum  = 0.f;
    float freq = 1.f;
    float amp  = 1.f;

    for(int i = 0; i < octaves; i++)
    {
        float p = PerlinNoise1D(x * freq);
        sum += p * amp;
        freq *= lacunarity;
        amp  *= gain;
    }
    return sum;
}

//--------------------------------------------------
// We'll define a small "ActiveNote" with a 5s duration
// (You can make it user-adjustable easily).
//--------------------------------------------------
struct ActiveNote
{
    bool  on;
    float phase;      // 0..duration
    float duration;   // default 5s
};

static ActiveNote g_note = {false, 0.f, 5.f};

//--------------------------------------------------
// Gate pin
//--------------------------------------------------
static dsy_gpio gatePin;
static void SetGate(bool high)
{
    dsy_gpio_write(&gatePin, high);
}

//--------------------------------------------------
// We'll define 4 oscillators for demonstration
//--------------------------------------------------
static Oscillator osc[4];

//--------------------------------------------------
// Daisy hardware
//--------------------------------------------------
static DaisyPatch      patch;
static MidiUartHandler midi;

// We'll define a pitch slew for the final freq
class SlewLimiter
{
  public:
    void Init(float samplerate)
    {
        sr_    = samplerate;
        value_ = 0.f;
        dest_  = 0.f;
        rise_  = 0.01f;
        fall_  = 0.01f;
    }
    void SetRiseTime(float t) { rise_ = t; }
    void SetFallTime(float t) { fall_ = t; }
    void SetValue(float v)    { value_ = v; dest_ = v; }
    void SetDest(float d)     { dest_  = d; }

    float Process()
    {
        float diff = dest_ - value_;
        if(diff > 0.f)
        {
            float step = diff / (rise_ * sr_);
            if(fabsf(step) > fabsf(diff))
                value_ = dest_;
            else
                value_ += step;
        }
        else if(diff < 0.f)
        {
            float step = diff / (fall_ * sr_);
            if(fabsf(step) > fabsf(diff))
                value_ = dest_;
            else
                value_ += step;
        }
        return value_;
    }

  private:
    float sr_;
    float value_;
    float dest_;
    float rise_;
    float fall_;
};

static SlewLimiter pitchSlew;

//--------------------------------------------------
// For demonstration, let's do a simple "quantize"
// mapping fractal value in [-N..+N] => freq in [50..2000]
//
// If you want 12TET or Just, you can adapt the "Randos"
// approach: turn the fractVal => semitone => freq
//--------------------------------------------------
static float QuantizeFractal(float val)
{
    // val in ~[-2..+2] if we have e.g. 4..5 octaves
    // clamp
    if(val < -2.f) val = -2.f;
    if(val >  2.f) val =  2.f;
    // shift to [0..4]
    float shifted = val + 2.f; // now in [0..4]
    // map [0..4] => [50..2000] or any range you like
    float freq = 50.f + shifted * (1950.f/4.f); // [50..2000]
    return freq;
}

//--------------------------------------------------
// We'll define "zoomFactor" and "zoomPoint"
// that we read from knobs on NOTE ON.
//--------------------------------------------------
static float g_zoomFactor = 1.f;
static float g_zoomPoint  = 0.f; // in [0..5]

// fBm parameters
static int   g_octaves    = 5;    // you can make this user adjustable
static float g_lacunarity = 2.f;
static float g_gain       = 0.5f;

//--------------------------------------------------
// MIDI handling:
//   - If we get NoteOn, we start a new "5s fractal"
//     using the current zoom knobs.
//--------------------------------------------------
static void HandleMidi(MidiUartHandler &m)
{
    while(m.HasEvents())
    {
        auto msg = m.PopEvent();
        switch(msg.type)
        {
            case NoteOn:
            {
                uint8_t n   = msg.data[0] & 0x7F;
                uint8_t vel = msg.data[1] & 0x7F;
                if(vel > 0)
                {
                    g_note.on    = true;
                    g_note.phase = 0.f;
                    // read knob0 => zoom factor in [1..3]
                    {
                        float k0 = patch.controls[0].Process(); // 0..1
                        // let's do 1 * (3^(k0)) => [1..3]
                        g_zoomFactor = powf(3.f, k0);
                    }
                    // read knob1 => zoom point in [0..5]
                    {
                        float k1 = patch.controls[1].Process(); // 0..1
                        g_zoomPoint = k1 * 5.f;
                    }

                    SetGate(true);
                }
                else
                {
                    // velocity=0 => note off
                    g_note.on = false;
                    SetGate(false);
                }
            }
            break;

            case NoteOff:
            {
                g_note.on = false;
                SetGate(false);
            }
            break;

            default:
                break;
        }
    }
}

//--------------------------------------------------
// Audio callback
//   knobs:
//    - knob0 => zoomFactor (used on noteon only, but you
//               could also do continuous if you want).
//    - knob1 => zoomPoint  (same note as above).
//    - knob2 => slew time
//    - knob3 => amplitude
//--------------------------------------------------
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    patch.ProcessAnalogControls();

    float slewK = patch.controls[2].Process(); // [0..1]
    float ampK  = patch.controls[3].Process(); // [0..1]

    // set pitch slew times
    pitchSlew.SetRiseTime(slewK * 1.f);
    pitchSlew.SetFallTime(slewK * 1.f);

    float sr = patch.AudioSampleRate();
    float inc = 1.f / sr; // each sample => +1/sr

    for(size_t i = 0; i < size; i++)
    {
        float sig = 0.f;
        if(g_note.on)
        {
            // increment
            g_note.phase += inc;
            // If we pass 5s, end
            if(g_note.phase >= g_note.duration)
            {
                g_note.on = false;
                SetGate(false);
            }
            else
            {
                // real-time fBm
                // domain = ( (phase + zoomPoint) * zoomFactor )
                float domainX = (g_note.phase + g_zoomPoint) * g_zoomFactor;

                // evaluate fractal
                float fractVal = fBm1D(domainX, g_octaves, g_lacunarity, g_gain);

                // quantize => freq
                float freq = QuantizeFractal(fractVal);
                pitchSlew.SetDest(freq);
                float freqNow = pitchSlew.Process();

                // set 4 oscillators
                for(int c=0; c<4; c++)
                    osc[c].SetFreq(freqNow);

                // produce audio
                float s0 = osc[0].Process();
                float s1 = osc[1].Process();
                float s2 = osc[2].Process();
                float s3 = osc[3].Process();
                // mix them
                float mix = (s0 + s1 + s2 + s3)*0.25f;
                sig = mix * ampK;
            }
        }

        out[0][i] = sig;
        out[1][i] = sig;
        out[2][i] = sig;
        out[3][i] = sig;
    }
}

//--------------------------------------------------
// We'll do a small function to draw the fractal
// on the OLED in near-real-time, just like before.
//
// We'll sample ~32 points from t=0..5,
// do fBm((t + zoomPoint)*zoomFactor), and draw lines.
//--------------------------------------------------
static void DrawFractalOnOled()
{
    patch.display.Fill(false);

    patch.display.SetCursor(0,0);
    patch.display.WriteString("fBm FractalZoom", Font_7x10, true);

    // We'll pick 32 steps from 0..5
    const int steps = 32;
    float stepSize  = 5.f / (steps-1);

    int lastX = 0;
    int lastY = 32;

    for(int i=0; i<steps; i++)
    {
        float t = i*stepSize;
        // domain
        float domainX = (t + g_zoomPoint) * g_zoomFactor;
        float val = fBm1D(domainX, g_octaves, g_lacunarity, g_gain);
        // val in ~[-2..2], shift => [0..4], then => 0..1
        float mapped = (val + 2.f)*0.25f;
        if(mapped < 0.f) mapped=0.f;
        if(mapped > 1.f) mapped=1.f;

        // screen x ~ [0..127], let's do i in [0..31]
        // screen y ~ [0..63]
        int x = (int)((float)i * (128.f / (steps-1)));
        int y = (int)((1.f - mapped) * 40.f) + 12; // shift so it's visible

        // draw line from last => current
        patch.display.DrawLine(lastX, lastY, x, y, true);

        lastX = x;
        lastY = y;
    }

    patch.display.Update();
}

//--------------------------------------------------
// Main
//--------------------------------------------------
int main(void)
{
    patch.Init();
    float sr = patch.AudioSampleRate();

    // gate pin
    dsy_gpio_pin gateP = {DSY_GPIOA, 10};
    gatePin.pin  = gateP;
    gatePin.mode = DSY_GPIO_MODE_OUTPUT_PP;
    gatePin.pull = DSY_GPIO_NOPULL;
    dsy_gpio_init(&gatePin);
    SetGate(false);

    // MIDI
    MidiUartHandler::Config midi_cfg;
    midi.Init(midi_cfg);
    midi.StartReceive();

    // init perlin table
    InitPerlinPermutation();

    // init oscillators
    for(int i=0; i<4; i++)
    {
        osc[i].Init(sr);
        osc[i].SetWaveform(i == 0 ? Oscillator::WAVE_SIN :
                           i == 1 ? Oscillator::WAVE_SQUARE :
                           i == 2 ? Oscillator::WAVE_TRI :
                                    Oscillator::WAVE_SAW);
        osc[i].SetAmp(1.f);
    }
    // init slew
    pitchSlew.Init(sr);
    pitchSlew.SetValue(220.f);

    // splash
    patch.display.Fill(false);
    patch.display.SetCursor(0,0);
    patch.display.WriteString("FractalZoom fBm", Font_7x10, true);
    patch.display.Update();
    patch.DelayMs(1000);

    // Start audio
    patch.StartAdc();
    patch.StartAudio(AudioCallback);

    while(1)
    {
        midi.Listen();
        HandleMidi(midi);

        // draw fractal on OLED
        DrawFractalOnOled();

        patch.DelayMs(50);
    }
    return 0;
}
