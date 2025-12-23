#ifndef STREAM_H
#define STREAM_H

#include "net.h"
#include <opusfile.h>
#include <stdbool.h>
#include <3ds.h>

#define STREAM_BUF_SIZE (512 * 1024) // 512KB Buffer

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t head; // Write pos
    size_t tail; // Read pos
    size_t count;
    
    bool eof;
    volatile bool quit;
    
    LightLock lock;
    LightEvent canRead;
    LightEvent canWrite;
    
    SecureCtx *net; 
    const char *url;
} StreamQueue;

extern const OpusFileCallbacks STREAM_CALLBACKS;

// initialize the queue
bool stream_queue_init(StreamQueue *q, size_t capacity);
void stream_queue_free(StreamQueue *q);

// thread worker that connects and downloads to the queue
void stream_download_thread(void *arg);

#endif
