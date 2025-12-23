#include "metadata.h"
#define JSMN_STATIC
#include "jsmn.h"
#include "net.h"
#include "common.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define METADATA_BUFFER_SIZE (8 * 1024)

Metadata current_metadata = {.title = "Loading...",
                             .artist = "Tripletail FM",
                             .art = "",
                             .listeners = 0};

static const char *MD_HOST = "tripletaildash.blueberry.coffee";
static const char *MD_PATH = "/api/live/nowplaying/websocket";

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
  if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

static void json_token_str(const char *json, jsmntok_t *t, char *out,
                           size_t size) {
  int src = t->start;
  int end = t->end;
  size_t dst = 0;
  
  while (src < end && dst < size - 1) {
    if (json[src] == '\\' && src + 1 < end) {
      if (json[src + 1] == 'u' && src + 5 < end) {
        // unicode escape \uXXXX
        uint32_t cp = (hex_digit(json[src + 2]) << 12) |
                      (hex_digit(json[src + 3]) << 8) |
                      (hex_digit(json[src + 4]) << 4) |
                      (hex_digit(json[src + 5]));
        src += 6;
        
        // encode utf-8
        if (cp < 0x80) {
            if (dst < size - 1) out[dst++] = (char)cp;
        } else if (cp < 0x800) {
            if (dst < size - 2) {
                out[dst++] = (char)(0xC0 | (cp >> 6));
                out[dst++] = (char)(0x80 | (cp & 0x3F));
            }
        } else if (cp < 0x10000) {
            if (dst < size - 3) {
                out[dst++] = (char)(0xE0 | (cp >> 12));
                out[dst++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[dst++] = (char)(0x80 | (cp & 0x3F));
            }
        }
      } else {
          switch (json[src + 1]) {
            case '/':
            case '"':
            case '\\':
              out[dst++] = json[src + 1];
              src += 2;
              break;
            case 'n':
              out[dst++] = '\n';
              src += 2;
              break;
            case 't':
              out[dst++] = '\t';
              src += 2;
              break;
            case 'r':
              out[dst++] = '\r';
              src += 2;
              break;
            default:
              out[dst++] = json[src++];
              break;
          }
      }
    } else {
      out[dst++] = json[src++];
    }
  }
  out[dst] = '\0';
}

static void parse_meta_json(const char *json, size_t len) {
  jsmn_parser p;
  jsmn_init(&p);

  static jsmntok_t tokens[2048];
  int ret = jsmn_parse(&p, json, len, tokens, 2048);
  if (ret < 0)
    return;

  char title[128] = "";
  char artist[128] = "";
  char art[256] = "";

  for (int i = 1; i < ret; i++) {
    if (tokens[i].type == JSMN_STRING &&
        jsoneq(json, &tokens[i], "song") == 0) {
      // found song object key. next is object.
      if (i + 1 < ret && tokens[i + 1].type == JSMN_OBJECT) {
        int song_idx = i + 1;
        int end = tokens[song_idx].end;
        int cur = song_idx + 1;
        while (cur < ret && tokens[cur].start < end) {
          if (tokens[cur].type == JSMN_STRING) {
            if (jsoneq(json, &tokens[cur], "title") == 0) {
              json_token_str(json, &tokens[cur + 1], title, sizeof(title));
            } else if (jsoneq(json, &tokens[cur], "artist") == 0) {
              json_token_str(json, &tokens[cur + 1], artist, sizeof(artist));
            } else if (jsoneq(json, &tokens[cur], "art") == 0) {
              json_token_str(json, &tokens[cur + 1], art, sizeof(art));
            }
            if (tokens[cur + 1].type == JSMN_OBJECT ||
                tokens[cur + 1].type == JSMN_ARRAY) {
              int val_end = tokens[cur + 1].end;
              cur++;
              while (cur < ret && tokens[cur].start < val_end)
                cur++;
            } else {
              cur += 2;
            }
            continue;
          }

          // unknown key, skip value
          int val_end = tokens[cur + 1].end;
          cur++;
          while (cur < ret && tokens[cur].start < val_end)
            cur++;
        }
        break; // done song
      }
    }
  }

  if (title[0]) {
    strncpy(current_metadata.title, title, sizeof(current_metadata.title));
    strncpy(current_metadata.artist, artist, sizeof(current_metadata.artist));
    strncpy(current_metadata.art, art, sizeof(current_metadata.art));
    LightEvent_Signal(&g_metadata_event);
  }
}

LightEvent g_metadata_event;

void metadata_init(void) {
    LightEvent_Init(&g_metadata_event, RESET_ONESHOT);
}

void metadata_refresh(void) {
  SecureCtx ctx = {0};

  strncpy(current_metadata.title, "Connecting...",
          sizeof(current_metadata.title));

  if (!connect_ssl(&ctx, MD_HOST, "443")) {
    strncpy(current_metadata.title, "Connect Failed",
            sizeof(current_metadata.title));
    return;
  }

  mbedtls_net_set_nonblock(&ctx.fd);

  strncpy(current_metadata.title, "Handshaking...",
          sizeof(current_metadata.title));

  char req[512];
  snprintf(req, sizeof(req),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
           "Sec-WebSocket-Version: 13\r\n\r\n",
           MD_PATH, MD_HOST);
  mbedtls_ssl_write(&ctx.ssl, (const unsigned char *)req, strlen(req));

  size_t buf_cap = METADATA_BUFFER_SIZE;
  char *buf = malloc(buf_cap);
  if (!buf) {
    cleanup_ssl(&ctx);
    return;
  }

  int received = 0;
  int header_end = -1;

  while ((size_t)received < buf_cap - 1) {
    int r = mbedtls_ssl_read(&ctx.ssl, (unsigned char *)buf + received,
                             buf_cap - 1 - received);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        svcSleepThread(10 * 1000 * 1000); // 10ms for handshake
        continue;
    }
    if (r <= 0)
      break;
    received += r;

    for (int i = 0; i < received - 3; i++) {
      if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
          buf[i + 3] == '\n') {
        header_end = i + 4;
        break;
      }
    }
    if (header_end != -1)
      break;
  }

  if (header_end == -1) {
    strncpy(current_metadata.title, "HS Read Failed",
            sizeof(current_metadata.title));
    free(buf);
    cleanup_ssl(&ctx);
    return;
  }

  strncpy(current_metadata.title, "Subscribing...",
          sizeof(current_metadata.title));

  const char *sub = "{\"subs\": {\"station:tripletail\": {\"recover\": true}}}";
  net_send_ws(&ctx, sub);

  int buf_len = received - header_end;
  if (buf_len > 0) {
    memmove(buf, buf + header_end, buf_len);
  } else {
    buf_len = 0;
  }

  strncpy(current_metadata.title, "Waiting for Data...",
          sizeof(current_metadata.title));

  while (1) {
    if (!s_enable_metadata || s_quit) goto disconnected;

    while (buf_len >= 2) {
      unsigned char *p = (unsigned char *)buf;
      int opcode = p[0] & 0x0F;
      int payload_len = p[1] & 0x7F;
      int head_len = 2;

      if (payload_len == 126) {
        if (buf_len < 4)
          break;
        payload_len = (p[2] << 8) | p[3];
        head_len = 4;
      } else if (payload_len == 127) {
        // not supported
        buf_len = 0; // desync, kill
        break;
      }

      int total_len = head_len + payload_len;
      if (buf_len >= total_len) {
        // valid frame

        if (opcode == 0x1) { // text
          buf[total_len] = '\0';
          parse_meta_json((char *)p + head_len, payload_len);
        } else if (opcode == 0x8) { // close
          goto disconnected;
        } else if (opcode == 0x9) { // ping
          // reply with pong (opcode 0xA)
          net_send_ws_frame(&ctx, 0xA, p + head_len, payload_len);
        }

        buf_len -= total_len;
        if (buf_len > 0)
          memmove(buf, buf + total_len, buf_len);
      } else {
        break; // need more data
      }
    }

    if ((size_t)buf_len >= buf_cap - 1) {
      // buffer full, fatal
      goto disconnected;
    }

    int r = mbedtls_ssl_read(&ctx.ssl, (unsigned char *)buf + buf_len,
                             buf_cap - 1 - buf_len);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
      svcSleepThread(100 * 1000 * 1000); // 100ms polling for metadata is plenty
      continue;
    }
    if (r <= 0)
      break; // disconnected

    buf_len += r;
  }

disconnected:
  strncpy(current_metadata.title, "Disconnected",
          sizeof(current_metadata.title));
  free(buf);
  cleanup_ssl(&ctx);
  svcSleepThread(1000 * 1000 * 1000); // wait 1s before allowing potential reconnect
}
