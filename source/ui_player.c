#include "ui_player.h"
#include "metadata.h"
#include "text_manager.h"

void UI_Player_Init(void) {
    // no init needed
}

void UI_Player_Draw(float x, float y, float w, float h) {
    (void) w;
    (void) h;
    // static ids for ui elements
    const size_t ID_TITLE  = 0xF0000001;
    const size_t ID_ARTIST = 0xF0000002;
    const size_t ID_INFO   = 0xF0000003;

    float maxW = 240.0f; 

    static char s_lastTitle[256] = {0};
    static char s_lastArtist[256] = {0};
    static float s_titleScale = 1.3f;
    static float s_artistScale = 0.8f;

    // check for updates
    if (strcmp(s_lastTitle, current_metadata.title) != 0) {
        strncpy(s_lastTitle, current_metadata.title, 255);
        float titleW = Text_MeasureVisual(ID_TITLE, FONT_BLACK, current_metadata.title, 1.0f);
        s_titleScale = 1.3f;
        if (titleW * s_titleScale > maxW) {
             if (titleW > 0.1f) s_titleScale = maxW / titleW;
        }
    }

    if (strcmp(s_lastArtist, current_metadata.artist) != 0) {
        strncpy(s_lastArtist, current_metadata.artist, 255);
        float artistW = Text_MeasureVisual(ID_ARTIST, FONT_BLACK, current_metadata.artist, 1.0f);
        s_artistScale = 0.8f;
        if (artistW * s_artistScale > maxW) {
             if (artistW > 0.1f) s_artistScale = maxW / artistW;
        }
    }

    // title (big)
    Text_Draw(ID_TITLE, FONT_BLACK, current_metadata.title, x + 20, y + 20,
              s_titleScale, COLOR_TEXT_PRIMARY, C2D_WithColor);

    // artist (medium)
    Text_Draw(ID_ARTIST, FONT_REGULAR, current_metadata.artist, x + 20, y + 55,
              s_artistScale, COLOR_TEXT_SECONDARY, C2D_WithColor);

    // controls (bottom)
    Text_Draw(ID_INFO, FONT_REGULAR,
              "Controls:\nY: Username  A: Message\nStart: Exit", x + 20,
              y + 180, 0.6f, COLOR_TEXT_MUTED, C2D_WithColor);
}
