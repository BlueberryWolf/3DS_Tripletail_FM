#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

// global quit flag
extern volatile bool s_quit;

// thread priorities
#define THREAD_PRIO_AUDIO 0x18    // high priority for audio streaming
#define THREAD_PRIO_COVER 0x2B    // medium priority for cover art
#define THREAD_PRIO_CHAT 0x30     // lower priority for chat
#define THREAD_PRIO_METADATA 0x31 // lowest priority for metadata

// thread stack sizes
#define AUDIO_STACK_SIZE (32 * 1024)
#define CHAT_STACK_SIZE (32 * 1024)
#define METADATA_STACK_SIZE (32 * 1024)
#define COVER_STACK_SIZE (32 * 1024)

// timeouts and intervals
#define SSL_HANDSHAKE_RETRY_DELAY_MS 10
#define METADATA_REFRESH_INTERVAL_NS 10000000000LL // 10 seconds
#define COVER_CHECK_INTERVAL_NS 2000000000LL       // 2 seconds

#endif
