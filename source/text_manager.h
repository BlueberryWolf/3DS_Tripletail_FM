#pragma once
#include "render_types.h"

// initialize text manager and allocating internal buffers
void Text_Init(void);

// clean up resources
void Text_Exit(void);

// reset cache frame counter (call at start of frame)
void Text_BeginFrame(void);

typedef enum {
    FONT_REGULAR,
    FONT_BLACK,
    FONT_COUNT
} FontId;

C2D_Font Text_GetFont(FontId id);

void Text_Draw(size_t id, FontId font, const char *str, float x, float y,
               float size, u32 color, u32 flags);

// get width/height of text without drawing (uses cache if available)
void Text_GetSize(size_t id, FontId font, const char *str, float size,
                  float *outW, float *outH);

// get accurate visual width of text for fitting (handles cached text)
float Text_MeasureVisual(size_t id, FontId font, const char *str, float size);

// get visual width of text (includes spacing and kerning)
float Text_GetVisualWidth(const C2D_Text *text);
