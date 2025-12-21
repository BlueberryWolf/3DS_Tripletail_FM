#define _DEFAULT_SOURCE
#include "text_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_SIZE 128
#define STATIC_BUF_SIZE 65536

typedef struct {
  size_t id;
  C2D_Text text;
  char *content;
  bool in_use;
  u64 last_used_frame;
  // cached dimensions
  float width;
  float height;
  float last_size;
} TextCacheEntry;

static struct {
  C2D_TextBuf buffer;
  TextCacheEntry cache[CACHE_SIZE];
  u64 frame_count;
} g_tm;

void Text_Init(void) {
  g_tm.buffer = C2D_TextBufNew(STATIC_BUF_SIZE);
  memset(g_tm.cache, 0, sizeof(g_tm.cache));
  g_tm.frame_count = 0;
}

void Text_Exit(void) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (g_tm.cache[i].content) {
      free(g_tm.cache[i].content);
    }
  }
  C2D_TextBufDelete(g_tm.buffer);
}

void Text_BeginFrame(void) {
  g_tm.frame_count++;

  // clear buffer every ~10 seconds
  #define BUFFER_CLEAR_THRESHOLD 600
  
  if (g_tm.frame_count % BUFFER_CLEAR_THRESHOLD == 0) {
    C2D_TextBufClear(g_tm.buffer);
    
    // invalidate all cache entries
    for (int i = 0; i < CACHE_SIZE; i++) {
      g_tm.cache[i].in_use = false;
    }
  }
}

static TextCacheEntry *FindEntry(size_t id) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (g_tm.cache[i].in_use && g_tm.cache[i].id == id) {
      return &g_tm.cache[i];
    }
  }
  return NULL;
}

static TextCacheEntry *AllocEntry(void) {
  // find empty
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (!g_tm.cache[i].in_use)
      return &g_tm.cache[i];
  }

  // find lru
  int lru = 0;
  for (int i = 1; i < CACHE_SIZE; i++) {
    if (g_tm.cache[i].last_used_frame < g_tm.cache[lru].last_used_frame) {
      lru = i;
    }
  }

  // free existing
  if (g_tm.cache[lru].content) {
    free(g_tm.cache[lru].content);
    g_tm.cache[lru].content = NULL;
  }
  g_tm.cache[lru].in_use = false;

  return &g_tm.cache[lru];
}

static void UpdateEntry(TextCacheEntry *entry, size_t id, const char *str) {
  if (entry->content && strcmp(entry->content, str) == 0) {
    return;
  }

  if (entry->content)
    free(entry->content);
  entry->content = strdup(str);
  entry->id = id;
  entry->in_use = true;
  entry->last_size = 0.0f;

  C2D_TextParse(&entry->text, g_tm.buffer, str);
  C2D_TextOptimize(&entry->text);
}

void Text_Draw(size_t id, const char *str, float x, float y, float size,
               u32 color, u32 flags) {
  if (!str)
    return;

  TextCacheEntry *entry = FindEntry(id);

  if (!entry) {
    entry = AllocEntry();
    UpdateEntry(entry, id, str);
  } else if (!entry->in_use) {
    UpdateEntry(entry, id, str);
  }

  entry->last_used_frame = g_tm.frame_count;
  C2D_DrawText(&entry->text, flags, x, y, 0.5f, size, size, color);
}

void Text_GetSize(size_t id, const char *str, float size, float *outW,
                  float *outH) {
  if (!str) {
    *outW = 0;
    *outH = 0;
    return;
  }

  TextCacheEntry *entry = FindEntry(id);
  
  if (!entry) {
    entry = AllocEntry();
    UpdateEntry(entry, id, str);
  } else if (!entry->in_use) {
    UpdateEntry(entry, id, str);
  }

  entry->last_used_frame = g_tm.frame_count;
  
  // check if there's cached dimensions for this size
  if (entry->last_size == size && entry->width != 0.0f) {
    // use cached dimensions
    *outW = entry->width;
    *outH = entry->height;
  } else {
    // calculate and cache
    C2D_TextGetDimensions(&entry->text, size, size, outW, outH);
    entry->width = *outW;
    entry->height = *outH;
    entry->last_size = size;
  }
}
