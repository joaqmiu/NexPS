#ifndef NET_H
#define NET_H

#include <switch.h>
#include <curl/curl.h>
#include "common.h"

#define URL_NPS_PSP "https://nopaystation.com/tsv/PSP_GAMES.tsv"
#define URL_NPS_PSX "https://nopaystation.com/tsv/PSX_GAMES.tsv"
#define URL_CHEATS "https://raw.githubusercontent.com/Saramagrean/CWCheat-Database-Plus-/refs/heads/master/cheat.db"

extern GameEntry all_games[MAX_GAMES];
extern int total_games;
extern char *db_buffer;

struct MemoryStruct {
    char *memory;
    size_t size;
};

int download_file(const char *url, const char *path, PadState *pad, const char *header_title, int num_threads);
int fetch_to_memory(const char *url, struct MemoryStruct *chunk);
void parse_db(char *data, const char *platform);

#endif
