#include "daisy_patch.h"
#include "daisysp.h"
#include <cstdio>
#include <cmath>

using namespace daisy;
using namespace daisysp;

DaisyPatch patch;

// Global variables (updated in the audio callback)
volatile float g_inputCV = 0.0f;
volatile float g_eqCV    = 0.0f;
volatile float g_justCV  = 0.0f;

float QuantizeCV(float inputCV, bool useJustIntonation)
{
    int octave = static_cast<int>(std::floor(inputCV));
    float frac = inputCV - octave;
    int semitoneIndex = static_cast<int>(std::round(frac * 12.0f));
    if(semitoneIndex >= 12)
    {
        semitoneIndex = 0;
        octave += 1;
    }

    if(!useJustIntonation)
        return octave + (semitoneIndex / 12.0f);
    else
    {
        static const float justRatios[12] = {
            1.0f,          // Unison
            16.0f/15.0f,   // minor 2nd
            9.0f/8.0f,     // major 2nd
            6.0f/5.0f,     // minor 3rd
            5.0f/4.0f,     // major 3rd
            4.0f/3.0f,     // perfect 4th
            45.0f/32.0f,   // Tritone (one possibility)
            3.0f/2.0f,     // perfect 5th
            8.0f/5.0f,     // minor 6th
            5.0f/3.0f,     // major 6th
            9.0f/5.0f,     // minor 7th
            15.0f/8.0f     // major 7th
        };
        float ratio = justRatios[semitoneIndex];
        float justFracCV = std::log2(ratio);
        return octave + justFracCV;
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    patch.ProcessAllControls();
    float knob_val = patch.GetKnobValue(DaisyPatch::CTRL_1);
    float inputCV = knob_val * 8.0f; // simulate 0-8V
    float eqCV   = QuantizeCV(inputCV, false);
    float justCV = QuantizeCV(inputCV, true);
    g_inputCV = inputCV;
    g_eqCV    = eqCV;
    g_justCV  = justCV;

    // Map voltages to DAC code (assume 0-8V ~ 0-4095)
    uint16_t eqDac   = static_cast<uint16_t>(std::round((eqCV/8.0f)*4095.0f));
    uint16_t justDac = static_cast<uint16_t>(std::round((justCV/8.0f)*4095.0f));
    patch.seed.dac.WriteValue(DacHandle::Channel::ONE, eqDac);
    patch.seed.dac.WriteValue(DacHandle::Channel::TWO, justDac);

    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.0f;
    }
}

int main(void)
{
    patch.Init();

    // Write an initial message to the display before starting audio
    patch.display.Fill(false);
    patch.display.SetCursor(0, 0);
    patch.display.WriteString("CV Quantizer", Font_7x10, true);
    patch.display.SetCursor(0, 20);
    patch.display.WriteString("Eq & Just", Font_7x10, true);
    patch.display.Update();

    patch.StartAdc();
    patch.StartAudio(AudioCallback);

    // Now update the display in the main loop (only text, no graphics)
    while(1)
    {
        patch.display.Fill(false);
        char buf[32];

        patch.display.SetCursor(0, 0);
        snprintf(buf, sizeof(buf), "In: %.2fV", g_inputCV);
        patch.display.WriteString(buf, Font_7x10, true);

        patch.display.SetCursor(0, 12);
        snprintf(buf, sizeof(buf), "Eq: %.2fV", g_eqCV);
        patch.display.WriteString(buf, Font_7x10, true);

        patch.display.SetCursor(0, 24);
        snprintf(buf, sizeof(buf), "Just: %.2fV", g_justCV);
        patch.display.WriteString(buf, Font_7x10, true);

        patch.display.Update();
        patch.DelayMs(100);
    }
}
