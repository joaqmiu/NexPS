#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <switch.h>
#include <mbedtls/aes.h>
#include <zlib.h>
#include "converter.h"
#include "common.h"

#define CHUNK_SIZE (64 * 1024)

const unsigned char PKG_KEY[] = {
    0x07, 0xf2, 0xc6, 0x82, 0x90, 0xb5, 0x0d, 0x2c, 
    0x33, 0x81, 0x8d, 0x70, 0x9b, 0x60, 0xe6, 0x2b
};

static uint32_t swap32(uint32_t val) {
    return __builtin_bswap32(val);
}

static uint64_t swap64(uint64_t val) {
    return __builtin_bswap64(val);
}

static void calculate_iv(unsigned char *out, const unsigned char *base_iv, uint64_t offset_factor) {
    memcpy(out, base_iv, 16);
    for (int i = 15; i >= 0; i--) {
        uint64_t sum = out[i] + (offset_factor & 0xFF);
        out[i] = (unsigned char)(sum & 0xFF);
        offset_factor = (offset_factor >> 8) + (sum >> 8);
    }
}

static void draw_conversion_progress(size_t current, size_t total, const char* label) {
    static int last_percent_conv = -1;
    double fraction = (double)current / (double)total;
    int percent = (int)(fraction * 100);

    if (percent != last_percent_conv) {
        last_percent_conv = percent;
        int bar_width = 30;
        int pos = bar_width * fraction;

        printf("\r\x1b[K  %s [", label);
        for (int i = 0; i < bar_width; ++i) {
            if (i < pos) printf("=");
            else if (i == pos) printf(">");
            else printf(" ");
        }
        printf("] %3d%%", percent);
        consoleUpdate(NULL);
    }
}

static void generate_cue_file(const char *cue_path, const char *bin_filename) {
    FILE *f = fopen(cue_path, "w");
    if (f) {
        fprintf(f, "FILE \"%s\" BINARY\n", bin_filename);
        fprintf(f, "  TRACK 01 MODE2/2352\n");
        fprintf(f, "    INDEX 01 00:00:00\n");
        fclose(f);
    }
}

static int extract_psar_to_bin(const char *pbp_path, const char *bin_path) {
    FILE *fpbp = fopen(pbp_path, "rb");
    if (!fpbp) return 0;

    uint32_t header[10];
    if (fread(header, sizeof(uint32_t), 10, fpbp) != 10) {
        fclose(fpbp);
        return 0;
    }

    if (header[0] != 0x50425000) { 
        fclose(fpbp);
        return 0;
    }

    uint32_t psar_offset = header[9]; 
    
    fseek(fpbp, 0, SEEK_END);
    uint32_t file_size = ftell(fpbp);
    if (psar_offset == 0 || psar_offset >= file_size) {
        fclose(fpbp);
        return 0;
    }

    uint32_t psar_size = file_size - psar_offset;
    
    FILE *fbin = fopen(bin_path, "wb");
    if (!fbin) {
        fclose(fpbp);
        return 0;
    }

    fseek(fpbp, psar_offset, SEEK_SET);
    unsigned char *buffer = malloc(CHUNK_SIZE);
    uint32_t remaining = psar_size;

    printf("\n");
    while (remaining > 0) {
        size_t to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        size_t r = fread(buffer, 1, to_read, fpbp);
        if (r == 0) break;
        fwrite(buffer, 1, r, fbin);
        remaining -= r;
        draw_conversion_progress(psar_size - remaining, psar_size, "BIN Extraction");
    }

    free(buffer);
    fclose(fbin);
    fclose(fpbp);
    return 1;
}

