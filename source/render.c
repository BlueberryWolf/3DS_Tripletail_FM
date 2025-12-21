#include "render.h"
#include "text_manager.h"
#include "ui_player.h"
#include "ui_chat.h"
#include "ui_cover.h"
#include <stdio.h>
#include <string.h>

static C3D_RenderTarget *top;
static C3D_RenderTarget *bottom;

void render_init(void) {
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  C2D_Prepare();

  top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

  Text_Init();
  UI_Player_Init();
  UI_Chat_Init();
  UI_Cover_Init();
}

void render_exit(void) {
  UI_Cover_Exit();
  Text_Exit();
  C2D_Fini();
  C3D_Fini();
}

void render_chat(void) {
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

  Text_BeginFrame();

  // top screen
  C2D_TargetClear(top, C2D_Color32(30, 30, 30, 255));
  C2D_SceneBegin(top);
  
  // player info
  UI_Player_Draw(0, 0, SCREEN_WIDTH_TOP, SCREEN_HEIGHT_TOP);

  // cover
  UI_Cover_Draw(270, 110, 120, 120);
  
  // saturn FFT man can you add an audio visualizer pls

  // bottom screen
  C2D_TargetClear(bottom, C2D_Color32(20, 20, 20, 255));
  C2D_SceneBegin(bottom);
  UI_Chat_Draw(0, 0, 320, 240);

  C3D_FrameEnd(0);
}
