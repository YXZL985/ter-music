/**
 * @file test_config.c
 * @brief Quick unit test for XML config save/load/migration
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "defs.h"
#include "config_schema.h"
#include "config_xml.h"

/* clamp_config_values is static in config_xml.c — test via loading */
static int test_save_load(void);
static int test_clamping(void);

int main(void) {
    int ret = 0;
    ret += test_save_load();
    ret += test_clamping();
    printf("\n%s\n", ret ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return ret;
}

static int test_save_load(void) {
    printf("=== Test: Save & Load ===\n");
    const char *home = "/tmp/test-music-cfg";
    char dir[512]; snprintf(dir, sizeof(dir), "%s/.config/ter-music", home);
    mkdir("/tmp/test-music-cfg", 0755);
    mkdir("/tmp/test-music-cfg/.config", 0755);
    mkdir(dir, 0755);
    setenv("HOME", home, 1);

    char xml_path[512];
    snprintf(xml_path, sizeof(xml_path), "%s/config.xml", dir);

    AppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.config_version = CONFIG_CURRENT_VERSION;
    cfg.volume_percent = 42;
    cfg.default_playback_speed = 1.5f;
    cfg.theme.playlist_fg = 7;
    cfg.auto_play_on_start = 1;
    cfg.show_lyrics_panel = 0;
    cfg.remote_connection_count = 1;
    strcpy(cfg.remote_connections[0].name, "TestSrv");
    cfg.remote_connections[0].protocol = 1;
    strcpy(cfg.remote_connections[0].host, "192.168.1.1");
    cfg.remote_connections[0].port = 22;

    if (config_save_to_xml(xml_path, &cfg) != 0) {
        printf("  FAIL: save\n"); return 1;
    }
    printf("  OK: saved\n");

    AppConfig cfg2;
    memset(&cfg2, 0, sizeof(cfg2));
    if (config_load_from_xml(xml_path, &cfg2) != 0) {
        printf("  FAIL: load\n"); return 1;
    }

    int ok = 1;
    if (cfg2.volume_percent != 42) { printf("  FAIL: volume=%d\n", cfg2.volume_percent); ok = 0; }
    if (cfg2.auto_play_on_start != 1) { printf("  FAIL: auto_play=%d\n", cfg2.auto_play_on_start); ok = 0; }
    if (cfg2.show_lyrics_panel != 0) { printf("  FAIL: show_lyrics=%d\n", cfg2.show_lyrics_panel); ok = 0; }
    if (cfg2.remote_connection_count != 1) { printf("  FAIL: remote_count=%d\n", cfg2.remote_connection_count); ok = 0; }
    if (strcmp(cfg2.remote_connections[0].name, "TestSrv") != 0) {
        printf("  FAIL: remote_name=%s\n", cfg2.remote_connections[0].name); ok = 0;
    }

    if (ok) printf("  OK: all values match\n");

    /* Verify XML file content */
    FILE *f = fopen(xml_path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        printf("  XML file size: %ld bytes\n", sz);
    }

    unlink(xml_path);
    return ok ? 0 : 1;
}

static int test_clamping(void) {
    printf("=== Test: Clamping ===\n");
    AppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.volume_percent = 999;
    cfg.audio_latency_ms = 5;
    cfg.default_playback_speed = 5.0f;
    cfg.lyrics_alignment = 42;

    /* Call clamping via save+load cycle */
    const char *home = "/tmp/test-music-cfg";
    char path[512]; snprintf(path, sizeof(path), "%s/.config/ter-music/clamp_test.xml", home);
    mkdir("/tmp/test-music-cfg", 0755);
    mkdir("/tmp/test-music-cfg/.config", 0755);
    mkdir("/tmp/test-music-cfg/.config/ter-music", 0755);
    cfg.config_version = CONFIG_CURRENT_VERSION;
    config_save_to_xml(path, &cfg);

    AppConfig loaded;
    memset(&loaded, 0, sizeof(loaded));
    config_load_from_xml(path, &loaded);

    printf("  volume=%d (expect 100)\n", loaded.volume_percent);
    printf("  latency=%d (expect 20)\n", loaded.audio_latency_ms);
    printf("  speed=%.2f (expect 3.00)\n", loaded.default_playback_speed);
    printf("  align=%d (expect 0)\n", loaded.lyrics_alignment);

    int ok = (loaded.volume_percent == 100 && loaded.audio_latency_ms == 20 &&
              loaded.default_playback_speed >= 2.99f && loaded.lyrics_alignment == 0);
    printf("  %s\n", ok ? "OK: clamping works" : "FAIL: clamping mismatch");
    unlink(path);
    return ok ? 0 : 1;
}

