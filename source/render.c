#include "render.h"
#include "text_manager.h"
#include "ui_chat.h"
#include "ui_cover.h"
#include "ui_osciloscope.h"
#include "ui_player.h"
#include "ui_spectrogram.h"
#include <stdio.h>
#include <string.h>

static C3D_RenderTarget *top;
static C3D_RenderTarget *bottom;
static C2D_SpriteSheet bg_sheet;
static C2D_Image bg_image;
static bool has_bg = false;

void render_init(void) {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    bg_sheet = C2D_SpriteSheetLoad("romfs:/bg.tex3ds");
    if (bg_sheet) {
        bg_image = C2D_SpriteSheetGetImage(bg_sheet, 0);
        has_bg   = true;
    }

    top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    Text_Init();
    UI_Player_Init();
    UI_Chat_Init();
    UI_Cover_Init();
    UI_Osciloscope_Init();
    UI_Spectrogram_Init();
}

void render_exit(void) {
    UI_Cover_Exit();
    UI_Chat_Exit();
    UI_Osciloscope_Exit();
    UI_Spectrogram_Exit();
    Text_Exit();
    if (bg_sheet)
        C2D_SpriteSheetFree(bg_sheet);
    C2D_Fini();
    C3D_Fini();
}

void render_chat(void) {
    UI_Cover_CheckBuffers();
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    Text_BeginFrame();

    // top screen
    C2D_TargetClear(top, COLOR_BG_NAVY);
    C2D_SceneBegin(top);

    if (has_bg) {
        float bass = g_bassLevel;
        
        float scale = 1.0f + bass * 0.08f;
        
        float drawX = 200.0f * (1.0f - scale);
        float drawY = 120.0f * (1.0f - scale);

        C2D_DrawImageAt(bg_image, drawX, drawY, 0.5f, NULL, scale, scale);

        float alphaFloat = 0.4f - (bass * 0.2f);
        if (alphaFloat < 0.0f) alphaFloat = 0.0f;
        u8 alpha = (u8)(alphaFloat * 255.0f);
        
        if (alpha > 0) {
             C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, C2D_Color32(0, 0, 0, alpha));
        }
    }

    // saturn FFT man can you add an audio visualizer pls
    // NOTE(saturn): "no"
    UI_Spectrogram_Draw(0, 0, 400, 240);

    // osciloscope
    UI_Osciloscope_Draw(0, 0, 400, 240);

    // player info
    UI_Player_Draw(0, 0, SCREEN_WIDTH_TOP, SCREEN_HEIGHT_TOP);

    // cover
    UI_Cover_Draw(270, 20, 120, 120);

    // bottom screen
    C2D_TargetClear(bottom, COLOR_BG_NAVY);
    C2D_SceneBegin(bottom);
    UI_Chat_Draw(0, 0, 320, 240);

    C3D_FrameEnd(0);
}
