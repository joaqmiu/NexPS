#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <curl/curl.h>
#include <switch.h>
#include "net.h"

#define MAX_PARTS 8
#define MAX_RETRIES 4
#define WRITE_BUFFER_SIZE (256 * 1024)

GameEntry all_games[MAX_GAMES];
int total_games = 0;
char *db_buffer = NULL;

typedef struct {
    int fd;
    long start_offset;
    long current_offset;
    curl_off_t total_written;
    long end_offset;
    int retry_count;
    int part_index;
    int failed;
} PartContext;

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

static size_t WritePartCallback(void *ptr, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    PartContext *ctx = (PartContext *)userp;
    
    lseek(ctx->fd, ctx->current_offset, SEEK_SET);
    ssize_t written = write(ctx->fd, ptr, realsize);
    
    if (written <= 0) return 0;
    ctx->current_offset += written;
    ctx->total_written += written;
    return (size_t)written;
}

static curl_off_t get_file_size(const char *url) {
    curl_off_t filesize = -1;
    for (int attempt = 0; attempt < 3 && filesize < 0; attempt++) {
        CURL *curl = curl_easy_init();
        if (!curl) break;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        if (curl_easy_perform(curl) == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &filesize);
        }
        curl_easy_cleanup(curl);
    }
    return filesize;
}

static CURL *make_part_handle(const char *url, PartContext *ctx) {
    CURL *h = curl_easy_init();
    if (!h) return NULL;

    char range_buf[64];
    snprintf(range_buf, sizeof(range_buf), "%lld-%lld",
             (long long)ctx->current_offset, (long long)ctx->end_offset);

    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WritePartCallback);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(h, CURLOPT_RANGE, range_buf);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "NPS-Switch/4.0");
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(h, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(h, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_BUFFERSIZE, (long)WRITE_BUFFER_SIZE);
    curl_easy_setopt(h, CURLOPT_PRIVATE, ctx);

    return h;
}

