#ifndef STREAM_H
#define STREAM_H

#include "net.h"
#include <opusfile.h>
#include <stdbool.h>

extern const OpusFileCallbacks STREAM_CALLBACKS;

// Connect to the radio stream (HTTPS)
// Returns true on success (and sets up ctx->pushBuf for initial data)
bool stream_connect(SecureCtx *ctx, const char *url);

#endif
