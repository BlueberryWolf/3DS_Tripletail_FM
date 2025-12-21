#pragma once
#include "render_types.h"

void UI_Cover_Init(void);
void UI_Cover_Exit(void);

void UI_Cover_Update(u8 *artData, u32 width, u32 height);

void UI_Cover_Draw(float x, float y, float w, float h);
