#include <3ds.h>
#include <malloc.h>
#include <opusfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "chat.h"
#include "chat_net.h"
#include "common.h"
#include "metadata.h"
#include "net.h"
#include "render.h"
#include "settings.h"
#include "stream.h"

// global state
volatile bool s_quit = false;
volatile bool s_enable_chat = true;
volatile bool s_enable_cover = true;
volatile bool s_enable_metadata = true;

static const char *STREAM_URL = "https://radio.blueberry.coffee/3ds.ogg";

// threads
static void metadata_thread_func(void *arg) {
    (void)arg;
    while (!s_quit) {
        if (s_enable_metadata) {
            metadata_refresh();
            svcSleepThread(METADATA_REFRESH_INTERVAL_NS);
        } else {
             svcSleepThread(500 * 1000 * 1000); // 500ms check
        }
    }
}

int main(void) {
    // init
    gfxInitDefault();
    render_init();
    osSetSpeedupEnable(true);
    net_init();
    chat_init();
    metadata_init();
    settings_init();
    
    // register username variable (chat_store.username is in chat.h)
    settings_register_string("username", chat_store.username, sizeof(chat_store.username));
    
    // settings_load overwrites the default username set in chat_init()
    settings_load();

    // audio/stream
    StreamQueue streamQ;
    SecureCtx streamNetCtx = {0}; // alloc on stack, used by worker
    
    if (stream_queue_init(&streamQ, STREAM_BUF_SIZE)) {
        streamQ.net = &streamNetCtx;
        streamQ.url = STREAM_URL;
        
        // start background download thread
        Thread streamTh = threadCreate(stream_download_thread, &streamQ, STREAM_STACK_SIZE, THREAD_PRIO_STREAM, -1, false);
        
        // blocks until enough data is buffered to read headers
        OggOpusFile *of = op_open_callbacks(&streamQ, &STREAM_CALLBACKS, NULL, 0, NULL);
        
        if (of) {
            if (audio_init()) {
                // threads
                Thread decoderTh = threadCreate(audio_decoder_thread, of, DECODER_STACK_SIZE, THREAD_PRIO_DECODER, -1, false);
                Thread audioTh = threadCreate(audio_thread, NULL, AUDIO_STACK_SIZE, THREAD_PRIO_AUDIO, -1, false);
                Thread chatTh = threadCreate(chat_net_thread, NULL, CHAT_STACK_SIZE, THREAD_PRIO_CHAT, -1, false);
                Thread metaTh = threadCreate(metadata_thread_func, NULL, METADATA_STACK_SIZE, THREAD_PRIO_METADATA, -1, false);

                // main loop
                static char swkbd_buf[256];

                while (aptMainLoop()) {
                    hidScanInput();
                    u32 kDown = hidKeysDown();

                    if (kDown & KEY_START)
                        break;

                    // username input
                    if (kDown & KEY_Y) {
                        SwkbdState swkbd;
                        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 30);
                        swkbdSetHintText(&swkbd, "Enter Username");
                        swkbdSetInitialText(&swkbd, chat_store.username);
                        if (swkbdInputText(&swkbd, swkbd_buf, sizeof(swkbd_buf)) == SWKBD_BUTTON_CONFIRM) {
                            chat_set_username(swkbd_buf);
                            settings_save(); // manually save since the registered variable was modified
                        }
                    }

                    // chat message input
                    if (kDown & KEY_A) {
                        SwkbdState swkbd;
                        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 3, -1);
                        swkbdSetHintText(&swkbd, "Chat Message...");
                        if (swkbdInputText(&swkbd, swkbd_buf, sizeof(swkbd_buf)) == SWKBD_BUTTON_CONFIRM) {
                            chat_send_message(swkbd_buf, NULL);
                        }
                    }

#ifdef RENDER_FPS_CAP
                    static int frame_tick = 0;
                    frame_tick++;
                    int skip = 60 / RENDER_FPS_CAP;
                    if (skip < 1) skip = 1;
                    
                    if (frame_tick % skip != 0) {
                        gspWaitForVBlank();
                        continue;
                    }
#endif

                    render_chat();
                }

                // cleanup
                s_quit = true;
                streamQ.quit = true; // signal stream explicitly
                
                // signal audio thread to wake up and see exit flag
                audio_signal_exit();
                
                LightEvent_Signal(&streamQ.canRead);
                LightEvent_Signal(&streamQ.canWrite);
                
                threadJoin(decoderTh, UINT64_MAX);
                threadJoin(audioTh, UINT64_MAX);
                threadJoin(chatTh, UINT64_MAX);
                threadJoin(streamTh, UINT64_MAX);
                threadJoin(metaTh, UINT64_MAX);

                audio_exit(); // ndsp exit
            }
            op_free(of);
        }
        
        stream_queue_free(&streamQ);
    }

    cleanup_ssl(&streamNetCtx);
    chat_exit();
    net_exit();
    render_exit();
    gfxExit();
    return 0;
}
