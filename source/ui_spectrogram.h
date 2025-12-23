#ifndef UI_SPECTROGRAM_H
#define UI_SPECTROGRAM_H

////////////////////////////////////////////////////////////////////////////////
// INCLUDES ////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#include "audio.h"
#include "render_types.h"
#include "ring_buffer.h"
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>

//// TODO: Get rid of the warnings from kissfft
#include "kiss_fftr.h"

////////////////////////////////////////////////////////////////////////////////
// GLOBALS /////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

CircularBuffer osc_cb;

// NOTE: this should not be bigger than CB_SIZE / 2, due to nyquist
#define SIZE_FOR_THINGIES (CB_SIZE / 8)

// here we try to align the arrays into one 32bit cache line;
// which will probably not happen? but still faster than not aligning them.
//  ;   should probably pack these in a struct but
//      that's probably not nessesary.
static float smooth[SIZE_FOR_THINGIES] __attribute__((aligned(16))) = {0};
static float timedata[CB_SIZE] __attribute__((aligned(16)))         = {0};
static const uint16_t HALF_CB   = CB_SIZE / 2;
static const float INT16_TO_FLT = 1.0F / 32768.F;
static const float MAG_MULT     = 0.5F * 0.0125F;
static kiss_fftr_cfg cfg        = NULL;

////////////////////////////////////////////////////////////////////////////////
// SPECTROGRAM /////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void UI_Spectrogram_Init(void) {
    CB_Init(&osc_cb);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#pragma GCC diagnostic ignored "-Wuninitialized"
// Taken from quake, I tought it would be funny :P
float Q_rsqrt(float number) {
    long i;
    float x2, y;
    const float threehalfs = 1.5F;

    x2 = number * 0.5F;
    y  = number;
    i  = *(long *) &y;          // evil floating point bit level hacking
    i  = 0x5f3759df - (i >> 1); // what the fuck?
    y  = *(float *) &i;
    y  = y * (threehalfs - (x2 * y * y)); // 1st iteration

    return y;
}
#pragma GCC diagnostic pop

float fast_sqrt(float x) {
    return x * Q_rsqrt(x);
}

void UI_Spectrogram_Draw(float x, float y, float w, float h) {
    if (g_audio_buffer == NULL || g_audio_buffer_num_samples == 0)
        return;

    CB_PushSamples(&osc_cb, g_audio_buffer, g_audio_buffer_num_samples);

    if (cfg == NULL) {
        cfg = kiss_fftr_alloc(CB_SIZE, 0, NULL, NULL);
    }

    // Ring Buffer Stuff
    for (int i = 0; i < CB_SIZE; i++) {
        //// TODO: get rid of modulo here
        uint16_t idx = (osc_cb.write_pos + i) % CB_SIZE;
        // timedata[i] = (float)osc_cb.left[idx] / 32768.0F;
        timedata[i] = (float) osc_cb.left[idx] * INT16_TO_FLT;
    }

    // FFT
    kiss_fft_cpx freqdata[HALF_CB + 1];
    kiss_fftr(cfg, timedata, freqdata);

    const float bar_width = w / (float) SIZE_FOR_THINGIES;
    for (int i = 0; i < SIZE_FOR_THINGIES; i++) {
        float real = freqdata[i].r;
        float imag = freqdata[i].i;
        float mag  = sqrtf(real * real + imag * imag);

        // Expensive ass EMA smoothing
        smooth[i] += (mag - smooth[i]) * 0.4F;
        mag = smooth[i];

        // Slope to make the magnitude look flat
        float slope = fast_sqrt((float) i + 2.F); ///< +6dB slope
        mag *= slope;                             //// dB == 20 * log10(slope)
        // TODO: normalize the fft properly!!
        float bar_height = mag * (h * MAG_MULT);
        if (bar_height > h)
            bar_height = h;

        float bar_x = x + i * bar_width;
        float bar_y = y + h - bar_height;

        C2D_DrawRectSolid(bar_x, bar_y, 0.5F, bar_width - 1.0F, bar_height,
                          C2D_Color32(60, 60, 60, 255));
    }
}

void UI_Spectrogram_Exit(void) {
    if (cfg != NULL) {
        kiss_fftr_free(cfg);
        cfg = NULL;
    }
}

#endif // UI_SPECTROGRAM_H
