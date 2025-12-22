#include "audio.h"
#include "common.h"
#include <malloc.h>
#include <string.h>
#include <stdio.h>

#define SAMPLE_RATE         48000
#define CHANNELS            2
#define BYTES_PER_SAMPLE    2 // int16


#define NDSP_NUM_BUFFERS    4
#define NDSP_SAMPLES_PER_BUF 960 // 48000 * 0.02
#define NDSP_BUF_SIZE_BYTES (NDSP_SAMPLES_PER_BUF * CHANNELS * BYTES_PER_SAMPLE)

#define RB_SIZE             (192 * 1024)

// NDSP
static ndspWaveBuf s_waveBufs[NDSP_NUM_BUFFERS];
static int16_t *s_ndspMem = NULL; // linear memory for wavebufs
static LightEvent s_event;

// Ring Buffer
static uint8_t *s_rbMem = NULL;
static size_t s_rbReadPos = 0;
static size_t s_rbWritePos = 0;
static volatile size_t s_rbAvailable = 0; // thread-safe track
static LightLock s_rbLock;

#define DECODE_CHUNK_SAMPLES 1024 

// returns bytes written
static size_t rb_write(const void *data, size_t size) {
    LightLock_Lock(&s_rbLock);

    size_t space = RB_SIZE - s_rbAvailable;
    if (size > space) size = space;

    if (size == 0) {
        LightLock_Unlock(&s_rbLock);
        return 0;
    }

    size_t end = s_rbWritePos + size;
    if (end > RB_SIZE) {
        size_t first_part = RB_SIZE - s_rbWritePos;
        memcpy(s_rbMem + s_rbWritePos, data, first_part);
        memcpy(s_rbMem, (const uint8_t*)data + first_part, size - first_part);
        s_rbWritePos = size - first_part;
    } else {
        memcpy(s_rbMem + s_rbWritePos, data, size);
        s_rbWritePos += size;
        if (s_rbWritePos == RB_SIZE) s_rbWritePos = 0;
    }

    s_rbAvailable += size;
    
    LightLock_Unlock(&s_rbLock);
    
    // signal audio thread that data is available
    LightEvent_Signal(&s_event);
    
    return size;
}

// returns bytes read
static size_t rb_read(void *dest, size_t size) {
    LightLock_Lock(&s_rbLock);

    if (size > s_rbAvailable) size = s_rbAvailable;

    if (size == 0) {
        LightLock_Unlock(&s_rbLock);
        return 0;
    }

    size_t end = s_rbReadPos + size;
    if (end > RB_SIZE) {
        size_t first_part = RB_SIZE - s_rbReadPos;
        memcpy(dest, s_rbMem + s_rbReadPos, first_part);
        memcpy((uint8_t*)dest + first_part, s_rbMem, size - first_part);
        s_rbReadPos = size - first_part;
    } else {
        memcpy(dest, s_rbMem + s_rbReadPos, size);
        s_rbReadPos += size;
        if (s_rbReadPos == RB_SIZE) s_rbReadPos = 0;
    }

    s_rbAvailable -= size;

    LightLock_Unlock(&s_rbLock);
    return size;
}

static size_t rb_get_available(void) {
    LightLock_Lock(&s_rbLock);
    size_t avail = s_rbAvailable;
    LightLock_Unlock(&s_rbLock);
    return avail;
}

static size_t rb_get_space(void) {
    LightLock_Lock(&s_rbLock);
    size_t space = RB_SIZE - s_rbAvailable;
    LightLock_Unlock(&s_rbLock);
    return space;
}

static void ndsp_cb(void *u) {
    LightEvent_Signal(&s_event);
}

