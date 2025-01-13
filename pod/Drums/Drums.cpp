#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisyPod      hw;
AnalogBassDrum bd;
Metro          tick;


void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    for(size_t i = 0; i < size; i++)
    {
        bool t = tick.Process();
        if(t)
        {
            bd.SetTone(.7f * random() / (float)RAND_MAX);
            bd.SetDecay(random() / (float)RAND_MAX);
            bd.SetSelfFmAmount(random() / (float)RAND_MAX);
        }

        out[0][i] = out[1][i] = bd.Process(t);
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(4);
    float sample_rate = hw.AudioSampleRate();

    bd.Init(sample_rate);
    bd.SetFreq(50.f);

    tick.Init(2.f, sample_rate);

    hw.StartAudio(AudioCallback);
    while(1) {}
}

//#include "daisysp.h"
//#include "daisy_pod.h"
//
//#define UINT32_MSB 0x80000000U
//#define MAX_LENGTH 32U
//
//using namespace daisy;
//using namespace daisysp;
//
//DaisyPod hardware;
//
//// Oscillator osc;
//AnalogBassDrum kick;
//AnalogSnareDrum snare;
//
//Metro     tick;
//Parameter lengthParam;
//
//bool    kickSeq[MAX_LENGTH];
//bool    snareSeq[MAX_LENGTH];
//uint8_t kickStep  = 0;
//uint8_t snareStep = 0;
//
//void ProcessTick();
//void ProcessControls();
//
//void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
//                   AudioHandle::InterleavingOutputBuffer out,
//                   size_t                                size)
//{
//    float kick_out, snare_out, sig;
//
//    ProcessTick();
//
//    ProcessControls();
//
//    //audio
//    for(size_t i = 0; i < size; i += 2)
//    {
//
//        kick_out = kick.Process();
//        snare_out = snare.Process();
//
//        sig = .5 * snare_out + .5 * kick_out;
//
//        out[i]     = sig;
//        out[i + 1] = sig;
//    }
//}
//
//void SetupDrums(float samplerate)
//{
//    kick.Init(samplerate);
//    kick.SetAccent(0.5);
//    kick.SetDecay(0.5);
//    kick.SetFreq(50.0f);
//    kick.SetSustain(false);
//    kick.SetTone(0.75f);
//    kick.SetAttackFmAmount(0.2);
//    kick.SetSelfFmAmount(0.2);
//
//    snare.Init(samplerate);
//    snare.SetAccent(0.3);
//    snare.SetDecay(0.4);
//    snare.SetFreq(200.0f);
//    snare.SetSnappy(0.4);
//    snare.SetSustain(false);
//    snare.SetTone(0.5);
//}
//
//void SetSeq(bool* seq, bool in)
//{
//    for(uint32_t i = 0; i < MAX_LENGTH; i++)
//    {
//        seq[i] = in;
//    }
//}
//
//int main(void)
//{
//    hardware.Init();
//    hardware.SetAudioBlockSize(4);
//    float samplerate   = hardware.AudioSampleRate();
//    float callbackrate = hardware.AudioCallbackRate();
//
//    //setup the drum sounds
//    SetupDrums(samplerate);
//
//    tick.Init(5, callbackrate);
//
//    lengthParam.Init(hardware.knob2, 1, 32, lengthParam.LINEAR);
//
//    SetSeq(snareSeq, 0);
//    SetSeq(kickSeq, 0);
//
//    hardware.StartAdc();
//    hardware.StartAudio(AudioCallback);
//
//    // Loop forever
//    for(;;) {}
//}
//
//float snareLength = 32.f;
//float kickLength  = 32.f;
//
//void IncrementSteps()
//{
//    snareStep++;
//    kickStep++;
//    snareStep %= (int)snareLength;
//    kickStep %= (int)kickLength;
//}
//
//void ProcessTick()
//{
//    if(tick.Process())
//    {
//        IncrementSteps();
//
//        if(kickSeq[kickStep])
//        {
//            kick.Trig();
//        }
//
//        if(snareSeq[snareStep])
//        {
//            snare.Trig();
//        }
//    }
//}
//
//void SetArray(bool* seq, int arrayLen, float density)
//{
//    SetSeq(seq, 0);
//
//    int ones  = (int)round(arrayLen * density);
//    int zeros = arrayLen - ones;
//
//    if(ones == 0)
//        return;
//
//    if(zeros == 0)
//    {
//        SetSeq(seq, 1);
//        return;
//    }
//
//    int oneArr[ones];
//    int zeroArr[ones];
//
//    for(int i = 0; i < ones; i++)
//    {
//        oneArr[i] = zeroArr[i] = 0;
//    }
//
//    //how many zeroes per each one
//    int idx = 0;
//    for(int i = 0; i < zeros; i++)
//    {
//        zeroArr[idx] += 1;
//        idx++;
//        idx %= ones;
//    }
//
//    //how many ones remain
//    int rem = 0;
//    for(int i = 0; i < ones; i++)
//    {
//        if(zeroArr[i] == 0)
//            rem++;
//    }
//
//    //how many ones on each prior one
//    idx = 0;
//    for(int i = 0; i < rem; i++)
//    {
//        oneArr[idx] += 1;
//        idx++;
//        idx %= ones - rem;
//    }
//
//    //fill the global seq
//    idx = 0;
//    for(int i = 0; i < (ones - rem); i++)
//    {
//        seq[idx] = 1;
//        idx++;
//
//        for(int j = 0; j < zeroArr[i]; j++)
//        {
//            seq[idx] = 0;
//            idx++;
//        }
//
//        for(int j = 0; j < oneArr[i]; j++)
//        {
//            seq[idx] = 1;
//            idx++;
//        }
//    }
//}
//
//uint8_t mode = 0;
//void    UpdateEncoder()
//{
//    mode += hardware.encoder.Increment();
//    mode = (mode % 2 + 2) % 2;
//    hardware.led2.Set(0, !mode, mode);
//}
//
//void ConditionalParameter(float o, float n, float& param, float update)
//{
//    if(abs(o - n) > 0.00005)
//    {
//        param = update;
//    }
//}
//
//float snareAmount, kickAmount = 0.f;
//float k1old, k2old            = 0.f;
//void  UpdateKnobs()
//{
//    float k1 = hardware.knob1.Process();
//    float k2 = hardware.knob2.Process();
//
//    if(mode)
//    {
//        ConditionalParameter(k1old, k1, snareAmount, k1);
//        ConditionalParameter(k2old, k2, snareLength, lengthParam.Process());
//    }
//    else
//    {
//        ConditionalParameter(k1old, k1, kickAmount, k1);
//        ConditionalParameter(k2old, k2, kickLength, lengthParam.Process());
//    }
//
//    k1old = k1;
//    k2old = k2;
//}
//
//float tempo = 3;
//void  UpdateButtons()
//{
//    if(hardware.button2.Pressed())
//    {
//        tempo += .0015;
//    }
//
//    else if(hardware.button1.Pressed())
//    {
//        tempo -= .0015;
//    }
//
//    tempo = std::fminf(tempo, 10.f);
//    tempo = std::fmaxf(.5f, tempo);
//
//    tick.SetFreq(tempo);
//    hardware.led1.Set(tempo / 10.f, 0, 0);
//}
//
//void UpdateVars()
//{
//    SetArray(snareSeq, (int)round(snareLength), snareAmount);
//    SetArray(kickSeq, (int)round(kickLength), kickAmount);
//}
//
//void ProcessControls()
//{
//    hardware.ProcessDigitalControls();
//    hardware.ProcessAnalogControls();
//
//    UpdateEncoder();
//
//    UpdateKnobs();
//
//    UpdateButtons();
//
//    UpdateVars();
//
//    hardware.UpdateLeds();
//}
