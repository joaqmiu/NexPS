#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "ia_manager.h"
#include "net.h"

CustomConsole custom_consoles[MAX_CUSTOM_CONSOLES];
int custom_console_count = 0;

char *ia_allocated_urls[MAX_GAMES];
char *ia_allocated_names[MAX_GAMES];
int ia_allocated_count = 0;

void ia_load_consoles() {
    FILE *f = fopen(CONSOLES_DB_PATH, "rb");
    if (f) {
        fread(&custom_console_count, sizeof(int), 1, f);
        fread(custom_consoles, sizeof(CustomConsole), custom_console_count, f);
        fclose(f);
    } else {
        custom_console_count = 0;
    }
}

void ia_save_consoles() {
    FILE *f = fopen(CONSOLES_DB_PATH, "wb");
    if (f) {
        fwrite(&custom_console_count, sizeof(int), 1, f);
        fwrite(custom_consoles, sizeof(CustomConsole), custom_console_count, f);
        fclose(f);
    }
}

int ia_add_console(const char *name, const char *ia_id, const char *path) {
    if (custom_console_count >= MAX_CUSTOM_CONSOLES) return 0;
    strncpy(custom_consoles[custom_console_count].name, name, 63);
    strncpy(custom_consoles[custom_console_count].ia_id, ia_id, 127);
    strncpy(custom_consoles[custom_console_count].path, path, 255);
    custom_console_count++;
    ia_save_consoles();
    return 1;
}

void ia_remove_console(int index) {
    if (index < 0 || index >= custom_console_count) return;
    for (int i = index; i < custom_console_count - 1; i++) {
        custom_consoles[i] = custom_consoles[i+1];
    }
    custom_console_count--;
    ia_save_consoles();
}

void ia_clear_allocations() {
    for (int i = 0; i < ia_allocated_count; i++) {
        if (ia_allocated_urls[i]) free(ia_allocated_urls[i]);
        if (ia_allocated_names[i]) free(ia_allocated_names[i]);
    }
    ia_allocated_count = 0;
}

void url_encode(char *dst, const char *src) {
    const char *hex = "0123456789ABCDEF";
    while (*src) {
        if (isalnum((unsigned char)*src) || *src == '-' || *src == '_' || *src == '.' || *src == '~') {
            *dst++ = *src;
        } else if (*src == ' ') {
            *dst++ = '%'; *dst++ = '2'; *dst++ = '0';
        } else {
            *dst++ = '%';
            *dst++ = hex[(*src >> 4) & 0xF];
            *dst++ = hex[*src & 0xF];
        }
        src++;
    }
    *dst = '\0';
}

void ia_parse_metadata(char *data, const char *console_name, const char *ia_id) {
    char *ptr = data;
    while ((ptr = strstr(ptr, "\"name\"")) != NULL) {
        if (total_games >= MAX_GAMES || ia_allocated_count >= MAX_GAMES) break;
        ptr += 6;
        while (*ptr == ' ' || *ptr == ':' || *ptr == '\t') ptr++;
        if (*ptr == '"') {
            ptr++;
            char *end = strchr(ptr, '"');
            if (end) {
                *end = '\0';
                if (!stristr(ptr, ".xml") && !stristr(ptr, ".sqlite") && 
                    !stristr(ptr, ".torrent") && !stristr(ptr, "_meta.txt") &&
                    !stristr(ptr, ".jpeg") && !stristr(ptr, ".png")) {
                    
                    all_games[total_games].id = "IA";
                    all_games[total_games].region = "ALL";
                    all_games[total_games].platform = (char*)console_name;
                    
                    char *stored_name = strdup(ptr);
                    ia_allocated_names[ia_allocated_count] = stored_name;
                    all_games[total_games].name = stored_name;
                    
                    char encoded_name[1024];
                    url_encode(encoded_name, ptr);
                    
                    char url_buf[1536];
                    snprintf(url_buf, sizeof(url_buf), "https://archive.org/download/%s/%s", ia_id, encoded_name);
                    
                    char *stored_url = strdup(url_buf);
                    ia_allocated_urls[ia_allocated_count] = stored_url;
                    
                    all_games[total_games].url = stored_url;
                    ia_allocated_count++;
                    total_games++;
                }
                ptr = end + 1;
            }
        }
    }
}
