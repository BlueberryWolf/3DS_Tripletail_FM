#ifndef AUDIO_H
#define AUDIO_H

#include <3ds.h>
#include <opusfile.h>
#include <stdbool.h>

// initialize ndsp, allocate memory, setup buffers and events
bool audio_init(void);

// clean up ndsp and memory
void audio_exit(void);

// signal the audio thread to unblock (used during quit)
void audio_signal_exit(void);

// audio decoding thread
void audio_thread(void *arg);

#endif
