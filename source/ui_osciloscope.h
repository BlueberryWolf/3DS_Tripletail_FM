#ifndef UI_OSCILOSCOPE_H
#define UI_OSCILOSCOPE_H

////////////////////////////////////////////////////////////////////////////////
// INCLUDES ////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#include "render_types.h"
#include "audio.h"
#include "ring_buffer.h"
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>

////////////////////////////////////////////////////////////////////////////////
// GLOBALS /////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#define FILL_BACKGROUND 1
CircularBuffer osc_cb;

////////////////////////////////////////////////////////////////////////////////
// OSCILOSCOPE /////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void UI_Osciloscope_Init(void) {
    CB_Init(&osc_cb);
}

void UI_Osciloscope_Draw(float x, float y, float w, float h) {
    if (g_audio_buffer == NULL || g_audio_buffer_num_samples == 0) 
        return;

    CB_PushSamples(&osc_cb, g_audio_buffer, g_audio_buffer_num_samples);
    
    const float step = w / (float)CB_SIZE;
    const float centerY = y + h / 2.0F;
    const float scale = h / (USHRT_MAX-1);
    
    for (uint16_t i = 0; i < CB_SIZE; ++i) {
        const uint16_t idx = (osc_cb.write_pos + i) % CB_SIZE;
        const int16_t audio = osc_cb.left[idx];
        const float sampleX = x + i * step;
        const float sampleHeight = audio * scale;
        C2D_DrawRectSolid(sampleX, 
            centerY - sampleHeight * 0.5F, 
            0.5F, step, sampleHeight, C2D_Color32(60, 60, 60, 255));
    }
}

void UI_Osciloscope_Exit(void) {}

#endif // UI_OSCILOSCOPE_H