#include "ui_cover.h"
#include "common.h"
#include "metadata.h"
#include "net.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <citro2d.h>

// Image layout constants match C2D_Image

static Thread coverThread;
static volatile bool cover_quit = false;

static C3D_Tex coverTex;
static bool hasCover = false;
static char lastArtUrl[256] = {0};

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
                
                if (size == 128 * 128 * 3) {
                    u8 *rgba = malloc(128 * 128 * 4);
                    if (rgba) {
                        for (int i = 0; i < 128 * 128; i++) {
                            rgba[i * 4 + 0] = 0xFF;            // A
                            rgba[i * 4 + 3] = data[i * 3 + 2]; // R
                            rgba[i * 4 + 2] = data[i * 3 + 1]; // G
                            rgba[i * 4 + 1] = data[i * 3 + 0]; // B
                        }
                        free(data);
                        data = rgba;
                        size = 128 * 128 * 4;
                    } else {
                        free(data);
                        data = NULL;
                    }
                }

                if (data && size == 128 * 128 * 4) {
                    UI_Cover_Update(data, 128, 128);
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
static u32 pendingW = 0, pendingH = 0;
static LightLock pendingLock;

#define COVER_DMA_BUFFER_SIZE (128 * 128 * 4)
static u8 *coverDmaBuffer = NULL;

void UI_Cover_Init(void) {
    LightLock_Init(&pendingLock);
    // pre-allocate dma buffer for texture uploads
    coverDmaBuffer = linearAlloc(COVER_DMA_BUFFER_SIZE);
    
    C3D_TexInit(&coverTex, 128, 128, GPU_RGBA8); 
    
    cover_quit = false;
    coverThread = threadCreate(cover_worker, NULL, COVER_STACK_SIZE, THREAD_PRIO_COVER, -1, false);
}

void UI_Cover_Exit(void) {
    cover_quit = true;
    threadJoin(coverThread, UINT64_MAX);
    C3D_TexDelete(&coverTex);
    
    if (pendingData) free(pendingData);
    if (coverDmaBuffer) linearFree(coverDmaBuffer);
}

// called from worker thread
void UI_Cover_Update(u8 *artData, u32 width, u32 height) {
    LightLock_Lock(&pendingLock);
    if (pendingData) free(pendingData);
    pendingData = artData;
    pendingW = width;
    pendingH = height;
    LightLock_Unlock(&pendingLock);
}

// called from render loop before frame begin
void UI_Cover_CheckBuffers(void) {
    // check for pending updates
    if (LightLock_TryLock(&pendingLock) == 0) {
        if (pendingData) {

            size_t reqSize = 128 * 128 * 4;
            
            if (coverDmaBuffer) {
                memcpy(coverDmaBuffer, pendingData, reqSize);
                
                GSPGPU_FlushDataCache(coverDmaBuffer, reqSize);
                
                // rgba8 linear -> rgba8 tiled
                u32 flags = (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | 
                             GX_TRANSFER_RAW_COPY(0) | 
                             GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | 
                             GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) | 
                             GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

                GX_DisplayTransfer((u32*)osConvertVirtToPhys(coverDmaBuffer), 
                                   GX_BUFFER_DIM(128, 128),
                                   (u32*)osConvertVirtToPhys(coverTex.data), 
                                   GX_BUFFER_DIM(128, 128), 
                                   flags);
                
                // flush the texture data after transfer so the GPU sees it
                gspWaitForPPF(); // wait for transfer to finish
                GSPGPU_FlushDataCache(coverTex.data, coverTex.size);

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
    if (hasCover) {
        static const Tex3DS_SubTexture subtex = {
            .width = 128, .height = 128,
            .left = 0.0f, .top = 1.0f, .right = 1.0f, .bottom = 0.0f 
        };
        
        float scaleX = w / 128.0f;
        float scaleY = h / 128.0f;

        C2D_Image image = { .tex = &coverTex, .subtex = &subtex };
        C2D_DrawImageAt(image, x, y, 0.5f, NULL, scaleX, scaleY);
    }
}
