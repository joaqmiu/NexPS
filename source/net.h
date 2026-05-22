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

// Callbacks for download_file. The progress callback is invoked at most twice
// per second with the cumulative byte count across all threads. The cancel
// callback is polled in the same loop; a non-zero return aborts the download
// (the partial file is removed and download_file returns -1).
typedef void (*download_progress_cb)(long long downloaded, long long total, void *user);
typedef int  (*download_cancel_cb)(void *user);

// Returns 1 on success, 0 on error, -1 on user cancel.
int download_file(const char *url, const char *path, int num_threads,
                  download_progress_cb on_progress,
                  download_cancel_cb   should_cancel,
                  void *user);

int fetch_to_memory(const char *url, struct MemoryStruct *chunk);
void parse_db(char *data, const char *platform);

#endif
