/**********************************************************
   Randos.cpp
   Pseudo-random stepwise generator for Daisy Patch
   (Root in [None, C..B], plus Just On/Off, no ADSR)
**********************************************************/

#include "daisysp.h"
#include "daisy_patch.h"
#include <string>

// ----------------------------------------------------
// Namespaces
// ----------------------------------------------------
using namespace daisy;
using namespace daisysp;

// ----------------------------------------------------
// A simple linear-congruential generator (LCG)
// ----------------------------------------------------
static uint32_t LCG_Next(uint32_t &seed)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}
static float Rand01(uint32_t &seed)
{
    uint32_t r = (LCG_Next(seed) >> 8);
    return float(r) * (1.0f / 16777216.0f);
}

// ----------------------------------------------------
// We'll store a single active note (MIDI-driven)
// ----------------------------------------------------
struct ActiveNote
{
    bool     on;
    uint8_t  midinote;
    uint32_t seed;
    float    phase; // step logic accumulator
};

static ActiveNote g_note = {false, 0, 0, 0.f};

// ----------------------------------------------------
// Simple linear slew limiter for pitch/CV
// ----------------------------------------------------
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

static SlewLimiter pitchSlew, cvSlew1, cvSlew2;

// ----------------------------------------------------
// Root choices: 0 => "None", 1=>C, 2=>C#, ..., 12=>B
// We'll store them in a single array for display
// Then subtract 1 for the actual semitone if root>0
// ----------------------------------------------------
static const char* ROOT_NAMES[13] = {
    "None","C","C#","D","D#","E","F",
    "F#","G","G#","A","A#","B"
};

// We'll define major scale offsets for 12-TET major scale
static const int MAJOR_OFFSETS[8] = {0,2,4,5,7,9,11,12};

// We'll also define a Just Major scale (interval ratios)
static float JUST_MAJOR[] = {
    1.f,      // unison
    9.f/8.f,  // major 2nd
    5.f/4.f,  // major 3rd
    4.f/3.f,  // perfect 4th
    3.f/2.f,  // perfect 5th
    5.f/3.f,  // major 6th
    15.f/8.f, // major 7th
    2.f       // octave
};

// ----------------------------------------------------
// We'll keep these global state variables:
//   g_rootIndex => 0..12 => "None", "C", "C#", etc.
//   g_octRange  => in [0.5..6]
//   g_justOn    => bool
//   g_uiMode    => 0..3 => which UI param we are editing
// ----------------------------------------------------
static int   g_rootIndex = 0;   // 0 => None, 1..12 => C..B
static float g_octRange  = 1.f; // 0.5..6
static bool  g_justOn    = false;
static int   g_uiMode    = 0;   // 0=>root,1=>range,2=>just,3=>idle

// ----------------------------------------------------
// Gate output pin
// ----------------------------------------------------
static dsy_gpio gatePin;
static void SetGate(bool high)
{
    dsy_gpio_write(&gatePin, high);
}

// ----------------------------------------------------
// Four oscillators for audio out
// ----------------------------------------------------
static Oscillator osc[4];

// ----------------------------------------------------
// Daisy hardware objects
// ----------------------------------------------------
static DaisyPatch      patch;
static MidiUartHandler midi;

// ----------------------------------------------------
// Convert 0..5 V => 12-bit DAC
// ----------------------------------------------------
static uint16_t VoltsToDac(float volts)
{
    if(volts < 0.f)  volts = 0.f;
    if(volts > 5.f)  volts = 5.f;
    return (uint16_t)((volts / 5.f) * 4095.f);
}

// ----------------------------------------------------
// MIDI->freq
// ----------------------------------------------------
static float MidiToFreq(int note)
{
    return 440.f * powf(2.f, (note - 69.f) / 12.f);
}

// ----------------------------------------------------
// Just scale around a "base frequency" derived from root
// We'll treat baseFreq = MidiToFreq(48 + (rootIndex-1))
//  if rootIndex>0, or just 220 if you prefer a standard A3.
// Then we pick from JUST_MAJOR plus random octaves
// up to g_octRange
// ----------------------------------------------------
static float JustRandomFreq(uint32_t &seed, float baseFreq)
{
    const int scaleSize = 8; // length of JUST_MAJOR
    // pick one ratio
    float r1 = Rand01(seed);
    int   idx = (int)(r1 * scaleSize);
    if(idx >= scaleSize) idx = scaleSize - 1;

    // pick an integer octave in [0.. floor(g_octRange)]
    float r2       = Rand01(seed);
    int   maxOctI  = (int)g_octRange; // floor
    int   octPicked = (int)(r2 * (maxOctI+1));
    if(octPicked > maxOctI) octPicked = maxOctI;

    float ratio = JUST_MAJOR[idx] * powf(2.f, (float)octPicked);
    return baseFreq * ratio;
}

