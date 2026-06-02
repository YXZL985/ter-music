#ifndef REMOTE_H
#define REMOTE_H

#include "types.h"
#include <stddef.h>
#include <stdlib.h>

typedef struct {
    char name[256];
    int is_dir;
} RemoteDirEntry;

// Lifecycle
void remote_init(void);
void remote_cleanup(void);

// Path detection
int remote_is_remote_path(const char *path);

// URL construction
void remote_build_url(const RemoteConnectionConfig *conn,
                      const char *subpath,
                      char *url, size_t url_size);

// Percent-encode non-ASCII and URL-special characters in a path.
// Preserves '/' for path structure.  Output buffer must be at least
// available_input_len * 3 + 1 bytes for correct encoding of all inputs.
void remote_encode_url_path(const char *in, char *out, size_t out_size);
void remote_url_decode(const char *in, char *out, size_t out_size);

// Directory listing – returns number of entries, or -1 on error.
// Call remote_free_entries() when done.
int remote_list_directory(const RemoteConnectionConfig *conn,
                          const char *subpath,
                          RemoteDirEntry **out_entries,
                          int *out_count);
void remote_free_entries(RemoteDirEntry *entries, int count);

// Protocol name for display
const char *remote_protocol_name(int protocol);

// URL parsing – parse "protocol://user:pass@host:port/path" into config
// Returns 0 on success, -1 on parse failure.
int remote_parse_url(const char *url, RemoteConnectionConfig *conn);

// Error reporting – returns last error message (empty string if none)
const char *remote_strerror(void);

// Download a remote URL into a malloc'd buffer (caller must free).
// Returns 0 on success, -1 on error.
int remote_fetch_to_buffer(const char *url, unsigned char **data, size_t *size);

// Download a remote URL directly to a local file.
// Returns 0 on success, -1 on error (partial file is cleaned up).
int remote_fetch_to_file(const char *url, const char *dest_path);

// Set a hook that is called periodically during remote download to keep the
// UI responsive.  Pass NULL to unset.  The hook is called from inside curl's
// blocking I/O, so it must be safe to call from any context (typically just
// calls refresh() or similar).
void remote_set_progress_hook(void (*hook)(void));

#endif
