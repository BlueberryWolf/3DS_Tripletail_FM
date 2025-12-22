#include "ui_cover.h"
#include "common.h"
#include "metadata.h"
#include "net.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <citro2d.h>

// image layout constants match C2D_Image

static Thread coverThread;
static volatile bool cover_quit = false;

static C2D_SpriteSheet coverSheet;
static C2D_Image coverImg;
static bool hasCover = false;
static char lastArtUrl[256] = {0};

// text for when the cover art isn't yet downloaded.
static C2D_TextBuf g_static_buffer;
static C2D_Font g_font;

// proxy url base
static const char *PROXY_BASE = "https://tripletail-socket.blueberry.coffee/api/convert-image?url=";

static void cover_worker(void *arg) {
    while (!cover_quit && !s_quit) {
        // check if art url changed
        char currentUrl[256];
        bool changed = false;
        
        strncpy(currentUrl, current_metadata.art, 255);
        currentUrl[255] = 0;

        if (currentUrl[0] && strcmp(lastArtUrl, currentUrl) != 0) {
            changed = true;
        }

        if (changed) {
            
            char fullUrl[1024];
            snprintf(fullUrl, sizeof(fullUrl), "%s%s", PROXY_BASE, currentUrl);

            uint8_t *data = NULL;
            size_t size = 0;
            
            if (net_download(fullUrl, &data, &size)) {
                snprintf(lastArtUrl, sizeof(lastArtUrl), "%s", currentUrl); // update last url
                
                // backend returns a valid .t3x file
                if (data && size > 16) { 
                    UI_Cover_Update(data, size, 0); 
                } else {
                    if (data) free(data);
                }
            } else {
                // retry backoff implicitly handled by loop sleep
            }
        }

        svcSleepThread(COVER_CHECK_INTERVAL_NS);
    }
}

// global handoff
static uint8_t *pendingData = NULL;
static u32 pendingW = 0;
static LightLock pendingLock;

#define COVER_DMA_BUFFER_SIZE (128 * 128 * 4)
static u8 *coverDmaBuffer = NULL;

void UI_Cover_Init(void) {
    LightLock_Init(&pendingLock);
    // pre-allocate dma buffer for texture uploads
    coverDmaBuffer = linearAlloc(COVER_DMA_BUFFER_SIZE);
    
    coverSheet = NULL; 
    
    cover_quit = false;
    coverThread = threadCreate(cover_worker, NULL, COVER_STACK_SIZE, THREAD_PRIO_COVER, -1, false);

    g_static_buffer = C2D_TextBufNew(128);
    g_font = C2D_FontLoadSystem(CFG_REGION_USA);
}

void UI_Cover_Exit(void) {
    cover_quit = true;
    threadJoin(coverThread, UINT64_MAX);
    if (coverSheet) C2D_SpriteSheetFree(coverSheet);
    
    if (pendingData) free(pendingData);
    if (coverDmaBuffer) linearFree(coverDmaBuffer);

    C2D_TextBufDelete(g_static_buffer);
    C2D_FontFree(g_font);
}

// called from worker thread
void UI_Cover_Update(u8 *artData, u32 size, u32 height_unused) {
    LightLock_Lock(&pendingLock);
    if (pendingData) free(pendingData);
    pendingData = artData;
    pendingW = size; // hack: passing size in width param
    LightLock_Unlock(&pendingLock);
}

// called from render loop before frame begin
void UI_Cover_CheckBuffers(void) {
    // check for pending updates
    if (LightLock_TryLock(&pendingLock) == 0) {
        if (pendingData) {
            
            // release old sheet
            if (coverSheet) {
                C2D_SpriteSheetFree(coverSheet);
                coverSheet = NULL;
                hasCover = false;
            }
            
            // load new sheet from .t3x data
            coverSheet = C2D_SpriteSheetLoadFromMem(pendingData, pendingW);
            
            if (coverSheet) {
                coverImg = C2D_SpriteSheetGetImage(coverSheet, 0);
                hasCover = true;
            }
            
            free(pendingData);
            pendingData = NULL;
        }
        LightLock_Unlock(&pendingLock);
    }
}

// called from main render loop
void UI_Cover_Draw(float x, float y, float w, float h) {
    if (hasCover && coverSheet) {
        float imgW = coverImg.subtex->width;
        float imgH = coverImg.subtex->height;
        
        float scaleX = w / imgW;
        float scaleY = h / imgH;

        C2D_DrawImageAt(coverImg, x, y, 0.5f, NULL, scaleX, scaleY);
    }
    else {
        C2D_DrawRectSolid(x, y, 0.5f, w, h, C2D_Color32(40, 40, 40, 255));

        C2D_Text text;
        C2D_TextBufClear(g_static_buffer);

        const char* label_text = "fetching cover art";
        C2D_TextFontParse(&text, g_font, g_static_buffer, label_text);
        C2D_TextOptimize(&text);

        float text_w, text_h;
        C2D_TextGetDimensions(&text, 
            0.5F, 0.5F, // text scale
            &text_w, &text_h);

        const float text_x = x + (w - text_w) * 0.5F;
        const float text_y = y + (h - text_h) * 0.5F;

        C2D_DrawText(
            &text,
            C2D_WithColor,
            text_x,
            text_y,
            0.5F, // z? was y in patch but y arg is usually depth
            0.5F, // text scale
            0.5F, // text scale
            C2D_Color32(127, 127, 127, 255)
        );
    }
}
