#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *SOC_buffer = NULL;

typedef struct {
    char host[256];
    char path[512];
    char port[6];
} ParsedUrl;

static bool parse_url(const char *url, ParsedUrl *out) {
    if (!url || !out) return false;
    memset(out, 0, sizeof(ParsedUrl));

    const char *prefix = "https://";
    if (strncmp(url, prefix, 8) != 0) return false; // only https

    const char *domain_start = url + 8;
    const char *path_start   = strchr(domain_start, '/');

    size_t host_len = path_start ? (size_t)(path_start - domain_start) : strlen(domain_start);
    if (host_len >= sizeof(out->host)) return false;

    memcpy(out->host, domain_start, host_len);
    out->host[host_len] = '\0';

    if (path_start) {
        size_t path_len = strlen(path_start);
        if (path_len >= sizeof(out->path)) return false;
        strncpy(out->path, path_start, sizeof(out->path) - 1);
        out->path[sizeof(out->path)-1] = '\0';
    } else {
        strncpy(out->path, "/", sizeof(out->path)-1);
        out->path[sizeof(out->path)-1] = '\0';
    }

    // default HTTPS port
    strncpy(out->port, "443", sizeof(out->port)-1);
    out->port[sizeof(out->port)-1] = '\0';

    return true;
}

int net_init(void) {
    SOC_buffer = (uint32_t *) memalign(SOC_ALIGN, SOC_BUFFERSIZE);

    if (!SOC_buffer) {
        return -1;
    }

    if (socInit((u32 *)SOC_buffer, SOC_BUFFERSIZE) != 0) {
        free(SOC_buffer);
        printf("Net init failed\n");
        return -2;
    }

    return 0;
}

void net_exit(void) {
    socExit();
    if (SOC_buffer) {
        free(SOC_buffer);
        SOC_buffer = NULL;
    }
}

void cleanup_ssl(SecureCtx *ctx) {
    if (!ctx) return;

    if (ctx->fd.fd != -1) mbedtls_net_free(&ctx->fd);

    mbedtls_ssl_free(&ctx->ssl);
    mbedtls_ssl_config_free(&ctx->conf);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_entropy_free(&ctx->entropy);
    mbedtls_x509_crt_free(&ctx->cacert);

    if (ctx->pushBuf) {
        free(ctx->pushBuf);
        ctx->pushBuf = NULL;
    }
}

bool connect_ssl(SecureCtx *ctx, const char *host, const char *port) {
    memset(ctx, 0, sizeof(SecureCtx));

    mbedtls_net_init(&ctx->fd);
    mbedtls_ssl_init(&ctx->ssl);
    mbedtls_ssl_config_init(&ctx->conf);
    mbedtls_x509_crt_init(&ctx->cacert);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
    mbedtls_entropy_init(&ctx->entropy);

    const char *pers = "3ds_net";
    if (mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy,
                             (const unsigned char *)pers, strlen(pers)) != 0) {
        cleanup_ssl(ctx);
        return false;
    }
                            

    mbedtls_ssl_config_defaults(&ctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);


    if (mbedtls_ssl_setup(&ctx->ssl, &ctx->conf) != 0 ||
        mbedtls_ssl_set_hostname(&ctx->ssl, host) != 0) {
        cleanup_ssl(ctx);
        return false;
    }

    if (mbedtls_net_connect(&ctx->fd, host, port, MBEDTLS_NET_PROTO_TCP) != 0) {
        cleanup_ssl(ctx);
        return false;
    }

    mbedtls_ssl_set_bio(&ctx->ssl, &ctx->fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    int handshake_ret;
    int retry_count                 = 0;
    const int MAX_HANDSHAKE_RETRIES = 500; // 5 seconds timeout

    while ((handshake_ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        if (handshake_ret != MBEDTLS_ERR_SSL_WANT_READ &&
            handshake_ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            // fatal error
            cleanup_ssl(ctx);
            return false;
        }

        if (++retry_count > MAX_HANDSHAKE_RETRIES) {
            // timeout
            cleanup_ssl(ctx);
            return false;
        }

        svcSleepThread(10 * 1000 * 1000); // 10ms between retries
    }

    return true;
}

static int net_write_all(SecureCtx *ctx, const uint8_t *data, size_t len) {
    size_t written = 0;
    while (written < len) {
        int ret = mbedtls_ssl_write(&ctx->ssl, data + written, len - written);
        
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            svcSleepThread(5 * 1000 * 1000); // 5ms
            continue;
        }
        
        if (ret < 0) return ret; // error
        written += ret;
    }
    return written;
}

