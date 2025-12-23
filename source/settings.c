#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <3ds.h>
#include <ctype.h>
#include <stdarg.h>
#include <strings.h>
#include "settings.h"

#define SETTINGS_DIR "sdmc:/tripletail"
#define SETTINGS_PATH "sdmc:/tripletail/settings.ini"
#define MAX_SETTINGS 64

// dynamic registry
typedef struct {
    const char* key;
    SettingType type;
    void* value_ptr;
    size_t size; // only used for strings
} ConfigEntry;

static ConfigEntry s_registry[MAX_SETTINGS];
static int s_registry_count = 0;

// registration functions
void settings_register_string(const char* key, char* buffer, size_t buffer_size) {
    if (s_registry_count >= MAX_SETTINGS) return;
    s_registry[s_registry_count].key = key; // assumes literal string or persistent memory
    s_registry[s_registry_count].type = TYPE_STRING;
    s_registry[s_registry_count].value_ptr = buffer;
    s_registry[s_registry_count].size = buffer_size;
    s_registry_count++;
}

void settings_register_int(const char* key, int* value) {
    if (s_registry_count >= MAX_SETTINGS) return;
    s_registry[s_registry_count].key = key;
    s_registry[s_registry_count].type = TYPE_INT;
    s_registry[s_registry_count].value_ptr = value;
    s_registry_count++;
}

void settings_register_bool(const char* key, bool* value) {
    if (s_registry_count >= MAX_SETTINGS) return;
    s_registry[s_registry_count].key = key;
    s_registry[s_registry_count].type = TYPE_BOOL;
    s_registry[s_registry_count].value_ptr = value;
    s_registry_count++;
}

// debug logging
void log_debug(const char* fmt, ...) {
    struct stat st = {0};
    if (stat(SETTINGS_DIR, &st) == -1) {
        mkdir(SETTINGS_DIR, 0777);
    }

    FILE* fp = fopen("sdmc:/tripletail/debug.log", "a");
    if (fp) {
        va_list args;
        va_start(args, fmt);
        vfprintf(fp, fmt, args);
        va_end(args);
        fprintf(fp, "\n");
        fclose(fp);
    }
}

// init/load/save
void settings_init(void) {
    // reset registry
    s_registry_count = 0;
    
    // create directory
    mkdir(SETTINGS_DIR, 0777);
}

void settings_load(void) {
    FILE* fp = fopen(SETTINGS_PATH, "r");
    if (!fp) {
        settings_save(); // create file with current defaults
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        // trim whitespace
        char* p = line;
        while (isspace((unsigned char)*p)) p++;
        
        char* end = p + strlen(p) - 1;
        while (end > p && isspace((unsigned char)*end)) {
            *end = 0;
            end--;
        }

        if (*p == 0 || *p == '#' || *p == ';') continue;

        // parse key=value
        char* val = strchr(p, '=');
        if (!val) continue;
        
        *val = '\0'; // separate key from value
        char* key = p;
        
        val++; // skip '='
        while (isspace((unsigned char)*val)) val++; // skip leading value space

        // search registry
        for (int i = 0; i < s_registry_count; i++) {
            if (strcmp(key, s_registry[i].key) == 0) {
                switch (s_registry[i].type) {
                    case TYPE_STRING:
                        strncpy((char*)s_registry[i].value_ptr, val, s_registry[i].size - 1);
                        ((char*)s_registry[i].value_ptr)[s_registry[i].size - 1] = '\0';
                        break;
                    case TYPE_INT:
                        *(int*)s_registry[i].value_ptr = atoi(val);
                        break;
                    case TYPE_BOOL:
                        if (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0)
                            *(bool*)s_registry[i].value_ptr = true;
                        else
                            *(bool*)s_registry[i].value_ptr = false;
                        break;
                }
                break;
            }
        }
    }
    
    fclose(fp);
}

void settings_save(void) {
    struct stat st = {0};
    if (stat(SETTINGS_DIR, &st) == -1) {
        mkdir(SETTINGS_DIR, 0777);
    }

    FILE* fp = fopen(SETTINGS_PATH, "w");
    if (!fp) return;

    fprintf(fp, "[General]\n");
    
    for (int i = 0; i < s_registry_count; i++) {
        switch (s_registry[i].type) {
            case TYPE_STRING:
                fprintf(fp, "%s=%s\n", s_registry[i].key, (char*)s_registry[i].value_ptr);
                break;
            case TYPE_INT:
                fprintf(fp, "%s=%d\n", s_registry[i].key, *(int*)s_registry[i].value_ptr);
                break;
            case TYPE_BOOL:
                fprintf(fp, "%s=%s\n", s_registry[i].key, (*(bool*)s_registry[i].value_ptr) ? "true" : "false");
                break;
        }
    }
    
    fclose(fp);
}
