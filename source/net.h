#pragma once
#include <3ds.h>
#include <malloc.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <stdbool.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000
#define NET_TIMEOUT_MS   5000
#define HTTP_USER_AGENT  "3DS_Tripletail_FM/1.0"

typedef struct {
    mbedtls_net_context fd;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;

    // for stream only (http partial reads)
    uint8_t *pushBuf;
    size_t pushSize;
    size_t pushPos;
} SecureCtx;

int net_init(void);
void net_exit(void);
bool connect_ssl(SecureCtx *ctx, const char *host, const char *port);
void cleanup_ssl(SecureCtx *ctx);
int read_exact(SecureCtx *ctx, uint8_t *buf, int len);
void net_send_ws(SecureCtx *ctx, const char *text);
void net_send_ws_frame(SecureCtx *ctx, int opcode, const uint8_t *data,
                       size_t len);

// buffer is null-terminated (size+1) just in case it's text, but outSize is
// actual data size
bool net_download(const char *url, uint8_t **outBuf, size_t *outSize);
