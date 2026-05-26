#ifndef CONVERTER_H
#define CONVERTER_H

// Phases reported by the converter progress callback.
#define CONV_PHASE_DECRYPT 0   // PKG -> PBP decryption
#define CONV_PHASE_PSAR    1   // PSAR -> BIN extraction
#define CONV_PHASE_ARCHIVE 2   // generic archive extraction (zip/7z/rar)

typedef void (*conv_progress_cb)(int phase, long long current, long long total, void *user);

int extract_game_from_pkg(const char *input_pkg, const char *output_dir,
                          const char *safe_name, int is_psx,
                          conv_progress_cb on_progress, void *user);

int extract_archive(const char *filename, const char *dest_dir,
                    conv_progress_cb on_progress, void *user);

#endif