// ----------------------------------------------------
// A function that picks a random frequency based on
//   1) g_rootIndex: 0 => none, 1..12 => semitone root
//   2) g_octRange
//   3) g_justOn => if true, pick from a Just scale
//                  else pick from a 12TET major scale
// ----------------------------------------------------
static float RandomQuantizedFreq(uint32_t &seed)
{
    // If root=0 => "None" => unquantized
    if(g_rootIndex == 0)
    {
        // 50..2000 Hz
        float r = Rand01(seed);
        return 50.f + 1950.f * r;
    }

    // else we have a root
    int rootSemitone = (g_rootIndex - 1); // 0..11 => C..B
    float baseFreq    = MidiToFreq(48 + rootSemitone);
        // e.g. around C3 if rootSemitone=0 => C

    // If "Just" is ON => pick from Just scale
    if(g_justOn)
    {
        return JustRandomFreq(seed, baseFreq);
    }
    else
    {
        // 12TET major scale quant
        // e.g. pick random semitones up to 12*g_octRange
        float r = Rand01(seed);
        float maxSemisF = 12.f * g_octRange;
        float pickF     = r * maxSemisF;
        int   pickI     = (int)pickF; // integer semitones
        int   fullOct   = pickI / 12;
        int   leftover  = pickI % 12;
        if(leftover < 0) leftover += 12;

        // snap leftover to major scale
        int chosen = 0;
        for(int i=0; i<8; i++)
        {
            if(MAJOR_OFFSETS[i] <= leftover)
                chosen = MAJOR_OFFSETS[i];
            else
                break;
        }
        int totalSemis = fullOct * 12 + chosen + rootSemitone;
        int midinote   = 48 + totalSemis; // ~C3-based
        if(midinote < 0)   midinote = 0;
        if(midinote > 127) midinote = 127;
        return MidiToFreq(midinote);
    }
}

// ----------------------------------------------------
// MIDI handling
// ----------------------------------------------------
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
                    g_note.on       = true;
                    g_note.midinote = n;
                    // Deterministic seed
                    g_note.seed     = (n * 12345u) + 99999u;
                    g_note.phase    = 0.f;

                    SetGate(true);
                }
                else
                {
                    // velocity=0 => note off
                    if(g_note.midinote == n)
                    {
                        g_note.on = false;
                        SetGate(false);
                    }
                }
            }
            break;

            case NoteOff:
            {
                uint8_t n = msg.data[0] & 0x7F;
                if(g_note.on && g_note.midinote == n)
                {
                    g_note.on = false;
                    SetGate(false);
                }
            }
            break;

            default: break;
        }
    }
}

// ----------------------------------------------------
// Encoder UI
//   Press cycles 4 states: 0=root,1=range,2=justOn,3=idle
//   Turn changes root, range, or toggles just
// ----------------------------------------------------
static bool g_prevPress = false;
static void UpdateEncoderUI()
{
    patch.ProcessDigitalControls();
    Encoder &enc = patch.encoder;

    bool pressed = enc.Pressed();
    // detect rising edge
    if(!g_prevPress && pressed)
    {
        g_uiMode = (g_uiMode + 1) % 4; // now 4 states
    }
    g_prevPress = pressed;

    int inc = enc.Increment();
    if(inc != 0)
    {
        switch(g_uiMode)
        {
            case 0: // editing root
            {
                // rootIndex in [0..12]
                g_rootIndex += inc;
                if(g_rootIndex < 0)  g_rootIndex = 0;
                if(g_rootIndex > 12) g_rootIndex = 12;
                break;
            }
            case 1: // editing range
            {
                float step = 0.5f * float(inc);
                g_octRange += step;
                if(g_octRange < 0.5f) g_octRange = 0.5f;
                if(g_octRange > 6.f)  g_octRange = 6.f;
                break;
            }
            case 2: // toggling Just
            {
                // each increment toggles once (or you can do only inc>0)
                if(inc > 0)
                    g_justOn = !g_justOn;
                else
                    g_justOn = !g_justOn;
                break;
            }
            case 3: // idle
            default:
                // do nothing
                break;
        }
    }
}

// ----------------------------------------------------
// Simple OLED display
// ----------------------------------------------------
static void UpdateOled()
{
    patch.display.Fill(false);

    patch.display.SetCursor(0, 0);
    patch.display.WriteString("Randos + Root/Just", Font_7x10, true);

    // Root
    patch.display.SetCursor(0, 15);
    patch.display.WriteString("Root: ", Font_7x10, true);
    patch.display.WriteString(ROOT_NAMES[g_rootIndex], Font_7x10, true);

    // range
    patch.display.SetCursor(0, 30);
    char rbuf[32];
    sprintf(rbuf, "Range: %.1f oct", (double)g_octRange);
    patch.display.WriteString(rbuf, Font_7x10, true);

    // Just status
    patch.display.SetCursor(0, 45);
    if(g_justOn)
        patch.display.WriteString("Just=ON ", Font_7x10, true);
    else
        patch.display.WriteString("Just=OFF", Font_7x10, true);

    // mode
    patch.display.SetCursor(80, 45);
    switch(g_uiMode)
    {
        case 0: patch.display.WriteString("[Root]",  Font_7x10, true); break;
        case 1: patch.display.WriteString("[Range]", Font_7x10, true); break;
        case 2: patch.display.WriteString("[Just]",  Font_7x10, true); break;
        case 3: patch.display.WriteString("[Idle]",  Font_7x10, true); break;
    }

    patch.display.Update();
}

