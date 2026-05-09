#include <stdio.h>
#include <string.h>
#include "common.h"

char install_path_psp[512] = DEFAULT_INSTALL_PATH_PSP;
char install_path_psx[512] = DEFAULT_INSTALL_PATH_PSX;
int selected_threads = 4;

void load_config() {
    FILE *f = fopen(CONFIG_PATH, "rb");
    if (f) {
        fread(&selected_threads, sizeof(int), 1, f);
        if (fread(install_path_psp, 1, sizeof(install_path_psp), f) <= 0) {
            strncpy(install_path_psp, DEFAULT_INSTALL_PATH_PSP, sizeof(install_path_psp));
        }
        if (fread(install_path_psx, 1, sizeof(install_path_psx), f) <= 0) {
            strncpy(install_path_psx, DEFAULT_INSTALL_PATH_PSX, sizeof(install_path_psx));
        }
        fclose(f);
        if (selected_threads != 1 && selected_threads != 4 && selected_threads != 8) {
            selected_threads = 4;
        }
        install_path_psp[sizeof(install_path_psp) - 1] = '\0';
        install_path_psx[sizeof(install_path_psx) - 1] = '\0';
    } else {
        selected_threads = 4;
        strncpy(install_path_psp, DEFAULT_INSTALL_PATH_PSP, sizeof(install_path_psp));
        strncpy(install_path_psx, DEFAULT_INSTALL_PATH_PSX, sizeof(install_path_psx));
    }
}

void save_config() {
    FILE *f = fopen(CONFIG_PATH, "wb");
    if (f) {
        fwrite(&selected_threads, sizeof(int), 1, f);
        fwrite(install_path_psp, 1, sizeof(install_path_psp), f);
        fwrite(install_path_psx, 1, sizeof(install_path_psx), f);
        fclose(f);
    }
}
