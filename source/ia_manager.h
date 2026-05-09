#ifndef IA_MANAGER_H
#define IA_MANAGER_H

#include <switch.h>
#include "common.h"

#define MAX_CUSTOM_CONSOLES 20
#define CONSOLES_DB_PATH "/switch/NexPS/consoles.dat"

typedef struct {
    char name[64];
    char ia_id[128];
    char path[256];
} CustomConsole;

extern CustomConsole custom_consoles[MAX_CUSTOM_CONSOLES];
extern int custom_console_count;

void ia_load_consoles();
void ia_save_consoles();
int ia_add_console(const char *name, const char *ia_id, const char *path);
void ia_remove_console(int index);
void ia_clear_allocations();
void ia_parse_metadata(char *data, const char *console_name, const char *ia_id);

#endif
