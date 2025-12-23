#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TYPE_STRING,
    TYPE_INT,
    TYPE_BOOL
} SettingType;

// core init/load/save
void settings_init(void);
void settings_load(void);
void settings_save(void);

// dynamic registration api
// call these before settings_load()
void settings_register_string(const char* key, char* buffer, size_t buffer_size);
void settings_register_int(const char* key, int* value);
void settings_register_bool(const char* key, bool* value);

// debug logging
void log_debug(const char* fmt, ...);
