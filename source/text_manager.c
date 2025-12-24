#define _DEFAULT_SOURCE
#include "text_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// reduced cache size since we only use this for UI labels essentially
#define CACHE_SIZE 64
// 8192 glyphs is plenty
#define BUF_SIZE 8192

// HACK: internal definitions to access glyph positions
typedef struct C2Di_Glyph_s {
    u32 lineNo;
    C3D_Tex* sheet;
    float xPos;
    float width;
    struct { float left, top, right, bottom; } texcoord;
    u32 wordNo;
} C2Di_Glyph;

struct C2D_TextBuf_s {
    u32 reserved[2];
    size_t glyphCount;
    size_t glyphBufSize;
    C2Di_Glyph glyphs[0];
};

float Text_GetVisualWidth(const C2D_Text *text) {
    if (text->end <= text->begin) return 0.0f; // empty
    
    struct C2D_TextBuf_s* buf = (struct C2D_TextBuf_s*)text->buf;
    if (!buf) return 0.0f;

    // get last glyph
    C2Di_Glyph* last = &buf->glyphs[text->end - 1];
    
    // calculate visual width using internal font scale
    typedef struct { void* cfnt; void* sheets; float textScale; } C2D_Font_Partial;
    float fontScale = ((C2D_Font_Partial*)text->font)->textScale;
    
    return (last->xPos + last->width) * fontScale;
}

typedef struct {
    size_t id;
    C2D_Text text;
    char *content;
    bool in_use;
    // cached dimensions
    float width;
    float height;
    float last_size;
    FontId font;
} TextCacheEntry;

static struct {
    C2D_TextBuf buffer;
    TextCacheEntry cache[CACHE_SIZE];
} g_tm;

static C2D_Font g_fonts[FONT_COUNT];

C2D_Font Text_GetFont(FontId id) {
    if (id >= FONT_COUNT)
        return NULL;
    if (!g_fonts[id] && id != FONT_REGULAR && g_fonts[FONT_REGULAR])
        return g_fonts[FONT_REGULAR];
    return g_fonts[id];
}

void Text_Init(void) {
    g_tm.buffer = C2D_TextBufNew(BUF_SIZE);
    memset(g_tm.cache, 0, sizeof(g_tm.cache));

    g_fonts[FONT_REGULAR]  = C2D_FontLoad("romfs:/fonts/Poppins-Regular.bcfnt");
    g_fonts[FONT_BLACK]    = C2D_FontLoad("romfs:/fonts/Poppins-Black.bcfnt");

    // fallback if regular failed
    if (!g_fonts[FONT_REGULAR]) {
        g_fonts[FONT_REGULAR] = C2D_FontLoadSystem(CFG_REGION_USA);
    }
}

void Text_Exit(void) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (g_tm.cache[i].content) {
            free(g_tm.cache[i].content);
        }
    }
    C2D_TextBufDelete(g_tm.buffer);
    for (int i = 0; i < FONT_COUNT; i++) {
        if (g_fonts[i])
            C2D_FontFree(g_fonts[i]);
    }
}

void Text_BeginFrame(void) {
    // no-op: we manage buffer dynamically based on usage
}

static void FlushCache(void) {
    C2D_TextBufClear(g_tm.buffer);
    for (int i = 0; i < CACHE_SIZE; i++) {
        g_tm.cache[i].in_use = false;
        // don't free content/id, we keep them to match against,
        // but they must be re-parsed before draw
    }
}

static TextCacheEntry *FindEntry(size_t id) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        // match id and font, or unused
        if (g_tm.cache[i].id == id) {
            return &g_tm.cache[i];
        }
    }
    return NULL;
}

static TextCacheEntry *AllocEntry(void) {
    // try to find an empty slot (never used)
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (g_tm.cache[i].id == 0) // assume ID 0 is invalid/empty
            return &g_tm.cache[i];
    }

    // evict LRU
    static int victim = 0;
    TextCacheEntry *e = &g_tm.cache[victim];
    victim            = (victim + 1) % CACHE_SIZE;

    // free old content
    if (e->content) {
        free(e->content);
        e->content = NULL;
    }
    e->in_use = false;
    return e;
}

static void UpdateEntry(TextCacheEntry *entry, size_t id, FontId font,
                        const char *str) {
    // check if we need to flush buffer, just a guess. to be safe, if > 6000,
    // flush
    if (C2D_TextBufGetNumGlyphs(g_tm.buffer) > (BUF_SIZE - 512)) {
        FlushCache();
    }

    if (entry->content && strcmp(entry->content, str) == 0 &&
        entry->font == font && entry->in_use) {
        return; // already up to date and valid
    }

    // update content
    if (!entry->content || strcmp(entry->content, str) != 0) {
        if (entry->content)
            free(entry->content);
        entry->content = strdup(str);
    }

    entry->id   = id;
    entry->font = font;

    // parse
    // if FlushCache happened above, buffer is empty.
    C2D_Font fontHandle = Text_GetFont(font);
    if (fontHandle) {
        C2D_TextFontParse(&entry->text, fontHandle, g_tm.buffer, str);
    } else {
        C2D_TextParse(&entry->text, g_tm.buffer, str);
    }
    C2D_TextOptimize(&entry->text);

    entry->in_use    = true;
    entry->last_size = 0.0f; // invalidate dimensions
}

void Text_Draw(size_t id, FontId font, const char *str, float x, float y,
               float size, u32 color, u32 flags) {
    if (!str)
        return;

    TextCacheEntry *entry = FindEntry(id);

    if (!entry || entry->font != font) {
        if (!entry)
            entry = AllocEntry(); // this might evict someone

        entry->id      = id;      // mark id
        entry->in_use  = false;   // not parsed yet
        entry->content = NULL;    // allocentry freed it or it's new
    }

    UpdateEntry(entry, id, font, str);

    // Draw
    C2D_DrawText(&entry->text, flags, x, y, 0.5f, size, size, color);
}

void Text_GetSize(size_t id, FontId font, const char *str, float size,
                  float *outW, float *outH) {
    if (!str) {
        *outW = 0;
        *outH = 0;
        return;
    }

    TextCacheEntry *entry = FindEntry(id);

    if (!entry || entry->font != font) {
        if (!entry)
            entry = AllocEntry();
        entry->id      = id;
        entry->in_use  = false;
        entry->content = NULL;
    }

    UpdateEntry(entry, id, font, str);

    if (entry->last_size == size) {
        *outW = entry->width;
        *outH = entry->height;
    } else {
        C2D_TextGetDimensions(&entry->text, size, size, outW, outH);
        entry->width     = *outW;
        entry->height    = *outH;
        entry->last_size = size;
    }
}

float Text_MeasureVisual(size_t id, FontId font, const char *str, float size) {
    if (!str) return 0.0f;

    TextCacheEntry *entry = FindEntry(id);

    if (!entry || entry->font != font) {
        if (!entry)
            entry = AllocEntry();
        entry->id      = id;
        entry->in_use  = false;
        entry->content = NULL;
    }

    UpdateEntry(entry, id, font, str);

    // get visual width based on internal glyph positions
    float rawW = Text_GetVisualWidth(&entry->text);
    return rawW * size;
}
