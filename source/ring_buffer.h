#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#define CB_SIZE (1<<8)

// TODO: Rewrite this crap

/* we use this in order to use either a modulo or a bitwise &;
 * If the RING_SIZE is a power of two, 
 * we can save a few cpu cicles I suppose. */ 
#define POWER_OF_TWO 1

typedef struct {
    int16_t left[CB_SIZE];
    int16_t right[CB_SIZE];
    uint16_t write_pos;
} CircularBuffer;

// Initialize a circular buffer instance
static void 
CB_Init(CircularBuffer* cb) {
    for (size_t i = 0; i < CB_SIZE; ++i) {
        cb->left[i] = 0;
        cb->right[i] = 0;
    }
    cb->write_pos = 0;
}

// Push a single stereo sample
static void 
CB_Push(CircularBuffer* cb, int16_t left, int16_t right) {
    cb->left[cb->write_pos] = left;
    cb->right[cb->write_pos] = right;

#if POWER_OF_TWO
    cb->write_pos = (cb->write_pos + 1) & (CB_SIZE - 1);
#else
    cb->write_pos = (cb->write_pos + 1) % (CB_SIZE - 1);
#endif
}

// Push multiple stereo samples from an interleaved array
static void 
CB_PushSamples(CircularBuffer* cb, int16_t* samples, uint16_t count) {
    uint16_t stride = (count > CB_SIZE) ? (count / CB_SIZE) : 1;
    uint16_t samples_to_push = (count > CB_SIZE) ? CB_SIZE : count;

    for (uint16_t i = 0; i < samples_to_push; ++i) {
        uint16_t idx = i * stride * 2; // stereo: left + right
        CB_Push(cb, samples[idx], samples[idx + 1]);
    }
}

#endif
