#ifndef REMOTE_H
#define REMOTE_H

#include "defs.h"
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

#endif
