#include "daisy_patch.h"
#include "daisysp.h"
#include <cmath>
#include <cstdio>

using namespace daisy;
using namespace daisysp;

DaisyPatch patch;

// Global variables (declared volatile since they're updated in the audio callback)
// These will be read by the main loop to update the display.
volatile float g_inputCV = 0.0f;
volatile float g_eqCV    = 0.0f;
volatile float g_justCV  = 0.0f;

//---------------------------------------------------------------------
// Helper: DrawFilledRect
//
// This template helper draws a filled rectangle on the display by drawing
// horizontal lines. The display parameter is templated to accept any
// instantiation of OledDisplay.
// Parameters:
//   display - the OledDisplay instance
//   x, y    - top-left coordinates of the rectangle
//   width, height - dimensions of the rectangle
//   color   - the fill color (true for white, false for black)
//---------------------------------------------------------------------
template<typename T>
void DrawFilledRect(OledDisplay<T>& display, int x, int y, int width, int height, bool color)
{
    for(int j = y; j < y + height; j++)
    {
        display.DrawLine(x, j, x + width - 1, j, color);
    }
}

//---------------------------------------------------------------------
// QuantizeCV
//
// This function takes an input CV (in volts, where 1V/octave applies)
// and quantizes it to the nearest semitone. If useJustIntonation is true,
// it remaps the quantized semitone to a just intonation value using a lookup table.
//---------------------------------------------------------------------
float QuantizeCV(float inputCV, bool useJustIntonation)
{
    // Separate the input into its octave (integer part) and fractional part (0–1)
    int octave = static_cast<int>(std::floor(inputCV));
    float frac = inputCV - octave;

    // Determine the nearest semitone index (0–11)
    int semitoneIndex = static_cast<int>(std::round(frac * 12.0f));
    // Handle rounding that might push us into the next octave.
    if(semitoneIndex >= 12)
    {
        semitoneIndex = 0;
        octave += 1;
    }

    if(!useJustIntonation)
    {
        // Equal temperament: simply rebuild the CV value.
        return octave + (semitoneIndex / 12.0f);
    }
    else
    {
        // Lookup table for one common set of just intonation ratios.
        // (Note: many just intonation mappings exist; this is one possibility.)
        static const float justRatios[12] = {
            1.0f,          // Unison
            16.0f / 15.0f, // minor 2nd
            9.0f  / 8.0f,  // major 2nd
            6.0f  / 5.0f,  // minor 3rd
            5.0f  / 4.0f,  // major 3rd
            4.0f  / 3.0f,  // perfect 4th
            45.0f / 32.0f, // Tritone (one possibility)
            3.0f  / 2.0f,  // perfect 5th
            8.0f  / 5.0f,  // minor 6th
            5.0f  / 3.0f,  // major 6th
            9.0f  / 5.0f,  // minor 7th
            15.0f / 8.0f   // major 7th
        };
        float ratio = justRatios[semitoneIndex];
        // Convert the ratio to a voltage offset in a logarithmic (1V/octave) system:
        float justFracCV = std::log2(ratio);
        return octave + justFracCV;
    }
}

//---------------------------------------------------------------------
// AudioCallback
//
// This function is called in real time. Here we read a CV (simulated from CTRL_1),
// compute its quantized versions, update DAC outputs, and store values into globals
// for display updates in the main loop.
//---------------------------------------------------------------------
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // Update ADC/digital controls (e.g., knob values)
    patch.ProcessAllControls();

    // For demonstration, we use CTRL_1 (a knob/CV input) as our CV source.
    // The knob value is normalized (0.0 to 1.0), so we map it to a 0–8V range.
    float knob_val = patch.GetKnobValue(DaisyPatch::CTRL_1);
    float inputCV = knob_val * 8.0f;

    // Quantize to equal temperament and just intonation:
    float eqCV   = QuantizeCV(inputCV, false);
    float justCV = QuantizeCV(inputCV, true);

    // Update the global variables for display:
    g_inputCV = inputCV;
    g_eqCV    = eqCV;
    g_justCV  = justCV;

    // Convert quantized voltages (in volts) to 12-bit DAC values.
    // Here we assume that 0–8V corresponds roughly to 0–4095.
    uint16_t eqDac   = static_cast<uint16_t>(std::round((eqCV   / 8.0f) * 4095.0f));
    uint16_t justDac = static_cast<uint16_t>(std::round((justCV / 8.0f) * 4095.0f));

    patch.seed.dac.WriteValue(DacHandle::Channel::ONE, eqDac);
    patch.seed.dac.WriteValue(DacHandle::Channel::TWO, justDac);

    // Clear the audio buffers (if not used for audio processing)
    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = 0.0f;
        out[1][i] = 0.0f;
        out[2][i] = 0.0f;
        out[3][i] = 0.0f;
    }
}

//---------------------------------------------------------------------
// Main
//
// Initializes hardware and enters a loop that updates the display.
//---------------------------------------------------------------------
int main(void)
{
    // Initialize the Daisy Patch hardware
    patch.Init();
    patch.StartAdc();

    // Initialize the DAC with a configuration structure (per the current API)
    DacHandle::Config dac_config;
    patch.seed.dac.Init(dac_config);

    // Start the audio processing (which calls AudioCallback)
    patch.StartAudio(AudioCallback);

    // Main loop: update the OLED display every 100ms.
    while(1)
    {
        // Clear the display buffer (fill with black)
        patch.display.Fill(false);

        char buf[32];
        // Display the raw input voltage
        patch.display.SetCursor(0, 0);
        snprintf(buf, sizeof(buf), "In: %.2fV", g_inputCV);
        patch.display.WriteString(buf, Font_7x10, true);

        // Display the equal temperament quantized voltage
        patch.display.SetCursor(0, 12);
        snprintf(buf, sizeof(buf), "Eq: %.2fV", g_eqCV);
        patch.display.WriteString(buf, Font_7x10, true);

        // Display the just intonation quantized voltage
        patch.display.SetCursor(0, 24);
        snprintf(buf, sizeof(buf), "Just: %.2fV", g_justCV);
        patch.display.WriteString(buf, Font_7x10, true);

        // Optionally, draw a horizontal bar graph representing the equal temperament CV.
        // We'll map 0–8V to the width of the display.
        int barWidth = static_cast<int>((g_eqCV / 8.0f) * patch.display.Width());
        // Draw a rectangle border at y=40 with height 8 pixels.
        patch.display.DrawRect(0, 40, patch.display.Width(), 8, true);
        // Fill the rectangle with a horizontal bar using our helper function.
        DrawFilledRect(patch.display, 0, 40, barWidth, 8, true);

        // Push the buffer to the OLED display
        patch.display.Update();

        // Slow down the update rate so the display is readable
        patch.DelayMs(100);
    }
}
