#ifndef CONVERTER_H
#define CONVERTER_H

int extract_game_from_pkg(const char *input_pkg, const char *output_dir, const char *safe_name, int is_psx);
int extract_archive(const char *filename, const char *dest_dir);

#endif
