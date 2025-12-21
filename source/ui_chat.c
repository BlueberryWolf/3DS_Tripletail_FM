#include "ui_chat.h"
#include "text_manager.h"
#include <stdio.h>
#include <string.h>


void UI_Chat_Init(void) {
    // i dont neeed this anymore
}

void UI_Chat_Draw(float x, float y, float w, float h) {
  // only using y for now (maybe implement if you want saturn? or me idk lol)
  float currentY = y + h - 20;
  float scale = 0.5f;
  float lineHeight = 16.0f;

  // cache colon dimensions
  static float colonW = 0.0f, colonH = 0.0f;
  static const size_t ID_COLON = 0xF0000005;
  if (colonW == 0.0f) {
    Text_GetSize(ID_COLON, ": ", scale, &colonW, &colonH);
  }

  if (chat_store.typer_count > 0) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s typing...", chat_store.typers[0].user);
    Text_Draw(0xF0000004, buf, 5, currentY, scale, C2D_Color32(255, 255, 0, 255), C2D_WithColor | C2D_AtBaseline);
    currentY -= lineHeight;
  }

  #define MAX_VISIBLE_MESSAGES 14
  int start = chat_store.count > MAX_VISIBLE_MESSAGES ? chat_store.count - MAX_VISIBLE_MESSAGES : 0;
  
  for (int i = chat_store.count - 1; i >= start; i--) {
    ChatMessage *m = &chat_store.messages[i];
    if (m->deleted)
      continue;
    if (currentY < 10)
      break;

    // ids based on uid
    size_t id_user = m->uid * 10 + 0;
    size_t id_msg = m->uid * 10 + 2;

    float userW, userH;
    Text_GetSize(id_user, m->user, scale, &userW, &userH);

    // username
    float currentX = 5.0f;
    Text_Draw(id_user, m->user, currentX, currentY, scale, m->user_color_parsed, C2D_WithColor | C2D_AtBaseline);
    currentX += userW;

    // colon (use cached dimensions)
    Text_Draw(ID_COLON, ": ", currentX, currentY, scale, C2D_Color32(200, 200, 200, 255), C2D_WithColor | C2D_AtBaseline);
    currentX += colonW;

    // messag
    Text_Draw(id_msg, m->text, currentX, currentY, scale, C2D_Color32(230, 230, 230, 255), C2D_WithColor | C2D_AtBaseline);

    currentY -= lineHeight;
  }
}
