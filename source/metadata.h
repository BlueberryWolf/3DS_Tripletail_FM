#pragma once
#include <3ds.h>

typedef struct {
  char title[128];
  char artist[128];
  char art[256];
  int listeners;
} Metadata;

extern Metadata current_metadata;

void metadata_init(void);
void metadata_refresh(void);

extern LightEvent g_metadata_event;
