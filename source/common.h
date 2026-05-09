#ifndef COMMON_H
#define COMMON_H

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <switch.h>

#define APP_NAME "NexPS"
#define APP_VERSION "1.1"
#define APP_DIR "/switch/" APP_NAME
#define CONFIG_PATH APP_DIR "/settings.dat"

#define MAX_GAMES 15000
#define DB_BUFFER_SIZE (12 * 1024 * 1024)
#define DEFAULT_INSTALL_PATH_PSP "/tico/roms/psp"
#define DEFAULT_INSTALL_PATH_PSX "/tico/roms/psx"

typedef struct {
    char *id;
    char *region;
    char *platform;
    char *name;
    char *url;
} GameEntry;

extern int selected_threads;
extern char install_path_psp[512];
extern char install_path_psx[512];

void load_config();
void save_config();

static inline void ui_draw_header(const char *title) {
    printf("\x1b[0;0H"); 
    printf("\x1b[45;37m"); 
    char buf[81];
    snprintf(buf, sizeof(buf), " %-78.78s", title);
    printf("%s", buf);
    printf("\x1b[0m\n"); 
}

static inline void ui_draw_footer(const char *text) {
    printf("\x1b[44;0H"); 
    printf("\x1b[45;37m"); 
    char buf[81];
    snprintf(buf, sizeof(buf), " %-78.78s", text);
    printf("%s", buf);
    printf("\x1b[0m");
}

static inline char *stristr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; ++haystack) {
        if (toupper((unsigned char)*haystack) == toupper((unsigned char)*needle)) {
            const char *h, *n;
            for (h = haystack, n = needle; *h && *n; ++h, ++n) {
                if (toupper((unsigned char)*h) != toupper((unsigned char)*n)) break;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

static inline void sanitize_filename(char *name) {
    char *p = name;
    while (*p) {
        if (!isalnum((unsigned char)*p) && *p != ' ' && *p != '-' && *p != '.') {
            *p = '_';
        }
        p++;
    }
}

#endif
