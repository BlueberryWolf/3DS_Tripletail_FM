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
#include "stream.h"

// global state
volatile bool s_quit = false;
static const char *STREAM_URL = "https://radio.blueberry.coffee/radio.ogg";

// threads
static void metadata_thread_func(void *arg) {
    while (!s_quit) {
        metadata_refresh();
        svcSleepThread(METADATA_REFRESH_INTERVAL_NS);
    }
}

int main(void) {
    // init
    gfxInitDefault();
    render_init();
    osSetSpeedupEnable(true);
    net_init();

    // audio/stream
    SecureCtx ctx;
    if (stream_connect(&ctx, STREAM_URL)) {
        OggOpusFile *of = op_open_callbacks(&ctx, &STREAM_CALLBACKS, NULL, 0, NULL);
        if (of) {
            if (audio_init()) {
                // threads
                Thread audioTh = threadCreate(audio_thread, of, AUDIO_STACK_SIZE, THREAD_PRIO_AUDIO, -1, false);
                Thread chatTh = threadCreate(chat_net_thread, NULL, CHAT_STACK_SIZE, THREAD_PRIO_CHAT, -1, false);
                threadCreate(metadata_thread_func, NULL, METADATA_STACK_SIZE, THREAD_PRIO_METADATA, -1, true); // detached

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

                    render_chat();
                }

                // cleanup
                s_quit = true;
                
                // signal audio thread to wake up and see exit flag
                audio_signal_exit();
                
                threadJoin(audioTh, UINT64_MAX);
                threadJoin(chatTh, UINT64_MAX);
                // metaTh is detached

                audio_exit(); // ndsp exit
            }
            op_free(of);
        }
    }

    cleanup_ssl(&ctx);
    net_exit();
    render_exit();
    gfxExit();
    return 0;
}
