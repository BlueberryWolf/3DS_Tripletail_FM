#pragma once

typedef struct {
  char title[128];
  char artist[128];
  char art[256];
  int listeners;
} Metadata;

extern Metadata current_metadata;

void metadata_init(void);
void metadata_refresh(void);
