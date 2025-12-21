#pragma once
#include "render_types.h"

// initialize text manager and allocating internal buffers
void Text_Init(void);

// clean up resources
void Text_Exit(void);

// reset cache frame counter (call at start of frame)
void Text_BeginFrame(void);

void Text_Draw(size_t id, const char *str, float x, float y, float size,
               u32 color, u32 flags);

// get width/height of text without drawing (uses cache if available)
void Text_GetSize(size_t id, const char *str, float size, float *outW,
                  float *outH);
