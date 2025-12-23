#include "ui_chat.h"
#include "text_manager.h"
#include <stdio.h>
#include <string.h>

static C2D_TextBuf g_chatBuf;
static int g_msg_parsed_count = 0;

void UI_Chat_Init(void) {
    // large buffer for chat history
    g_chatBuf          = C2D_TextBufNew(8192);
    g_msg_parsed_count = 0;
}

void UI_Chat_Exit(void) {
    if (g_chatBuf) {
        C2D_TextBufDelete(g_chatBuf);
        g_chatBuf = NULL;
    }
}

static void RecacheAll(void) {
    C2D_TextBufClear(g_chatBuf);
    for (int i = 0; i < chat_store.count; i++) {
        chat_store.messages[i].text_cached = false;
    }
    g_msg_parsed_count = 0;
}

void UI_Chat_Draw(float x, float y, float w, float h) {
    (void) x;
    (void) w;
    float currentY   = y + h - 20;
    float scale      = 0.5f;
    float lineHeight = 16.0f;

    // typers
    if (chat_store.typer_count > 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s typing...", chat_store.typers[0].user);
        Text_Draw(0xF0000004, buf, 5, currentY, scale,
                  C2D_Color32(255, 255, 0, 255),
                  C2D_WithColor | C2D_AtBaseline);
        currentY -= lineHeight;
    }

    // pre-cache colon if needed
    static C2D_Text s_colonText;
    static bool s_colonInit = false;
    static float colonW     = 0.0f;

    if (!s_colonInit) {
        C2D_TextParse(&s_colonText, g_chatBuf, ": ");
        C2D_TextOptimize(&s_colonText);
        float h_tmp;
        C2D_TextGetDimensions(&s_colonText, scale, scale, &colonW, &h_tmp);
        s_colonInit = true;
    }

    // periodic flush of buffer to prevent overflow
    if (g_msg_parsed_count > 6000) {
        RecacheAll();
        s_colonInit = false;
    }

#define MAX_VISIBLE_MESSAGES 14
    LightLock_Lock(&chat_lock);

    int start = chat_store.count > MAX_VISIBLE_MESSAGES
                    ? chat_store.count - MAX_VISIBLE_MESSAGES
                    : 0;

    for (int i = chat_store.count - 1; i >= start; i--) {
        ChatMessage *m = &chat_store.messages[i];
        if (m->deleted)
            continue;
        if (currentY < -5) // don't draw if largely offscreen
            break;

        // cache text
        if (!m->text_cached) {
            // parse user
            C2D_TextParse(&m->userText, g_chatBuf, m->user);
            C2D_TextOptimize(&m->userText);

            // parse message
            C2D_TextParse(&m->msgText, g_chatBuf, m->text);
            C2D_TextOptimize(&m->msgText);

            m->text_cached = true;

            // count approximate usage (length of strings)
            g_msg_parsed_count += strlen(m->user) + strlen(m->text);
        }

        // draw
        float currentX = 5.0f;

        // user
        C2D_DrawText(&m->userText, C2D_WithColor | C2D_AtBaseline, currentX,
                     currentY, 0.5f, scale, scale, m->user_color_parsed);

        // width
        float userW, userH;
        C2D_TextGetDimensions(&m->userText, scale, scale, &userW, &userH);
        currentX += userW;

        // colon (use cached dimensions)
        if (!s_colonInit) {
            C2D_TextParse(&s_colonText, g_chatBuf, ": ");
            C2D_TextOptimize(&s_colonText);
            float h_tmp; // unused
            C2D_TextGetDimensions(&s_colonText, scale, scale, &colonW, &h_tmp);
            s_colonInit = true;
        }
        C2D_DrawText(&s_colonText, C2D_WithColor | C2D_AtBaseline, currentX,
                     currentY, 0.5f, scale, scale,
                     C2D_Color32(200, 200, 200, 255));
        currentX += colonW;

        // messag
        C2D_DrawText(&m->msgText, C2D_WithColor | C2D_AtBaseline, currentX,
                     currentY, 0.5f, scale, scale,
                     C2D_Color32(230, 230, 230, 255));

        currentY -= lineHeight;
    }
    LightLock_Unlock(&chat_lock);
}
