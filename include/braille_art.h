#ifndef BRAILLE_ART_H
#define BRAILLE_H

#include <stdint.h>
#include <stddef.h>

#define BRAILLE_CELL_W 2
#define BRAILLE_CELL_H 4

#define BRAILLE_MIN_SIZE 5
#define BRAILLE_MAX_SIZE 15
#define BRAILLE_DEFAULT_THRESHOLD 128

int load_png_image(const char *filepath, unsigned char **rgba_out, int *w, int *h);
int load_jpeg_image(const char *filepath, unsigned char **rgb_out, int *w, int *h);
int load_image(const char *filepath, unsigned char **rgba_out, int *w, int *h);

int rgba_to_gray(const unsigned char *rgba, int w, int h, unsigned char *gray);
int resize_gray(const unsigned char *src, int sw, int sh, unsigned char *dst, int dw, int dh);
void threshold_gray(const unsigned char *gray, int w, int h, unsigned char *binary, uint8_t thresh);
int binary_to_braille(const unsigned char *binary, int w, int h, int cols, int rows, uint32_t *output);
int braille_to_utf8(uint32_t codepoint, char *utf8_buf);
void print_braille_art(const uint32_t *braille, int rows, int cols);

int generate_braille_art_dynamic(const char *image_path,
                                  uint8_t threshold,
                                  int target_width,
                                  int target_height,
                                  char *output,
                                  size_t output_size);

int calculate_optimal_cover_size(int controls_height);

int get_braille_art_lines(const char *braille_art, char **lines, int max_lines);

#endif