// ----------------------------------------------------
// Audio callback
//   - Controller 0 => step rate  [3s..30 Hz]
//   - Controller 1 => amplitude  + CV out1
//   - Controller 2 => CV out2
//   - Controller 3 => slew time  [0..1s]
// ----------------------------------------------------
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    patch.ProcessAnalogControls();

    float ctrl0 = patch.controls[0].Process(); // step rate
    float ctrl1 = patch.controls[1].Process(); // amplitude + CV1
    float ctrl2 = patch.controls[2].Process(); // CV2
    float ctrl3 = patch.controls[3].Process(); // slew time

    // Step rate
    float minFreq  = 0.3333f; // ~1 step every 3s
    float maxFreq  = 30.f;
    float ratio    = maxFreq / minFreq; // ~90
    float stepFreq = minFreq * powf(ratio, ctrl0);

    // Slew time 0..1s
    float maxSlew = 1.f;
    float slewT   = ctrl3 * maxSlew;
    pitchSlew.SetRiseTime(slewT);
    pitchSlew.SetFallTime(slewT);
    cvSlew1.SetRiseTime(slewT);
    cvSlew1.SetFallTime(slewT);
    cvSlew2.SetRiseTime(slewT);
    cvSlew2.SetFallTime(slewT);

    float sr  = patch.AudioSampleRate();
    float inc = stepFreq / sr;

    for(size_t i = 0; i < size; i++)
    {
        if(!g_note.on)
        {
            // No note => zero audio + zero CV
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;
            patch.seed.dac.WriteValue(DacHandle::Channel::ONE, 0);
            patch.seed.dac.WriteValue(DacHandle::Channel::TWO, 0);
            continue;
        }

        g_note.phase += inc;
        if(g_note.phase >= 1.f)
        {
            g_note.phase -= 1.f;
            // new random freq
            float newFreq = RandomQuantizedFreq(g_note.seed);
            pitchSlew.SetDest(newFreq);

            // random for CV out2
            float r2 = Rand01(g_note.seed);
            float cv2target = r2 * 5.f;
            cvSlew2.SetDest(cv2target);

            // random for CV out1
            float r1 = Rand01(g_note.seed);
            float cv1target = r1 * 5.f;
            cvSlew1.SetDest(cv1target);
        }

        // process slews
        float freqNow = pitchSlew.Process();
        float cv1Now  = cvSlew1.Process();
        float cv2Now  = cvSlew2.Process();

        // update oscillators
        osc[0].SetFreq(freqNow);
        osc[1].SetFreq(freqNow);
        osc[2].SetFreq(freqNow);
        osc[3].SetFreq(freqNow);

        // amplitude
        float amp = ctrl1;
        float s0  = osc[0].Process() * amp;
        float s1  = osc[1].Process() * amp;
        float s2  = osc[2].Process() * amp;
        float s3  = osc[3].Process() * amp;

        out[0][i] = s0;
        out[1][i] = s1;
        out[2][i] = s2;
        out[3][i] = s3;

        // CV outs: scale CV1 by ctrl1, CV2 by ctrl2
        float cvOut1 = cv1Now * ctrl1;
        float cvOut2 = cv2Now * ctrl2;
        patch.seed.dac.WriteValue(DacHandle::Channel::ONE, VoltsToDac(cvOut1));
        patch.seed.dac.WriteValue(DacHandle::Channel::TWO, VoltsToDac(cvOut2));
    }
}

// ----------------------------------------------------
// Main
// ----------------------------------------------------
int main(void)
{
    patch.Init();
    float sr = patch.AudioSampleRate();

    // Gate pin
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

    // Oscillators
    for(int i = 0; i < 4; i++)
    {
        osc[i].Init(sr);
        osc[i].SetAmp(1.0f);
    }
    osc[0].SetWaveform(Oscillator::WAVE_SIN);
    osc[1].SetWaveform(Oscillator::WAVE_SQUARE);
    osc[2].SetWaveform(Oscillator::WAVE_TRI);
    osc[3].SetWaveform(Oscillator::WAVE_SAW);

    // Slews
    pitchSlew.Init(sr);
    pitchSlew.SetValue(220.f);
    cvSlew1.Init(sr);
    cvSlew1.SetValue(0.f);
    cvSlew2.Init(sr);
    cvSlew2.SetValue(0.f);

    // Splash
    patch.display.Fill(false);
    patch.display.SetCursor(0,0);
    patch.display.WriteString("Randos w/Root+Just", Font_7x10, true);
    patch.display.Update();
    patch.DelayMs(1000);

    // Start
    patch.StartAdc();
    patch.StartAudio(AudioCallback);

    while(1)
    {
        midi.Listen();
        HandleMidi(midi);

        UpdateEncoderUI();
        UpdateOled();

        patch.DelayMs(10);
    }
    return 0;
}