int extract_game_from_pkg(const char *input_pkg, const char *output_dir, const char *safe_name, int is_psx) {
    FILE *fd = fopen(input_pkg, "rb");
    if (!fd) return 0;

    unsigned char header[192];
    if (fread(header, 1, 192, fd) != 192) { fclose(fd); return 0; }

    uint32_t magic = swap32(*(uint32_t*)(header + 0));
    if (magic != 0x7F504B47) { 
        printf("\nError: Invalid Magic number.\n");
        fclose(fd); 
        return 0; 
    }

    uint32_t meta_offset = swap32(*(uint32_t*)(header + 8));
    uint32_t meta_count = swap32(*(uint32_t*)(header + 12));
    uint32_t item_count = swap32(*(uint32_t*)(header + 20));
    uint64_t enc_offset = swap64(*(uint64_t*)(header + 32));
    unsigned char iv[16];
    memcpy(iv, header + 112, 16);

    uint32_t items_offset = 0;
    uint32_t current_meta = meta_offset;
    
    for (uint32_t i = 0; i < meta_count; i++) {
        unsigned char block[16];
        fseek(fd, current_meta, SEEK_SET);
        if (fread(block, 1, 16, fd) != 16) break;

        uint32_t type = swap32(*(uint32_t*)(block + 0));
        uint32_t size = swap32(*(uint32_t*)(block + 4));

        if (type == 13) items_offset = swap32(*(uint32_t*)(block + 8));
        current_meta += 8 + size;
    }

    mbedtls_aes_context aes;
    unsigned char current_iv[16];
    unsigned char stream_block[16];
    size_t nc_off = 0;
    
    char pbp_temp_path[1024];
    snprintf(pbp_temp_path, sizeof(pbp_temp_path), "%s/EBOOT.PBP", output_dir);
    int pbp_extracted = 0;

    for (uint32_t i = 0; i < item_count; i++) {
        uint32_t entry_offset = items_offset + (i * 32);
        
        unsigned char entry_enc[32];
        fseek(fd, enc_offset + entry_offset, SEEK_SET);
        fread(entry_enc, 1, 32, fd);

        unsigned char entry_dec[32];
        calculate_iv(current_iv, iv, entry_offset / 16);
        
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, PKG_KEY, 128);
        nc_off = 0;
        memset(stream_block, 0, 16);
        mbedtls_aes_crypt_ctr(&aes, 32, &nc_off, current_iv, stream_block, entry_enc, entry_dec);

        uint32_t name_offset = swap32(*(uint32_t*)(entry_dec + 0));
        uint32_t name_size = swap32(*(uint32_t*)(entry_dec + 4));
        uint64_t data_offset = swap64(*(uint64_t*)(entry_dec + 8));
        uint64_t data_size = swap64(*(uint64_t*)(entry_dec + 16));

        unsigned char *name_enc = malloc(name_size);
        unsigned char *name_dec = malloc(name_size + 1);
        fseek(fd, enc_offset + name_offset, SEEK_SET);
        fread(name_enc, 1, name_size, fd);

        calculate_iv(current_iv, iv, name_offset / 16);
        mbedtls_aes_setkey_enc(&aes, PKG_KEY, 128);
        nc_off = 0;
        memset(stream_block, 0, 16);
        mbedtls_aes_crypt_ctr(&aes, name_size, &nc_off, current_iv, stream_block, name_enc, name_dec);
        name_dec[name_size] = '\0';

        if (stristr((char*)name_dec, "EBOOT.PBP")) {
            printf("\n  Decrypting: %s\n", is_psx ? "PSX Base (PBP)" : "PSP Game (PBP)");
            
            FILE *fout = fopen(pbp_temp_path, "wb");
            if (fout) {
                unsigned char *chunk_buf = malloc(CHUNK_SIZE);
                unsigned char *chunk_dec = malloc(CHUNK_SIZE);
                uint64_t remaining = data_size;
                uint64_t current_file_pos = data_offset;

                while (remaining > 0) {
                    size_t len = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
                    fseek(fd, enc_offset + current_file_pos, SEEK_SET);
                    fread(chunk_buf, 1, len, fd);

                    calculate_iv(current_iv, iv, current_file_pos / 16);
                    mbedtls_aes_setkey_enc(&aes, PKG_KEY, 128);
                    nc_off = 0;
                    memset(stream_block, 0, 16);
                    mbedtls_aes_crypt_ctr(&aes, len, &nc_off, current_iv, stream_block, chunk_buf, chunk_dec);

                    fwrite(chunk_dec, 1, len, fout);
                    remaining -= len;
                    current_file_pos += len;
                    draw_conversion_progress(data_size - remaining, data_size, "Decryption");
                }
                free(chunk_buf);
                free(chunk_dec);
                fclose(fout);
                pbp_extracted = 1;
            }
        } 
        else if (is_psx && stristr((char*)name_dec, "KEYS.BIN")) {
            char keys_path[1024];
            snprintf(keys_path, sizeof(keys_path), "%s/%s.KEYS.BIN", output_dir, safe_name);
            FILE *fout = fopen(keys_path, "wb");
            if (fout) {
                unsigned char *chunk_buf = malloc(data_size);
                unsigned char *chunk_dec = malloc(data_size);
                fseek(fd, enc_offset + data_offset, SEEK_SET);
                fread(chunk_buf, 1, data_size, fd);

                calculate_iv(current_iv, iv, data_offset / 16);
                mbedtls_aes_setkey_enc(&aes, PKG_KEY, 128);
                nc_off = 0;
                memset(stream_block, 0, 16);
                mbedtls_aes_crypt_ctr(&aes, data_size, &nc_off, current_iv, stream_block, chunk_buf, chunk_dec);

                fwrite(chunk_dec, 1, data_size, fout);
                free(chunk_buf);
                free(chunk_dec);
                fclose(fout);
            }
        }

        free(name_enc);
        free(name_dec);
        mbedtls_aes_free(&aes);
    }

    fclose(fd);

    if (!pbp_extracted) {
        printf("\nEBOOT.PBP not found in PKG.\n");
        return 0;
    }

    if (is_psx) {
        char bin_path[1024], cue_path[1024];
        char bin_filename[256];
        
        snprintf(bin_filename, sizeof(bin_filename), "%s.BIN", safe_name);
        snprintf(bin_path, sizeof(bin_path), "%s/%s", output_dir, bin_filename);
        snprintf(cue_path, sizeof(cue_path), "%s/%s.CUE", output_dir, safe_name);

        printf("\n  Unpacking PSAR to BIN/CUE...\n");
        if (extract_psar_to_bin(pbp_temp_path, bin_path)) {
            generate_cue_file(cue_path, bin_filename);
            remove(pbp_temp_path);
            printf("\n\n  \x1b[1;32mPSX CUE/BIN created.\x1b[0m\n");
            return 1;
        } else {
            return 0;
        }
    } else {
        char final_pbp_path[1024];
        snprintf(final_pbp_path, sizeof(final_pbp_path), "%s/%s.PBP", output_dir, safe_name);
        rename(pbp_temp_path, final_pbp_path);
        printf("\n\n  \x1b[1;32mPSP PBP extracted.\x1b[0m\n");
        return 1;
    }
}
