#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "braille_art.h"

void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <image.png> [--threshold N]\n", prog);
    fprintf(stderr, "Convert PNG image to 20x20 braille art\n");
    fprintf(stderr, "  --threshold N  Set threshold (0-255, default 128)\n");
    fprintf(stderr, "  --help         Show this help\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *image_path = NULL;
    uint8_t threshold = 128;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--threshold") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --threshold requires a value\n");
                return 1;
            }
            int val = atoi(argv[i + 1]);
            if (val < 0 || val > 255) {
                fprintf(stderr, "Error: threshold must be 0-255\n");
                return 1;
            }
            threshold = (uint8_t)val;
            i++;
        } else if (argv[i][0] != '-') {
            image_path = argv[i];
        }
    }

    if (!image_path) {
        fprintf(stderr, "Error: no image file specified\n");
        return 1;
    }

    unsigned char *rgba = NULL;
    int w, h;

    if (load_image(image_path, &rgba, &w, &h) != 0) {
        fprintf(stderr, "Error: failed to load image: %s\n", image_path);
        return 1;
    }

    unsigned char *gray = malloc(w * h);
    unsigned char *resized = malloc(BRAILLE_WIDTH * BRAILLE_HEIGHT);
    unsigned char *binary = malloc(BRAILLE_WIDTH * BRAILLE_HEIGHT);
    uint32_t *braille = malloc(BRAILLE_ROWS * BRAILLE_COLS * sizeof(uint32_t));

    if (!gray || !resized || !binary || !braille) {
        free(rgba); free(gray); free(resized); free(binary); free(braille);
        fprintf(stderr, "Error: memory allocation failed\n");
        return 1;
    }

    rgba_to_gray(rgba, w, h, gray);
    free(rgba);
    rgba = NULL;

    resize_gray(gray, w, h, resized, BRAILLE_WIDTH, BRAILLE_HEIGHT);
    free(gray);
    gray = NULL;

    threshold_gray(resized, BRAILLE_WIDTH, BRAILLE_HEIGHT, binary, threshold);
    free(resized);
    resized = NULL;

    if (binary_to_braille(binary, BRAILLE_WIDTH, BRAILLE_HEIGHT, braille) != 0) {
        free(binary); free(braille);
        fprintf(stderr, "Error: failed to convert to braille\n");
        return 1;
    }
    free(binary);
    binary = NULL;

    print_braille_art(braille, BRAILLE_ROWS, BRAILLE_COLS);
    free(braille);

    return 0;
}
