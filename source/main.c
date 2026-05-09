#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <switch.h>
#include <errno.h>

#include "common.h"
#include "converter.h"
#include "net.h"

#define PAGE_SIZE 37
#define MAX_DIRS 256

typedef enum {
    STATE_LOADING,
    STATE_MENU,
    STATE_LIST,
    STATE_SELECT_SPEED,
    STATE_DOWNLOADING,
    STATE_CONVERTING,
    STATE_SETTINGS,
    STATE_ABOUT,
    STATE_BROWSE_DIR
} AppState;

typedef enum {
    FILTER_SEARCH,
    FILTER_ALL,
    FILTER_PSP,
    FILTER_PSX,
    FILTER_NUM,
    FILTER_JP,
    FILTER_LETTER
} FilterMode;

GameEntry *filtered_games[MAX_GAMES];
int filtered_count = 0;
char current_search[256] = "";
char current_letter = 'A';
FilterMode current_filter = FILTER_ALL;

char dir_entries[MAX_DIRS][256];
int dir_count = 0;
char current_browse_path[512] = "/";
int dir_idx = 0;
int dir_top = 0;
int setting_path_mode = 1; 

void apply_filter() {
    filtered_count = 0;
    for(int i = 0; i < total_games; i++) {
        int match = 0;
        switch(current_filter) {
            case FILTER_ALL: match = 1; break;
            case FILTER_PSP: if(stristr(all_games[i].platform, "PSP")) match = 1; break;
            case FILTER_PSX: if(stristr(all_games[i].platform, "PSX")) match = 1; break;
            case FILTER_SEARCH: if(stristr(all_games[i].name, current_search)) match = 1; break;
            case FILTER_JP: if(stristr(all_games[i].region, "JP")) match = 1; break;
            case FILTER_NUM: if(isdigit((unsigned char)all_games[i].name[0])) match = 1; break;
            case FILTER_LETTER: if(toupper((unsigned char)all_games[i].name[0]) == current_letter) match = 1; break;
        }
        if(match) filtered_games[filtered_count++] = &all_games[i];
    }
}

