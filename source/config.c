#include <stdio.h>
#include <string.h>
#include "common.h"

char install_path[512] = DEFAULT_INSTALL_PATH;

void load_config() {
    FILE *f = fopen(CONFIG_PATH, "rb");
    if (f) {
        fread(&selected_threads, sizeof(int), 1, f);
        if (fread(install_path, 1, sizeof(install_path), f) <= 0) {
            strncpy(install_path, DEFAULT_INSTALL_PATH, sizeof(install_path));
        }
        fclose(f);
        if (selected_threads != 1 && selected_threads != 4 && selected_threads != 8) {
            selected_threads = 4;
        }
        install_path[sizeof(install_path) - 1] = '\0';
    } else {
        selected_threads = 4;
        strncpy(install_path, DEFAULT_INSTALL_PATH, sizeof(install_path));
    }
}

void save_config() {
    FILE *f = fopen(CONFIG_PATH, "wb");
    if (f) {
        fwrite(&selected_threads, sizeof(int), 1, f);
        fwrite(install_path, 1, sizeof(install_path), f);
        fclose(f);
    }
}
