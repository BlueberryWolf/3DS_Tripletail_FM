#include "chat_net.h"
#include "chat.h"
#include "net.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#define CHAT_HOST "tripletail-socket.blueberry.coffee"
#define CHAT_PATH "/socket.io/?EIO=4&transport=websocket"
#define RECONNECT_DELAY_NS 1000000000LL // 1 second

// buffer configuration
#define CHAT_INITIAL_BUF_SIZE (32 * 1024)  // 32 KB initial
#define CHAT_MAX_BUF_SIZE (128 * 1024)     // 128 KB maximum

void chat_net_thread(void *arg) {
    SecureCtx ctx;
    chat_init();
    chat_store.netCtx = &ctx;

    size_t bufCap = CHAT_INITIAL_BUF_SIZE;
    uint8_t *accumBuf = malloc(bufCap);
    if (!accumBuf)
        return;

    while (!s_quit) {
        chat_store.isConnected = false;
        // connect
        if (!connect_ssl(&ctx, CHAT_HOST, "443")) {
            svcSleepThread(5000000000LL); // 5 sec retry if connect fails immediately
            continue;
        }

        mbedtls_net_set_nonblock(&ctx.fd);

        // handshake
        char req[512];
        snprintf(req, sizeof(req),
                 "GET %s HTTP/1.1\r\nHost: %s\r\n"
                 "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                 "Sec-WebSocket-Version: 13\r\n\r\n",
                 CHAT_PATH, CHAT_HOST);

        int w_ret;
        uint64_t connect_start = osGetTime();
        
        // send request
        while ((w_ret = mbedtls_ssl_write(&ctx.ssl, (unsigned char *)req, strlen(req))) <= 0) {
            if (w_ret != MBEDTLS_ERR_SSL_WANT_READ && w_ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
               goto reconnect;
            }
            if (osGetTime() - connect_start > 10000) // timeout
                goto reconnect;
            svcSleepThread(10000000);
        }

        // read response headers
        uint8_t ch;
        int state = 0;
        uint64_t head_start = osGetTime();
        while (state < 4) {
            if (osGetTime() - head_start > 10000) // timeout
                goto reconnect;

            int r = mbedtls_ssl_read(&ctx.ssl, &ch, 1);
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
                svcSleepThread(5000000);
                continue;
            }
            if (r <= 0)
                goto reconnect;

            if (ch == '\r')
                state = (state % 2 == 0) ? state + 1 : 0;
            else if (ch == '\n')
                state = (state % 2 == 1) ? state + 1 : 0;
            else
                state = 0;
        }

        // ws upgrade complete
        net_send_ws(&ctx, "40");
        chat_store.isConnected = true;

        uint64_t last_tick = osGetTime();
        size_t accumPos = 0;

        // message loop
        while (!s_quit && chat_store.isConnected) {
            if (osGetTime() - last_tick > 1000) {
                chat_clean_typers();
                last_tick = osGetTime();
            }

            uint8_t head[2];
            int r = mbedtls_ssl_read(&ctx.ssl, head, 2);

            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
                svcSleepThread(10 * 1000 * 1000); // 10ms
                continue;
            }
            if (r <= 0)
                break; // disconnected

            // parse frame
            bool fin = (head[0] & 0x80) != 0;
            uint64_t len = head[1] & 0x7F;

            if (len == 126) {
                uint8_t ext[2];
                if (read_exact(&ctx, ext, 2) <= 0) break;
                len = (ext[0] << 8) | ext[1];
            } else if (len == 127) {
                uint8_t ext[8];
                if (read_exact(&ctx, ext, 8) <= 0) break;
                size_t low = (ext[4] << 24) | (ext[5] << 16) | (ext[6] << 8) | ext[7];
                len = low;
            }

            // expand buffer if needed
            if (accumPos + len > bufCap - 1) {
                size_t newCap = bufCap * 2;
                while (accumPos + len > newCap - 1) newCap *= 2;
                
                // cap at maximum to prevent unbounded growth
                if (newCap > CHAT_MAX_BUF_SIZE) {
                    goto reconnect;  // message too large, disconnect
                }
                
                uint8_t *newBuf = realloc(accumBuf, newCap);
                if (!newBuf) goto reconnect;
                accumBuf = newBuf;
                bufCap = newCap;
            }

            // read payload
            if (read_exact(&ctx, accumBuf + accumPos, (int)len) <= 0)
                break;
            accumPos += len;

            if (fin) {
                accumBuf[accumPos] = 0;
                char *ptr = (char *)accumBuf;
                
                // socket.io packet type handling
                if (ptr[0] == '2') { // ping -> pong
                    net_send_ws(&ctx, "3");
                } else if (ptr[0] == '4' && ptr[1] == '2') { // message
                    chat_process_packet(ptr + 2, accumPos - 2);
                }
                
                accumPos = 0;
            }
        }

    reconnect:
        cleanup_ssl(&ctx);
        chat_store.isConnected = false;
        svcSleepThread(RECONNECT_DELAY_NS);
    }

    if (accumBuf)
        free(accumBuf);
}
