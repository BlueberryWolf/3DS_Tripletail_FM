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

// colors
#define COLOR_TEXT_WHITE C2D_Color32(255, 255, 255, 255)
#define COLOR_TEXT_GRAY C2D_Color32(200, 200, 200, 255)
#define COLOR_TEXT_DARK_GRAY C2D_Color32(150, 150, 150, 255)
#define COLOR_BACKGROUND C2D_Color32(30, 30, 30, 255)
#define COLOR_CHAT_BG C2D_Color32(20, 20, 20, 255)
#define COLOR_TYPING C2D_Color32(255, 255, 0, 255)

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
