#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *SOC_buffer = NULL;

void net_init(void) {
  SOC_buffer = (uint32_t *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
  if (!SOC_buffer || socInit((u32 *)SOC_buffer, SOC_BUFFERSIZE) != 0) {
    printf("Net init failed\n");
    exit(EXIT_FAILURE);
  }
}

void net_exit(void) {
  socExit();
  if (SOC_buffer) {
    free(SOC_buffer);
    SOC_buffer = NULL;
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
    return false;
  }

  mbedtls_ssl_config_defaults(&ctx->conf, MBEDTLS_SSL_IS_CLIENT,
                              MBEDTLS_SSL_TRANSPORT_STREAM,
                              MBEDTLS_SSL_PRESET_DEFAULT);
  mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);
  mbedtls_ssl_setup(&ctx->ssl, &ctx->conf);
  mbedtls_ssl_set_hostname(&ctx->ssl, host);

  if (mbedtls_net_connect(&ctx->fd, host, port, MBEDTLS_NET_PROTO_TCP) != 0)
    return false;

  mbedtls_ssl_set_bio(&ctx->ssl, &ctx->fd, mbedtls_net_send, mbedtls_net_recv,
                      NULL);

  int handshake_ret;
  int retry_count = 0;
  const int MAX_HANDSHAKE_RETRIES = 50;

  while ((handshake_ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
    if (handshake_ret != MBEDTLS_ERR_SSL_WANT_READ &&
        handshake_ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      // fatal error
      return false;
    }
    
    if (++retry_count > MAX_HANDSHAKE_RETRIES) {
      // timeout
      return false;
    }
    
    svcSleepThread(10 * 1000 * 1000); // 10ms between retries
  }

  return true;
}

void cleanup_ssl(SecureCtx *ctx) {
  mbedtls_net_free(&ctx->fd);
  mbedtls_ssl_free(&ctx->ssl);
  mbedtls_ssl_config_free(&ctx->conf);
  mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
  mbedtls_entropy_free(&ctx->entropy);
  if (ctx->pushBuf) {
    free(ctx->pushBuf);
    ctx->pushBuf = NULL;
  }
}

int read_exact(SecureCtx *ctx, uint8_t *buf, int len) {
  int got = 0;
  while (got < len) {
    int r = mbedtls_ssl_read(&ctx->ssl, buf + got, len - got);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
      svcSleepThread(5 * 1000 * 1000);
      continue;
    }
    if (r <= 0)
      return r;
    got += r;
  }
  return got;
}

#define WS_SEND_BUFFER_SIZE (8 * 1024)
static uint8_t ws_send_buffer[WS_SEND_BUFFER_SIZE];
static LightLock ws_send_lock;
static bool ws_send_lock_initialized = false;

void net_send_ws(SecureCtx *ctx, const char *text) {
  size_t len = strlen(text);
  
  // initialize lock on first use
  if (!ws_send_lock_initialized) {
    LightLock_Init(&ws_send_lock);
    ws_send_lock_initialized = true;
  }
  
  LightLock_Lock(&ws_send_lock);
  
  uint8_t header[14]; // max header size
  int headLen = 0;

  header[headLen++] = 0x81;
  
  // masking is required for client -> server
  if (len < 126) {
    header[headLen++] = 0x80 | len;
  } else if (len < 65536) {
    header[headLen++] = 0x80 | 126;
    header[headLen++] = (len >> 8) & 0xFF;
    header[headLen++] = len & 0xFF;
  } else {
    // message too large fuck off lmao
    LightLock_Unlock(&ws_send_lock);
    return;
  }

  // masking key (this can just be 0)
  header[headLen++] = 0;
  header[headLen++] = 0;
  header[headLen++] = 0;
  header[headLen++] = 0;

  // check total size fits in static buffer
  size_t totalLen = headLen + len;
  if (totalLen > WS_SEND_BUFFER_SIZE) {
    LightLock_Unlock(&ws_send_lock);
    return;  // message too large for buffer
  }

  // static buffer
  memcpy(ws_send_buffer, header, headLen);
  memcpy(ws_send_buffer + headLen, text, len);

  mbedtls_ssl_write(&ctx->ssl, ws_send_buffer, totalLen);
  
  LightLock_Unlock(&ws_send_lock);
}

bool net_download(const char *url, uint8_t **outBuf, size_t *outSize) {
  if (!url || !outBuf || !outSize) return false;
  *outBuf = NULL;
  *outSize = 0;

  SecureCtx ctx;
  char host[256] = {0};
  char path[512] = "/";

  if (strncmp(url, "https://", 8) == 0) {
      const char *p = url + 8;
      const char *slash = strchr(p, '/');
      if (slash) {
          size_t hlen = slash - p;
          if (hlen > 255) hlen = 255;
          memcpy(host, p, hlen);
          strncpy(path, slash, 511);
      } else {
          strncpy(host, p, 255);
      }
  } else {
      return false; // only https rn (saturn add http if you want?)
  }

  if (!connect_ssl(&ctx, host, "443")) return false;

  char req[1024];
  int len = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: 3DS\r\nConnection: close\r\n\r\n", path, host);
  if (len < 0 || len >= (int)sizeof(req)) {
      cleanup_ssl(&ctx);
      return false;
  }

  mbedtls_ssl_write(&ctx.ssl, (unsigned char*)req, len);

  size_t capacity = 1024 * 1024; // 1mb
  uint8_t *buf = malloc(capacity);
  if (!buf) {
      cleanup_ssl(&ctx);
      return false;
  }

  size_t total = 0;
  int header_end = -1;

  while(1) {
      int r = mbedtls_ssl_read(&ctx.ssl, buf + total, capacity - total - 1);
      if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
          continue;
      }
      if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == 0) break;
      if (r < 0) break;

      total += r;

      if (header_end == -1) {
          for (int i = 0; i < (int)total - 3; i++) {
              if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
                  header_end = i + 4;
                  break;
              }
          }
      }

      if (total >= capacity - 4096) {
          capacity *= 2;
          uint8_t *newBuf = realloc(buf, capacity);
          if (!newBuf) {
              free(buf);
              cleanup_ssl(&ctx);
              return false;
          }
          buf = newBuf;
      }
  }

  cleanup_ssl(&ctx);

  if (header_end != -1) {
      size_t bodySize = total - header_end;
      uint8_t *body = malloc(bodySize + 1);
      if (body) {
          memcpy(body, buf + header_end, bodySize);
          body[bodySize] = 0;
          free(buf);
          *outBuf = body;
          *outSize = bodySize;
          return true;
      }
  }

  free(buf);
  return false;
}
