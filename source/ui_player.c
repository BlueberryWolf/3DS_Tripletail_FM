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
    // 0xF0000000 + offsets
    const size_t ID_TITLE  = 0xF0000001;
    const size_t ID_ARTIST = 0xF0000002;
    const size_t ID_INFO   = 0xF0000003;

    // title (big)
    Text_Draw(ID_TITLE, current_metadata.title, x + 10, y + 10, 1.0f,
              C2D_Color32(255, 255, 255, 255), C2D_WithColor);

    // artist (medium)
    Text_Draw(ID_ARTIST, current_metadata.artist, x + 10, y + 45, 0.7f,
              C2D_Color32(200, 200, 200, 255), C2D_WithColor);

    // controls (bottom)
    Text_Draw(ID_INFO, "Controls:\nY: Username  A: Message\nStart: Exit",
              x + 10, y + 180, 0.5f, C2D_Color32(150, 150, 150, 255),
              C2D_WithColor);
}
