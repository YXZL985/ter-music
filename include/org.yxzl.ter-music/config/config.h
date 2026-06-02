/**
 * @file config_xml.h
 * @brief XML config serialization API
 *
 * @author ter-music team
 * @date 2026-06-01
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Extern globals ── */
extern AppConfig g_app_config;

/* SIGHUP hot-reload flag; set by signal handler, checked in event loop */
extern volatile sig_atomic_t g_config_reload_requested;

/**
 * Validate an XML config file against the expected schema.
 * @param path  Path to config.xml
 * @return 0 on success (schema matches), -1 on error
 */
int config_validate_xml(const char *path);

/**
 * Serialize AppConfig to an XML file.
 * @param path  Output path (e.g. "config.xml")
 * @param cfg   The configuration to serialize
 * @return 0 on success, -1 on error
 */
int config_save_to_xml(const char *path, const AppConfig *cfg);

/**
 * Deserialize AppConfig from an XML file.
 * Overlays parsed values into cfg (caller should memset or init_default first).
 * @param path  Path to config.xml
 * @param cfg   Output structure (will be partly overwritten)
 * @return 0 on success, -1 on error
 */
int config_load_from_xml(const char *path, AppConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
