#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

// global quit flag
extern volatile bool s_quit;
extern volatile bool s_enable_chat;
extern volatile bool s_enable_cover;
extern volatile bool s_enable_metadata;

// thread priorities
// main thread is usually 0x30.
#define THREAD_PRIO_AUDIO 0x18    // critical for feeder
#define THREAD_PRIO_DECODER 0x24  // balanced priority (between audio and main)

#define THREAD_PRIO_STREAM 0x26   // below decoder, above main
#define THREAD_PRIO_COVER 0x27    // just below stream, above main
#define THREAD_PRIO_METADATA 0x28 // important for cover art
#define THREAD_PRIO_CHAT 0x31     // lower than main

#define RENDER_FPS_CAP 60


// thread stack sizes
#define AUDIO_STACK_SIZE (8 * 1024)   // smaller stack for feeder
#define DECODER_STACK_SIZE (64 * 1024) // large stack for opus decoding
#define STREAM_STACK_SIZE (16 * 1024)

#define CHAT_STACK_SIZE (32 * 1024)
#define METADATA_STACK_SIZE (32 * 1024)
#define COVER_STACK_SIZE (32 * 1024)

// timeouts and intervals
#define SSL_HANDSHAKE_RETRY_DELAY_MS 10
#define METADATA_REFRESH_INTERVAL_NS 10000000000LL // 10 seconds
#define COVER_CHECK_INTERVAL_NS 2000000000LL       // 2 seconds

#endif