int download_file(const char *url, const char *path, PadState *pad, const char *header_title, int num_threads) {
    CURLM *multi_handle;
    CURL *easy_handles[MAX_PARTS];
    PartContext part_ctx[MAX_PARTS];
    int still_running = 0;
    int i;
    int cancel_requested = 0;

    if (num_threads < 1) num_threads = 1;
    if (num_threads > MAX_PARTS) num_threads = MAX_PARTS;

    curl_off_t file_size = get_file_size(url);
    if (file_size <= 0) return 0;

    curl_off_t part_size = file_size / num_threads;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;

    if (ftruncate(fd, (off_t)file_size) != 0) {
        close(fd);
        remove(path);
        return 0;
    }

    multi_handle = curl_multi_init();
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long)num_threads);
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, (long)num_threads);

    for (i = 0; i < num_threads; i++) {
        part_ctx[i].fd = fd;
        part_ctx[i].start_offset = (long)(i * part_size);
        part_ctx[i].current_offset = part_ctx[i].start_offset;
        part_ctx[i].total_written = 0;
        part_ctx[i].end_offset = (long)((i == num_threads - 1) ? file_size - 1 : (i * part_size + part_size - 1));
        part_ctx[i].retry_count = 0;
        part_ctx[i].part_index = i;
        part_ctx[i].failed = 0;

        easy_handles[i] = make_part_handle(url, &part_ctx[i]);
        if (easy_handles[i]) {
            curl_multi_add_handle(multi_handle, easy_handles[i]);
        }
    }

    consoleClear();
    ui_draw_header(header_title);
    printf("\n\n  Initializing...\n");
    consoleUpdate(NULL);

    u64 last_time = armGetSystemTick();
    curl_off_t last_downloaded = 0;

    do {
        int numfds;
        CURLMcode mc = curl_multi_perform(multi_handle, &still_running);

        if (mc == CURLM_OK) {
            curl_multi_wait(multi_handle, NULL, 0, 100, &numfds);
        }

        CURLMsg *msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                PartContext *ctx = NULL;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char **)&ctx);

                CURL *done_handle = msg->easy_handle;
                curl_multi_remove_handle(multi_handle, done_handle);
                curl_easy_cleanup(done_handle);

                if (ctx) {
                    int idx = ctx->part_index;
                    easy_handles[idx] = NULL;

                    if (msg->data.result != CURLE_OK) {
                        if (ctx->retry_count < MAX_RETRIES) {
                            ctx->retry_count++;
                            CURL *retry = make_part_handle(url, ctx);
                            if (retry) {
                                easy_handles[idx] = retry;
                                curl_multi_add_handle(multi_handle, retry);
                            } else {
                                ctx->failed = 1;
                            }
                        } else {
                            ctx->failed = 1;
                        }
                    }
                }
            }
        }

        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_B) {
            cancel_requested = 1;
            break;
        }

        u64 current_time = armGetSystemTick();
        if (current_time - last_time >= armGetSystemTickFreq() / 2) {
            curl_off_t total_downloaded = 0;
            for (int k = 0; k < num_threads; k++) {
                total_downloaded += part_ctx[k].total_written;
            }

            double speed = (double)(total_downloaded - last_downloaded) * 2.0;
            last_downloaded = total_downloaded;
            last_time = current_time;

            double fraction = (double)total_downloaded / (double)file_size;
            if (fraction > 1.0) fraction = 1.0;
            int percentage = (int)(fraction * 100);

            consoleClear();
            ui_draw_header(header_title);

            printf("\n\n");
            printf("  Progress: %3d%%\n", percentage);

            printf("  Downloaded: %.2f / %.2f MB (%.2f MB/s)\n",
                   (double)total_downloaded / (1024 * 1024),
                   (double)file_size / (1024 * 1024),
                   speed / (1024 * 1024));

            printf("\n  [");
            int bar_width = 50;
            int pos = (int)(bar_width * fraction);
            for (int k = 0; k < bar_width; ++k) {
                if (k < pos) printf("\x1b[32m#\x1b[0m");
                else if (k == pos) printf("\x1b[32m>\x1b[0m");
                else printf("\x1b[30m-\x1b[0m");
            }
            printf("]\n");

            ui_draw_footer("[B] Cancel");
            consoleUpdate(NULL);
        }

    } while (still_running);

    int error_detected = 0;
    for (i = 0; i < num_threads; i++) {
        if (part_ctx[i].failed) error_detected = 1;
        if (easy_handles[i]) {
            curl_multi_remove_handle(multi_handle, easy_handles[i]);
            curl_easy_cleanup(easy_handles[i]);
            easy_handles[i] = NULL;
        }
    }

    curl_multi_cleanup(multi_handle);
    close(fd);

    if (cancel_requested || error_detected) {
        remove(path);
        return -1;
    }

    curl_off_t final_size = 0;
    for (i = 0; i < num_threads; i++) {
        final_size += part_ctx[i].total_written;
    }

    return (final_size >= file_size);
}

void parse_db(char *data) {
    char *line = data;
    total_games = 0;
    while (line && total_games < MAX_GAMES) {
        char *next_line = strchr(line, '\n');
        if (next_line) *next_line = '\0';
        if (strlen(line) > 10) {
            char *cols[10];
            int col_count = 0;
            char *ptr = line;
            cols[col_count++] = ptr;
            while (*ptr && col_count < 6) {
                if (*ptr == '\t') { *ptr = '\0'; cols[col_count++] = ptr + 1; }
                ptr++;
            }
            if (col_count >= 5) {
                if (stristr(cols[2], "NEOGEO") == NULL &&
                    stristr(cols[2], "PC Engine") == NULL &&
                    stristr(cols[4], "http")) {
                    all_games[total_games].id = cols[0];
                    all_games[total_games].region = cols[1];
                    all_games[total_games].platform = cols[2];
                    all_games[total_games].name = cols[3];
                    all_games[total_games].url = cols[4];
                    total_games++;
                }
            }
        }
        if (!next_line) break;
        line = next_line + 1;
    }
}
