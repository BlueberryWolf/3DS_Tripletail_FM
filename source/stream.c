#include "stream.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool stream_queue_init(StreamQueue *q, size_t capacity) {
    memset(q, 0, sizeof(StreamQueue));
    q->buffer = malloc(capacity);
    if (!q->buffer)
        return false;

    q->capacity = capacity;
    q->quit     = false;
    q->eof      = false;

    LightLock_Init(&q->lock);
    LightEvent_Init(&q->canRead, RESET_ONESHOT);
    LightEvent_Init(&q->canWrite, RESET_ONESHOT);

    // signal canWrite initially because it's empty
    LightEvent_Signal(&q->canWrite);

    return true;
}

void stream_queue_free(StreamQueue *q) {
    q->quit = true;
    LightEvent_Signal(&q->canRead);
    LightEvent_Signal(&q->canWrite);

    if (q->buffer) {
        free(q->buffer);
        q->buffer = NULL;
    }
}

static int stream_queue_read(void *user_data, unsigned char *ptr, int nbytes) {
    StreamQueue *q = (StreamQueue *) user_data;
    int read       = 0;

    while (nbytes > 0) {
        if (q->quit)
            return -1;

        LightLock_Lock(&q->lock);

        if (q->count == 0) {
            if (q->eof) {
                LightLock_Unlock(&q->lock);
                return read; // EOF
            }

            LightLock_Unlock(&q->lock);
            // wait for data
            LightEvent_Wait(&q->canRead);
            continue;
        }

        // read chunk
        size_t available = q->count;
        size_t chunk =
            (available > (size_t) nbytes) ? (size_t) nbytes : available;

        // handle wrap around
        size_t first_part = q->capacity - q->tail;
        if (chunk > first_part) {
            memcpy(ptr, q->buffer + q->tail, first_part);
            memcpy(ptr + first_part, q->buffer, chunk - first_part);
            q->tail = chunk - first_part;
        } else {
            memcpy(ptr, q->buffer + q->tail, chunk);
            q->tail += chunk;
            if (q->tail == q->capacity)
                q->tail = 0;
        }

        q->count -= chunk;
        if (q->count == 0) {
            LightEvent_Clear(&q->canRead);
        }

        // signal space available
        LightEvent_Signal(&q->canWrite);

        LightLock_Unlock(&q->lock);

        ptr += chunk;
        read += chunk;
        nbytes -= chunk;
    }

    return read;
}

static void stream_queue_push(StreamQueue *q, const uint8_t *data,
                              size_t size) {
    size_t written = 0;
    while (written < size && !q->quit) {
        LightLock_Lock(&q->lock);

        size_t space = q->capacity - q->count;
        if (space == 0) {
            LightLock_Unlock(&q->lock);
            LightEvent_Wait(&q->canWrite);
            continue;
        }

        size_t chunk = (size - written);
        if (chunk > space)
            chunk = space;

        size_t first_part = q->capacity - q->head;
        if (chunk > first_part) {
            memcpy(q->buffer + q->head, data + written, first_part);
            memcpy(q->buffer, data + written + first_part, chunk - first_part);
            q->head = chunk - first_part;
        } else {
            memcpy(q->buffer + q->head, data + written, chunk);
            q->head += chunk;
            if (q->head == q->capacity)
                q->head = 0;
        }

        q->count += chunk;
        LightEvent_Signal(&q->canRead);

        if (q->count == q->capacity) {
            LightEvent_Clear(&q->canWrite);
        }

        LightLock_Unlock(&q->lock);
        written += chunk;
    }
}

static bool internal_connect(SecureCtx *ctx, const char *url, uint8_t **pushBuf,
                             size_t *pushSize) {
    if (strncmp(url, "https://", 8) != 0)
        return false;

    const char *p  = url + 8;
    char host[256] = {0};
    char path[256] = "/";

    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hostLen = slash - p;
        if (hostLen > 255)
            hostLen = 255;
        memcpy(host, p, hostLen);
        strncpy(path, slash, 255);
    } else {
        strncpy(host, p, 255);
    }

    if (!connect_ssl(ctx, host, "443"))
        return false;

    char req[512];
    int len = snprintf(req, sizeof(req),
                       "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: 3DS\r\n\r\n",
                       path, host);

    mbedtls_ssl_write(&ctx->ssl, (unsigned char *) req, len);

    // read headers until \r\n\r\n
    uint8_t buf[1024];
    uint8_t *full = NULL;
    int size = 0, head_end = -1;

    while (head_end < 0) {
        int r = mbedtls_ssl_read(&ctx->ssl, buf, sizeof(buf));
        if (r <= 0)
            break;

        uint8_t *new_full = realloc(full, size + r);
        if (!new_full) {
            free(full);
            return false;
        }
        full = new_full;

        memcpy(full + size, buf, r);
        size += r;

        for (int i = 0; i < size - 3; i++) {
            if (memcmp(full + i, "\r\n\r\n", 4) == 0) {
                head_end = i + 4;
                break;
            }
        }
    }

    if (head_end > 0) {
        // return remaining data
        if (size > head_end) {
            *pushSize = size - head_end;
            *pushBuf  = malloc(*pushSize);
            if (*pushBuf)
                memcpy(*pushBuf, full + head_end, *pushSize);
        } else {
            *pushBuf  = NULL;
            *pushSize = 0;
        }
        free(full);
        return true;
    }

    if (full)
        free(full);
    return false;
}

void stream_download_thread(void *arg) {
    StreamQueue *q = (StreamQueue *) arg;
    if (!q || !q->net)
        return;

    while (!s_quit && !q->quit) {
        // connect
        uint8_t *initData = NULL;
        size_t initSize   = 0;

        if (internal_connect(q->net, q->url, &initData, &initSize)) {
            // push initial data
            if (initData && initSize > 0) {
                stream_queue_push(q, initData, initSize);
                free(initData);
            }

            // loop read
            uint8_t buf[4096];
            while (!s_quit && !q->quit) {
                int ret = mbedtls_ssl_read(&q->net->ssl, buf, sizeof(buf));

                if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                    ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    svcSleepThread(10 * 1000 *
                                   1000); // 10ms wait is fine in this thread
                    continue;
                }

                if (ret <= 0)
                    break; // error or eof

                stream_queue_push(q, buf, ret);

                // yield slightly to let other network threads (cover art) run
                svcSleepThread(1000 * 1000);
            }

            cleanup_ssl(q->net);
        } else {
            // connect failed
            svcSleepThread(1000 * 1000 * 1000); // 1s retry delay
        }

        if (!s_quit && !q->quit) {
            // wait slightly before reconnect
            svcSleepThread(100 * 1000 * 1000);
        }
    }

    q->eof = true;
    LightEvent_Signal(&q->canRead);
}

const OpusFileCallbacks STREAM_CALLBACKS = {
    .read = stream_queue_read, .seek = NULL, .tell = NULL, .close = NULL};