void mkdir_p(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if(tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for(p = tmp + 1; *p; p++)
        if(*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    mkdir(tmp, 0777);
}

void load_dir_list(const char* path) {
    dir_count = 0;
    DIR *d = opendir(path);
    if (!d) return;

    if (strcmp(path, "/") != 0) {
        strcpy(dir_entries[dir_count++], "..");
    }

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL && dir_count < MAX_DIRS) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;

        char full_path[1024];
        if (strcmp(path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", dir->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", path, dir->d_name);
        }

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(dir_entries[dir_count], 256, "%s", dir->d_name);
            dir_count++;
        }
    }
    closedir(d);

    int start_idx = (strcmp(path, "/") != 0) ? 1 : 0;
    for (int i = start_idx; i < dir_count - 1; i++) {
        for (int j = i + 1; j < dir_count; j++) {
            if (strcasecmp(dir_entries[i], dir_entries[j]) > 0) {
                char temp[256];
                strcpy(temp, dir_entries[i]);
                strcpy(dir_entries[i], dir_entries[j]);
                strcpy(dir_entries[j], temp);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    socketInitializeDefault();
    nxlinkStdio();
    consoleInit(NULL);
    
    mkdir_p(APP_DIR);
    load_config(); 

    db_buffer = (char*)malloc(DB_BUFFER_SIZE);
    if (!db_buffer) { printf("Memory Error.\n"); return -1; }

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    AppState state = STATE_LOADING;
    int menu_idx = 0;
    int list_idx = 0;
    int list_top = 0;
    int settings_idx = 0;
    int speed_idx = 1;
    
    char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char *main_menu[] = {"SEARCH", "ALL GAMES", "PSP ONLY", "PSX ONLY", "# (NUMBERS)", "JAPAN (JP)", "SETTINGS", "ABOUT"};
    int menu_size = 8 + 26; 

    const char *settings_menu[] = {"Set PSP Install Path", "Set PSX Install Path", "Download Cheats", "Back to Menu"};
    const char *speed_options[] = {"Slow/Stable", "Good/Recom.", "Fast/Unstable"};

    int v_timer = 0;
    int h_timer = 0;

    while(appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);

        int move_up = 0, move_down = 0, move_left = 0, move_right = 0;

        if (kDown & HidNpadButton_Down) { move_down = 1; v_timer = 0; }
        else if (kDown & HidNpadButton_Up) { move_up = 1; v_timer = 0; }
        else if (kHeld & HidNpadButton_Down) {
            v_timer++;
            if (v_timer > 15 && (v_timer % 4 == 0)) move_down = 1;
        }
        else if (kHeld & HidNpadButton_Up) {
            v_timer++;
            if (v_timer > 15 && (v_timer % 4 == 0)) move_up = 1;
        }
        else { v_timer = 0; }

        if (kDown & HidNpadButton_Right) { move_right = 1; h_timer = 0; }
        else if (kDown & HidNpadButton_Left) { move_left = 1; h_timer = 0; }
        else if (kHeld & HidNpadButton_Right) {
            h_timer++;
            if (h_timer > 15 && (h_timer % 4 == 0)) move_right = 1;
        }
        else if (kHeld & HidNpadButton_Left) {
            h_timer++;
            if (h_timer > 15 && (h_timer % 4 == 0)) move_left = 1;
        }
        else { h_timer = 0; }

        if (state == STATE_LOADING) {
            consoleClear();
            char head[128];
            snprintf(head, sizeof(head), "%s v%s - LOADING...", APP_NAME, APP_VERSION);
            ui_draw_header(head);
            printf("\n  Downloading Databases...\n");
            ui_draw_footer("Please wait...");
            consoleUpdate(NULL);
            
            total_games = 0;
            struct MemoryStruct chunk_psp = {malloc(1), 0};
            struct MemoryStruct chunk_psx = {malloc(1), 0};
            
            CURL *curl = curl_easy_init();
            if(curl) {
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                
                curl_easy_setopt(curl, CURLOPT_URL, URL_NPS_PSP);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk_psp);
                curl_easy_perform(curl);
                
                curl_easy_setopt(curl, CURLOPT_URL, URL_NPS_PSX);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk_psx);
                curl_easy_perform(curl);
                
                curl_easy_cleanup(curl);
            }
            
            size_t total_size = chunk_psp.size + chunk_psx.size;
            if(total_size > 0 && total_size < DB_BUFFER_SIZE - 2) {
                size_t offset = 0;
                
                if (chunk_psp.size > 0) {
                    memcpy(db_buffer, chunk_psp.memory, chunk_psp.size);
                    db_buffer[chunk_psp.size] = '\0';
                    parse_db(db_buffer, "PSP");
                    offset += chunk_psp.size + 1;
                }
                
                if (chunk_psx.size > 0) {
                    memcpy(db_buffer + offset, chunk_psx.memory, chunk_psx.size);
                    db_buffer[offset + chunk_psx.size] = '\0';
                    parse_db(db_buffer + offset, "PSX");
                }
                
                state = STATE_MENU;
            } else {
                printf("\n  DB Error. Check internet connection.\n");
                while(1) { padUpdate(&pad); consoleUpdate(NULL); }
            }
            free(chunk_psp.memory);
            free(chunk_psx.memory);
        }
        else if (state == STATE_MENU) {
            consoleClear();
            char head[128];
            snprintf(head, sizeof(head), "%s v%s", APP_NAME, APP_VERSION);
            ui_draw_header(head);
            
            printf("\n");
            for(int i = 0; i < menu_size; i++) {
                if(i == menu_idx) printf("\x1b[47;30m");
                if(i < 8) printf(" %s \n", main_menu[i]);
                else {
                    printf(" %c \n", letters[i-8]);
                }
                if(i == menu_idx) printf("\x1b[0m");
            }
            
            ui_draw_footer("[A] Select  [Arrows] Navigate");

            if(move_down) {
                if (menu_idx < menu_size - 1) menu_idx++;
            }
            if(move_up) {
                if (menu_idx > 0) menu_idx--;
            }
            if(move_right) {
                if(menu_idx + 10 < menu_size) menu_idx += 10;
                else menu_idx = menu_size - 1;
            }
            if(move_left) {
                if(menu_idx - 10 >= 0) menu_idx -= 10;
                else menu_idx = 0;
            }
            
            if(kDown & HidNpadButton_A) {
                if(menu_idx == 0) {
                    SwkbdConfig kbd;
                    if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
                        swkbdConfigMakePresetDefault(&kbd);
                        if (R_SUCCEEDED(swkbdShow(&kbd, current_search, sizeof(current_search)))) {
                            current_filter = FILTER_SEARCH;
                            apply_filter();
                            state = STATE_LIST;
                            list_idx = 0; list_top = 0;
                        }
                        swkbdClose(&kbd);
                    }
                } else if (menu_idx == 6) {
                    state = STATE_SETTINGS;
                    settings_idx = 0;
                } else if (menu_idx == 7) {
                    state = STATE_ABOUT;
                } else {
                    if(menu_idx == 1) current_filter = FILTER_ALL;
                    else if(menu_idx == 2) current_filter = FILTER_PSP;
                    else if(menu_idx == 3) current_filter = FILTER_PSX;
                    else if(menu_idx == 4) current_filter = FILTER_NUM;
                    else if(menu_idx == 5) current_filter = FILTER_JP;
                    else {
                        current_letter = letters[menu_idx - 8];
                        current_filter = FILTER_LETTER;
                    }
                    apply_filter();
                    state = STATE_LIST;
                    list_idx = 0; list_top = 0;
                }
            }
        }
        else if (state == STATE_ABOUT) {
            consoleClear();
            char head[128];
            snprintf(head, sizeof(head), "%s v%s - ABOUT", APP_NAME, APP_VERSION);
            ui_draw_header(head);
            
            printf("\n");
            printf("  Repo: https://github.com/joaqmiu/NexPS\n\n");
            printf("  Special thanks:\n");
            printf("  - Mestre Sion/Jann\n");
            printf("  - NewsInside.org\n");
            printf("  - switchnamao\n\n");
            printf("  Press B to return...\n");

            ui_draw_footer("[B] Back");

            if (kDown & (HidNpadButton_B | HidNpadButton_A)) {
                state = STATE_MENU;
            }
        }
        else if (state == STATE_SETTINGS) {
            consoleClear();
            char head[128];
            snprintf(head, sizeof(head), "%s v%s - SETTINGS", APP_NAME, APP_VERSION);
            ui_draw_header(head);
            
            printf("\n");
            for(int i = 0; i < 4; i++) {
                if(i == settings_idx) printf("\x1b[47;30m");
                
                if(i == 0) printf(" %s: %s \n", settings_menu[i], install_path_psp);
                else if(i == 1) printf(" %s: %s \n", settings_menu[i], install_path_psx);
                else printf(" %s \n", settings_menu[i]);
                
                if(i == settings_idx) printf("\x1b[0m");
            }

            ui_draw_footer("[A] Select  [B] Back");

            if(move_down) {
                if(settings_idx < 3) settings_idx++;
            }
            if(move_up) {
                if(settings_idx > 0) settings_idx--;
            }
            
            if(kDown & HidNpadButton_A) {
                if(settings_idx == 0 || settings_idx == 1) {
                    setting_path_mode = (settings_idx == 0) ? 1 : 2;
                    strcpy(current_browse_path, "/");
                    load_dir_list(current_browse_path);
                    dir_idx = 0;
                    dir_top = 0;
                    state = STATE_BROWSE_DIR;
                }
                else if(settings_idx == 2) {
                    mkdir_p("/switch/ppsspp/config/ppsspp/PSP/Cheats");
                    int res = download_file(URL_CHEATS, "/switch/ppsspp/config/ppsspp/PSP/Cheats/cheat.db", &pad, "DOWNLOADING CHEATS...", 1);
                    consoleClear();
                    ui_draw_header(APP_NAME " - Cheats");
                    if(res == 1) printf("\n\n  \x1b[1;32mDONE!\x1b[0m\n");
                    else printf("\n\n  \x1b[1;31mDOWNLOAD ERROR.\x1b[0m\n");
                    printf("  Press A to return...");
                    ui_draw_footer("[A] Back");
                    consoleUpdate(NULL);
                    while(1) {
                        padUpdate(&pad);
                        if(padGetButtonsDown(&pad) & HidNpadButton_A) break;
                    }
                }
                else if(settings_idx == 3) {
                    state = STATE_MENU;
                }
            }
            if(kDown & HidNpadButton_B) state = STATE_MENU;
        }
        else if (state == STATE_BROWSE_DIR) {
            consoleClear();
            char header_buf[600];
            snprintf(header_buf, sizeof(header_buf), "%s > BROWSE: %s", APP_NAME, current_browse_path);
            ui_draw_header(header_buf);

            if (dir_count == 0) {
                printf("\n  No directories found here.\n");
            } else {
                for(int i = 0; i < PAGE_SIZE; i++) {
                    int actual_idx = dir_top + i;
                    if(actual_idx >= dir_count) break;
                    if(actual_idx == dir_idx) printf("\x1b[47;30m");
                    
                    printf(" [%s] \n", dir_entries[actual_idx]);
                    
                    if(actual_idx == dir_idx) printf("\x1b[0m");
                }
            }

            ui_draw_footer("[A] Open  [B] Cancel  [X] Select Folder  [Arrows] Nav");

            if(move_down) {
                if(dir_idx < dir_count - 1) {
                    dir_idx++;
                    if(dir_idx >= dir_top + PAGE_SIZE) dir_top++;
                }
            }
            if(move_up) {
                if(dir_idx > 0) {
                    dir_idx--;
                    if(dir_idx < dir_top) dir_top--;
                }
            }
            if(move_right) {
                 if(dir_idx + PAGE_SIZE < dir_count) {
                     dir_idx += PAGE_SIZE; dir_top += PAGE_SIZE;
                 } else {
                     dir_idx = dir_count - 1;
                     if(dir_top < dir_count - PAGE_SIZE) dir_top = dir_count - PAGE_SIZE;
                 }
            }
            if(move_left) {
                if(dir_idx - PAGE_SIZE >= 0) {
                    dir_idx -= PAGE_SIZE; if(dir_idx < dir_top) dir_top -= PAGE_SIZE;
                } else { dir_idx = 0; dir_top = 0; }
            }
            if(dir_top < 0) dir_top = 0;

            if (kDown & HidNpadButton_B) {
                state = STATE_SETTINGS;
            }
            
            if (kDown & HidNpadButton_X) {
                if (setting_path_mode == 1) {
                    strncpy(install_path_psp, current_browse_path, sizeof(install_path_psp) - 1);
                    install_path_psp[sizeof(install_path_psp) - 1] = '\0';
                } else {
                    strncpy(install_path_psx, current_browse_path, sizeof(install_path_psx) - 1);
                    install_path_psx[sizeof(install_path_psx) - 1] = '\0';
                }
                save_config();
                state = STATE_SETTINGS;
            }

            if (kDown & HidNpadButton_A && dir_count > 0) {
                if (strcmp(dir_entries[dir_idx], "..") == 0) {
                    char *last_slash = strrchr(current_browse_path, '/');
                    if (last_slash && last_slash != current_browse_path) {
                        *last_slash = '\0';
                    } else {
                        strcpy(current_browse_path, "/");
                    }
                } else {
                    if (strcmp(current_browse_path, "/") != 0) {
                        strcat(current_browse_path, "/");
                    }
                    strcat(current_browse_path, dir_entries[dir_idx]);
                }
                load_dir_list(current_browse_path);
                dir_idx = 0;
                dir_top = 0;
            }
        }
        else if (state == STATE_LIST) {
            consoleClear();
            char header_buf[64];
            snprintf(header_buf, 64, "%s > RESULTS (%d)", APP_NAME, filtered_count);
            ui_draw_header(header_buf);

            if(filtered_count == 0) printf("\n  No games found.\n");
            else {
                for(int i = 0; i < PAGE_SIZE; i++) {
                    int actual_idx = list_top + i;
                    if(actual_idx >= filtered_count) break;
                    if(actual_idx == list_idx) printf("\x1b[47;30m");
                    
                    GameEntry *g = filtered_games[actual_idx];
                    char dname[54];
                    strncpy(dname, g->name, 53); dname[53] = '\0';
                    printf(" [%s][%s] %-50s \n", g->platform, g->region, dname);
                    
                    if(actual_idx == list_idx) printf("\x1b[0m");
                }
            }
            
            ui_draw_footer("[A] Download  [B] Back  [Arrows] Navigate");

            if(move_down) {
                if(list_idx < filtered_count - 1) {
                    list_idx++;
                    if(list_idx >= list_top + PAGE_SIZE) list_top++;
                }
            }
            if(move_up) {
                if(list_idx > 0) {
                    list_idx--;
                    if(list_idx < list_top) list_top--;
                }
            }
            if(move_right) {
                 if(list_idx + PAGE_SIZE < filtered_count) {
                     list_idx += PAGE_SIZE; list_top += PAGE_SIZE;
                 } else {
                     list_idx = filtered_count - 1;
                     if(list_top < filtered_count - PAGE_SIZE) list_top = filtered_count - PAGE_SIZE;
                 }
            }
            if(move_left) {
                if(list_idx - PAGE_SIZE >= 0) {
                    list_idx -= PAGE_SIZE; if(list_idx < list_top) list_top -= PAGE_SIZE;
                } else { list_idx = 0; list_top = 0; }
            }
            if(list_top < 0) list_top = 0;

            if(kDown & HidNpadButton_B) state = STATE_MENU;
            if(kDown & HidNpadButton_A && filtered_count > 0) {
                state = STATE_SELECT_SPEED;
                speed_idx = 1; 
            }
        }
        else if (state == STATE_SELECT_SPEED) {
            consoleClear();
            ui_draw_header("Select Download Speed");
            printf("\n");
            
            for(int i = 0; i < 3; i++) {
                if(i == speed_idx) printf("\x1b[47;30m");
                printf(" %s \n", speed_options[i]);
                if(i == speed_idx) printf("\x1b[0m");
            }
            
            ui_draw_footer("[A] Select  [B] Back");

            if(move_down) {
                if(speed_idx < 2) speed_idx++;
            }
            if(move_up) {
                if(speed_idx > 0) speed_idx--;
            }
            
            if(kDown & HidNpadButton_A) {
                if(speed_idx == 0) selected_threads = 1;
                else if(speed_idx == 1) selected_threads = 4;
                else selected_threads = 8;
                save_config();
                state = STATE_DOWNLOADING;
            }
            if(kDown & HidNpadButton_B) state = STATE_LIST;
        }
        else if (state == STATE_DOWNLOADING) {
            GameEntry *g = filtered_games[list_idx];
            char safe_name[256];
            strncpy(safe_name, g->name, 255); safe_name[255] = '\0';
            sanitize_filename(safe_name);
            
            int is_psx = (stristr(g->platform, "PSX") != NULL);
            const char *target_dir = is_psx ? install_path_psx : install_path_psp;
            mkdir_p(target_dir);
            
            char pkg_path[1024];
            snprintf(pkg_path, sizeof(pkg_path), "%s/%s_temp.pkg", target_dir, safe_name);
            
            char header_buf[128];
            snprintf(header_buf, 128, "%s > DOWNLOADING PKG...", APP_NAME);

            int result = download_file(g->url, pkg_path, &pad, header_buf, selected_threads);
            
            consoleClear();
            ui_draw_header(header_buf);

            if(result == 1) {
                state = STATE_CONVERTING;
                
                printf("\n\n  PROCESSING FILES...\n");
                ui_draw_footer("Please wait...");
                consoleUpdate(NULL); 

                if (extract_game_from_pkg(pkg_path, target_dir, safe_name, is_psx)) {
                    remove(pkg_path); 
                    printf("  Saved to directory: %s\n", target_dir);
                } else {
                    printf("\n\n  \x1b[1;31mPROCESSING ERROR.\x1b[0m\n");
                    remove(pkg_path); 
                }
                
                printf("\n\n  Press A to return...");
                ui_draw_footer("[A] Continue");
            } else if (result == -1) {
                printf("\n\n  \x1b[1;33mCANCELED.\x1b[0m\n");
                printf("  Press A to return...");
                ui_draw_footer("[A] Continue");
            } else {
                printf("\n\n  \x1b[1;31mDOWNLOAD ERROR.\x1b[0m\n");
                printf("  Press A to return...");
                ui_draw_footer("[A] Continue");
            }
            
            consoleUpdate(NULL); 
            
            while(1) {
                padUpdate(&pad);
                if(padGetButtonsDown(&pad) & HidNpadButton_A) break;
                consoleUpdate(NULL); 
            }
            state = STATE_LIST;
        }
        consoleUpdate(NULL);
    }
    
    free(db_buffer);
    consoleExit(NULL);
    socketExit();
    return 0;
}
