#ifndef UI_OSCILOSCOPE_H
#define UI_OSCILOSCOPE_H

////////////////////////////////////////////////////////////////////////////////
// INCLUDES ////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#include "audio.h"
#include "render_types.h"
#include "ring_buffer.h"
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>

////////////////////////////////////////////////////////////////////////////////
// GLOBALS /////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#define SMOOTHING (1.F * 0.125F) // This is the cutoff for the EMA low-pass

CircularBuffer osc_cb;

static float smoothed_values[CB_SIZE] __attribute__((aligned(32))) = {0};
static uint32_t frame_counter                                      = 64;

////////////////////////////////////////////////////////////////////////////////
// OSCILOSCOPE /////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void UI_Osciloscope_Init(void) {
    CB_Init(&osc_cb);
    frame_counter = 0;
}

void UI_Osciloscope_Draw(float x, float y, float w, float h) {
    if (g_audio_buffer == NULL || g_audio_buffer_num_samples == 0)
        return;

    CB_PushSamples(&osc_cb, g_audio_buffer, g_audio_buffer_num_samples);

    // TODO: get rid of the divisions here
    const int16_t BAR_WIDTH = CB_SIZE;
    const float step        = w / (float) BAR_WIDTH;
    const float scale       = h / (USHRT_MAX - 1);

    for (uint16_t i = 0; i < BAR_WIDTH; ++i) {
        ///< Should probably get rid of the modulo here?
        const uint16_t idx  = (osc_cb.write_pos + i) % BAR_WIDTH;
        const int16_t audio = osc_cb.left[idx];
        const float target  = (float) audio;

        // EMA
        smoothed_values[i] =
            smoothed_values[i] * (1.0F - SMOOTHING) + target * SMOOTHING;

        const float sampleX      = x + i * step;
        const float sampleHeight = smoothed_values[i] * scale;

        // colors
        u32 c_white  = C2D_Color32(255, 255, 255, 25);  // 0.1 alpha
        u32 c_purple = C2D_Color32(168, 85, 247, 230);  // 0.9 alpha
        u32 c_mix    = C2D_Color32(211, 170, 251, 128); // mid-point

        if (sampleHeight < 0) {
            C2D_DrawRectangle(sampleX, (y + (h / 2)), 0.5F, step, sampleHeight,
                              c_mix, c_mix,             // c1, c2 (center)
                              c_white, c_white          // c3, c4 (top)
            );
        } else {
            C2D_DrawRectangle(sampleX, (y + (h / 2)), 0.5F, step, sampleHeight,
                              c_mix, c_mix,             // c1, c2 (center)
                              c_purple, c_purple        // c3, c4 (bottom)
            );
        }
    }
}

void UI_Osciloscope_Exit(void) {
}

////////////////////////////////////////////////////////////////////////////////
// END /////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#endif // UI_OSCILOSCOPE_H
