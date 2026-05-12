#ifndef BRAILLE_ART_H
#define BRAILLE_ART_H

#include <stdint.h>

int load_png_image(const char *filepath, unsigned char **rgba_out, int *w, int *h);
int load_jpeg_image(const char *filepath, unsigned char **rgb_out, int *w, int *h);
int load_image(const char *filepath, unsigned char **rgba_out, int *w, int *h);

#define BRAILLE_WIDTH 20
#define BRAILLE_HEIGHT 20
#define BRAILLE_CELL_W 2
#define BRAILLE_CELL_H 4
#define BRAILLE_COLS (BRAILLE_WIDTH / BRAILLE_CELL_W)
#define BRAILLE_ROWS (BRAILLE_HEIGHT / BRAILLE_CELL_H)

int rgba_to_gray(const unsigned char *rgba, int w, int h, unsigned char *gray);
int resize_gray(const unsigned char *src, int sw, int sh, unsigned char *dst, int dw, int dh);
void threshold_gray(const unsigned char *gray, int w, int h, unsigned char *binary, uint8_t thresh);
int binary_to_braille(const unsigned char *binary, int w, int h, uint32_t *output);
int braille_to_utf8(uint32_t codepoint, char *utf8_buf);
void print_braille_art(const uint32_t *braille, int rows, int cols);
int generate_braille_art(const char *image_path, uint8_t threshold, char *output, size_t output_size);

#endif