int read_exact(SecureCtx *ctx, uint8_t *buf, int len) {
    int got = 0;
    while (got < len) {
        int r = mbedtls_ssl_read(&ctx->ssl, buf + got, len - got);
        
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            svcSleepThread(5 * 1000 * 1000);
            continue;
        }
        
        if (r <= 0) return r; // error or EOF
        got += r;
    }
    return got;
}

void net_send_ws_frame(SecureCtx *ctx, int opcode, const uint8_t *data, size_t len) {
    // construct header
    uint8_t header[14];
    int head_len = 0;

    header[head_len++] = 0x80 | (opcode & 0x0F); // FIN | Opcode

    // client->server must be masked (RFC 6455)
    if (len < 126) {
        header[head_len++] = 0x80 | len;
    } else if (len < 65536) {
        header[head_len++] = 0x80 | 126;
        header[head_len++] = (len >> 8) & 0xFF;
        header[head_len++] = len & 0xFF;
    } else {
        header[head_len++] = 0x80 | 127;
        // 64 bits (big endian)
        for (int i = 7; i >= 0; i--) {
            header[head_len++] = (len >> (i * 8)) & 0xFF;
        }
    }

    // masking Key (0 for performance on 3ds, still valid tho)
    uint32_t mask = 0; 
    memcpy(header + head_len, &mask, 4);
    head_len += 4;

    // send header
    if (net_write_all(ctx, header, head_len) < 0) return;

    // send body
    if (data && len > 0) {
        // with masking, stream directly
        net_write_all(ctx, data, len);
    }
}


void net_send_ws(SecureCtx *ctx, const char *text) {
    if (text) {
        net_send_ws_frame(ctx, 0x1, (const uint8_t *)text, strlen(text));
    }
}

bool net_download(const char *url, uint8_t **outBuf, size_t *outSize) {
    if (!url || !outBuf || !outSize) return false;
    *outBuf = NULL;
    *outSize = 0;

    ParsedUrl parsed;
    if (!parse_url(url, &parsed)) return false;

    SecureCtx ctx;
    if (!connect_ssl(&ctx, parsed.host, parsed.port)) return false;

    // build GET request
    char req[1024];
    int len = snprintf(req, sizeof(req),
                       "GET %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "User-Agent: %s\r\n"
                       "Connection: close\r\n\r\n",
                       parsed.path, parsed.host, HTTP_USER_AGENT);

    if (len < 0 || len >= (int)sizeof(req)) {
        cleanup_ssl(&ctx);
        return false;
    }

    if (net_write_all(&ctx, (uint8_t *)req, len) < 0) {
        cleanup_ssl(&ctx);
        return false;
    }

    // initial buffer
    size_t capacity = 32 * 1024;
    size_t total = 0;
    uint8_t *buf = malloc(capacity);
    if (!buf) {
        cleanup_ssl(&ctx);
        return false;
    }

    int header_end = -1;
    while (1) {
        // grow buffer if needed
        if (total >= capacity) {
            size_t new_cap = capacity * 2;
            if (new_cap > 16 * 1024 * 1024) break; // max 16 MB
            uint8_t *tmp = realloc(buf, new_cap);
            if (!tmp) break;
            buf = tmp;
            capacity = new_cap;
        }

        int r = mbedtls_ssl_read(&ctx.ssl, buf + total, capacity - total);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            svcSleepThread(1 * 1000 * 1000); // 1ms wait
            continue;
        }
        if (r <= 0) break; // EOF or error

        total += r;

        // look for end of HTTP headers
        if (header_end == -1) {
            for (size_t i = 0; i + 3 < total; i++) {
                if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
                    header_end = i + 4;
                    break;
                }
            }
        }
    }

    cleanup_ssl(&ctx);

    if (header_end != -1 && total > (size_t)header_end) {
        size_t body_size = total - header_end;
        uint8_t *body = malloc(body_size + 1);
        if (!body) {
            free(buf);
            return false;
        }
        memcpy(body, buf + header_end, body_size);
        body[body_size] = 0; // null-terminate
        free(buf);

        *outBuf = body;
        *outSize = body_size;
        return true;
    }

    free(buf);
    return false;
}