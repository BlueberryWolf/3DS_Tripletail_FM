#include "stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int stream_read_cb(void *stream, unsigned char *ptr, int nbytes) {
    SecureCtx *s = (SecureCtx *)stream;
    int copied = 0;
    
    // read from push buffer first
    if (s->pushBuf) {
        int avail = s->pushSize - s->pushPos;
        int k = (avail > nbytes) ? nbytes : avail;
        memcpy(ptr, s->pushBuf + s->pushPos, k);
        s->pushPos += k;
        if (s->pushPos >= s->pushSize) {
            free(s->pushBuf);
            s->pushBuf = NULL;
        }
        copied = k;
        ptr += copied;
        nbytes -= copied;
        if (nbytes == 0)
            return copied;
    }
    
    while (1) {
        int ret = mbedtls_ssl_read(&s->ssl, ptr, nbytes);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            svcSleepThread(10 * 1000 * 1000); // 10ms wait
            continue;
        }
        
        if (ret < 0) return -1;
        if (ret == 0) return copied; // EOF
        
        copied += ret;
        ptr += ret;
        nbytes -= ret;
        
        if (nbytes == 0) return copied;
        // if we got partial data, we could return it, OR loop for more.
        // opusfile prefers fuller reads usually, but returning what we have is strictly valid.
        // However, blocking until full request or EOF is safer for stream stability if inconsistent packets come in.
        // let's return what we have if we got > 0
        return copied;
    }
}

const OpusFileCallbacks STREAM_CALLBACKS = {
    .read = stream_read_cb,
    .seek = NULL,
    .tell = NULL,
    .close = NULL
};

bool stream_connect(SecureCtx *ctx, const char *url) {
    if (strncmp(url, "https://", 8) != 0) return false;
    
    const char *p = url + 8; // skip https://
    char host[256] = {0};
    char path[256] = "/";
    
    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hostLen = slash - p;
        if(hostLen > 255) hostLen = 255;
        memcpy(host, p, hostLen);
        strncpy(path, slash, 255);
    } else {
        strncpy(host, p, 255);
    }

    if (!connect_ssl(ctx, host, "443"))
        return false;

    char req[512];
    int len = snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: 3DS\r\n\r\n", path,
             host);
    if(len < 0 || len >= (int)sizeof(req)) return false;

    mbedtls_ssl_write(&ctx->ssl, (unsigned char *)req, len);

    // read headers until \r\n\r\n
    uint8_t buf[1024];
    uint8_t *full = NULL;
    int size = 0, head_end = -1;
    
    while (head_end < 0) {
        int r = mbedtls_ssl_read(&ctx->ssl, buf, sizeof(buf));
        if (r <= 0)
            break;

        uint8_t *new_full = realloc(full, size + r);
        if(!new_full) {
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

    if (head_end > 0 && size > head_end) {
        ctx->pushBuf = malloc(size - head_end);
        if(ctx->pushBuf) {
            memcpy(ctx->pushBuf, full + head_end, size - head_end);
            ctx->pushSize = size - head_end;
        }
    }

    if (full)
        free(full);

    return head_end > 0;
}
