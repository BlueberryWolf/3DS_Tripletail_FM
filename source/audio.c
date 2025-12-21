#include "audio.h"
#include "common.h"
#include <malloc.h>
#include <string.h>

#define AUDIO_BUF_SIZE (48000 * 120 / 1000)

static ndspWaveBuf waveBufs[3];
static int16_t *audioMemory = NULL;
static LightEvent audioEvent;

void ndsp_cb(void *u) {
    LightEvent_Signal(&audioEvent);
}

bool audio_init(void) {
    ndspInit();
    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(0, 48000);

    size_t bufSize = AUDIO_BUF_SIZE * 2 * sizeof(int16_t);
    // allocate buffer for 3 wavebufs
    audioMemory = linearAlloc(bufSize * 3);
    if (!audioMemory) {
        return false;
    }

    memset(waveBufs, 0, sizeof(waveBufs));
    for (int i = 0; i < 3; i++) {
        waveBufs[i].data_vaddr = audioMemory + (bufSize / 2) * i;
        waveBufs[i].status = NDSP_WBUF_DONE;
    }

    ndspSetCallback(ndsp_cb, NULL);
    LightEvent_Init(&audioEvent, RESET_ONESHOT);
    
    return true;
}

void audio_exit(void) {
    ndspExit();
    if (audioMemory) {
        linearFree(audioMemory);
        audioMemory = NULL;
    }
}

void audio_signal_exit(void) {
    LightEvent_Signal(&audioEvent);
}

void audio_thread(void *arg) {
    OggOpusFile *of = (OggOpusFile *)arg;
    if (!of) return;

    while (!s_quit) {
        for (int i = 0; i < 3; i++) {
            if (waveBufs[i].status != NDSP_WBUF_DONE)
                continue;
            

            int total = 0;
            while (total < AUDIO_BUF_SIZE) {
                int n = op_read_stereo(of, waveBufs[i].data_pcm16 + total * 2,
                                       (AUDIO_BUF_SIZE - total) * 2);
                if (n <= 0)
                    break;
                total += n;
            }
            
            if (total > 0) {
                waveBufs[i].nsamples = total;
                DSP_FlushDataCache(waveBufs[i].data_pcm16, total * 4);
                ndspChnWaveBufAdd(0, &waveBufs[i]);
            }
        }
        
        LightEvent_Wait(&audioEvent);
    }
}
