#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdlib.h>

// screen dimensions
#define SCREEN_WIDTH_TOP 400
#define SCREEN_HEIGHT_TOP 240
#define SCREEN_WIDTH_BOTTOM 320
#define SCREEN_HEIGHT_BOTTOM 240

// palette
#define COLOR_BG_NAVY C2D_Color32(0, 6, 38, 255)
#define COLOR_GLASS_PLAYER C2D_Color32(0, 6, 38, 128)            // 0.5 alpha
#define COLOR_GLASS_CHAT C2D_Color32(0, 0, 0, 51)                // 0.2 alpha

#define COLOR_TEXT_PRIMARY C2D_Color32(255, 255, 255, 255)
#define COLOR_TEXT_SECONDARY C2D_Color32(255, 255, 255, 127)     // 0.8 alpha
#define COLOR_TEXT_MUTED C2D_Color32(255, 255, 255, 102)         // 0.4 alpha

#define COLOR_ACCENT_PURPLE_START C2D_Color32(123, 75, 255, 255) // #7b4bff
#define COLOR_ACCENT_PURPLE_END C2D_Color32(155, 109, 255, 255)  // #9b6dff
#define COLOR_COMMAND C2D_Color32(162, 155, 254, 255)            // #a29bfe
#define COLOR_TYPING C2D_Color32(255, 255, 0, 255)               // #ffff00

// layout constants
#define PADDING 10.0f
#define LINE_HEIGHT_SMALL 15.0f
#define LINE_HEIGHT_NORMAL 20.0f

// helper to parse hex colors
static inline u32 ParseColor(const char *hex) {
    if (!hex)
        return C2D_Color32(0, 255, 255, 255);

    // skip # if present
    const char *p = hex;
    if (*p == '#')
        p++;

    // check length
    size_t len = 0;
    while (p[len] && len < 6)
        len++;
    if (len < 6)
        return C2D_Color32(0, 255, 255, 255);

    unsigned int r = 0, g = 0, b = 0;
    char buf[3] = {0};

    // R
    buf[0] = p[0];
    buf[1] = p[1];
    r      = strtoul(buf, NULL, 16);

    // G
    buf[0] = p[2];
    buf[1] = p[3];
    g      = strtoul(buf, NULL, 16);

    // B
    buf[0] = p[4];
    buf[1] = p[5];
    b      = strtoul(buf, NULL, 16);

    return C2D_Color32(r, g, b, 255);
}