bool audio_init(void) {
    // rb init
    s_rbMem = malloc(RB_SIZE);
    if (!s_rbMem) return false;
    memset(s_rbMem, 0, RB_SIZE);
    s_rbReadPos = 0;
    s_rbWritePos = 0;
    s_rbAvailable = 0;
    LightLock_Init(&s_rbLock);

    // ndsp mem init
    size_t ndspTotalSize = NDSP_NUM_BUFFERS * NDSP_BUF_SIZE_BYTES;
    s_ndspMem = linearAlloc(ndspTotalSize);
    if (!s_ndspMem) {
        free(s_rbMem);
        s_rbMem = NULL;
        return false;
    }
    // clear audio memory
    memset(s_ndspMem, 0, ndspTotalSize);

    // ndsp init
    if (R_FAILED(ndspInit())) {
        linearFree(s_ndspMem);
        free(s_rbMem);
        s_rbMem = NULL;
        return false;
    }
    
    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(0, SAMPLE_RATE);

    // setup wavebufs
    memset(s_waveBufs, 0, sizeof(s_waveBufs));
    for (int i = 0; i < NDSP_NUM_BUFFERS; i++) {
        // calculate offset carefully
        size_t offset = i * NDSP_BUF_SIZE_BYTES;
        s_waveBufs[i].data_vaddr = (void*)((uint8_t*)s_ndspMem + offset);
        s_waveBufs[i].nsamples = NDSP_SAMPLES_PER_BUF;
        s_waveBufs[i].status = NDSP_WBUF_DONE;
    }

    LightEvent_Init(&s_event, RESET_ONESHOT);
    ndspSetCallback(ndsp_cb, NULL);

    return true;
}

void audio_exit(void) {
    ndspExit();
    
    if (s_ndspMem) {
        linearFree(s_ndspMem);
        s_ndspMem = NULL;
    }
    if (s_rbMem) {
        free(s_rbMem);
        s_rbMem = NULL;
    }
}

void audio_signal_exit(void) {
    LightEvent_Signal(&s_event);
}

// decoder thread
void audio_decoder_thread(void *arg) {
    OggOpusFile *of = (OggOpusFile *)arg;
    if (!of) return;

    int16_t tempBuf[DECODE_CHUNK_SAMPLES * CHANNELS];

    while (!s_quit) {
        // check space
        size_t space = rb_get_space();
       
        if (space < sizeof(tempBuf)) {
            svcSleepThread(10 * 1000 * 1000); // 10ms
            continue;
        }

        // decode
        int samples = op_read_stereo(of, tempBuf, DECODE_CHUNK_SAMPLES);
        
        if (samples < 0) {
            svcSleepThread(100 * 1000 * 1000); // 100ms
            continue;
        }
        
        if (samples == 0) {
            svcSleepThread(10 * 1000 * 1000);
            continue;
        }

        // write to rb
        rb_write(tempBuf, samples * CHANNELS * BYTES_PER_SAMPLE);
    }
}

int16_t *g_audio_buffer = NULL;
uint32_t g_audio_buffer_num_samples = 0;

// feeder thread
void audio_thread(void *arg) {
    (void)arg;

    while (!s_quit) {
        bool bufferFree = false;
        bool rbEmpty = false;

        // check all buffers
        for (int i = 0; i < NDSP_NUM_BUFFERS; i++) {
            if (s_waveBufs[i].status == NDSP_WBUF_DONE) {
                bufferFree = true;
                
                size_t avail = rb_get_available();
                if (avail >= NDSP_BUF_SIZE_BYTES) {
                    // fill and submit
                    rb_read(s_waveBufs[i].data_pcm16, NDSP_BUF_SIZE_BYTES);

                    g_audio_buffer = s_waveBufs[i].data_pcm16;
                    g_audio_buffer_num_samples = NDSP_SAMPLES_PER_BUF;

                    DSP_FlushDataCache(s_waveBufs[i].data_pcm16, NDSP_BUF_SIZE_BYTES);
                    s_waveBufs[i].nsamples = NDSP_SAMPLES_PER_BUF;
                    ndspChnWaveBufAdd(0, &s_waveBufs[i]);
                } else {
                    rbEmpty = true;
                }
            }
        }
        
        if (bufferFree && rbEmpty) {
            LightEvent_WaitTimeout(&s_event, 100000000LL);
        } else if (!bufferFree) {
            // cannot fill because no free buffers. wait for playback
            LightEvent_WaitTimeout(&s_event, 100000000LL);
        }
    }
}
