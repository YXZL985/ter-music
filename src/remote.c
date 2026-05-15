#include "../include/remote.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

/* ---------- error reporting ---------- */

static char g_remote_errbuf[256];

void remote_set_error(const char *msg) {
    strncpy(g_remote_errbuf, msg ? msg : "", sizeof(g_remote_errbuf) - 1);
    g_remote_errbuf[sizeof(g_remote_errbuf) - 1] = '\0';
}

const char *remote_strerror(void) {
    return g_remote_errbuf;
}

/* ---------- lifecycle ---------- */

void remote_init(void) {
    curl_global_init(CURL_GLOBAL_ALL);
}

void remote_cleanup(void) {
    curl_global_cleanup();
}

/* ---------- path detection ---------- */

int remote_is_remote_path(const char *path) {
    return path && strstr(path, "://") != NULL;
}

/* ---------- URL construction ---------- */

static const char *protocol_scheme(int protocol) {
    switch (protocol) {
        case REMOTE_PROTOCOL_SMB:    return "smb";
        case REMOTE_PROTOCOL_SFTP:   return "sftp";
        case REMOTE_PROTOCOL_FTP:    return "ftp";
        case REMOTE_PROTOCOL_WEBDAV:
        case REMOTE_PROTOCOL_HTTP:   return "http";
        default:                     return "ftp";
    }
}

static int default_port(int protocol) {
    switch (protocol) {
        case REMOTE_PROTOCOL_SMB:    return 445;
        case REMOTE_PROTOCOL_SFTP:   return 22;
        case REMOTE_PROTOCOL_FTP:    return 21;
        case REMOTE_PROTOCOL_WEBDAV:
        case REMOTE_PROTOCOL_HTTP:   return 80;
        default:                     return 21;
    }
}

/* ---------- URL parsing ---------- */

int remote_parse_url(const char *url, RemoteConnectionConfig *conn) {
    if (!url || !conn) return -1;
    memset(conn, 0, sizeof(*conn));

    const char *scheme_end = strstr(url, "://");
    if (!scheme_end) return -1;

    size_t scheme_len = scheme_end - url;
    if (scheme_len == 3 && strncmp(url, "ftp", 3) == 0)
        conn->protocol = REMOTE_PROTOCOL_FTP;
    else if (scheme_len == 4 && strncmp(url, "sftp", 4) == 0)
        conn->protocol = REMOTE_PROTOCOL_SFTP;
    else if (scheme_len == 3 && strncmp(url, "smb", 3) == 0)
        conn->protocol = REMOTE_PROTOCOL_SMB;
    else if (scheme_len == 4 && strncmp(url, "http", 4) == 0)
        conn->protocol = REMOTE_PROTOCOL_HTTP;
    else if (scheme_len == 5 && strncmp(url, "https", 5) == 0) {
        conn->protocol = REMOTE_PROTOCOL_HTTP;
        conn->port = 443;
    }
    else
        return -1;

    const char *p = scheme_end + 3;
    if (!*p) return -1;

    // Extract user:pass@ if present
    const char *at = strrchr(p, '@');
    char host_part[512] = "";
    if (at) {
        size_t auth_len = at - p;
        char auth[256];
        size_t cp = auth_len < sizeof(auth) - 1 ? auth_len : sizeof(auth) - 1;
        strncpy(auth, p, cp);
        auth[cp] = '\0';

        const char *colon = strchr(auth, ':');
        if (colon) {
            size_t ulen = colon - auth;
            strncpy(conn->username, auth, ulen < sizeof(conn->username) - 1 ? ulen : sizeof(conn->username) - 1);
            strncpy(conn->password, colon + 1, sizeof(conn->password) - 1);
        } else {
            strncpy(conn->username, auth, sizeof(conn->username) - 1);
        }

        strncpy(host_part, at + 1, sizeof(host_part) - 1);
    } else {
        strncpy(host_part, p, sizeof(host_part) - 1);
    }

    // Extract host:port/path from host_part
    char *path_start = strchr(host_part, '/');
    char path_buf[512] = "";
    if (path_start) {
        strncpy(path_buf, path_start, sizeof(path_buf) - 1);
        *path_start = '\0';
    }

    // Check for IPv6: [addr]:port
    char *port_str = NULL;
    if (host_part[0] == '[') {
        char *close_bracket = strchr(host_part, ']');
        if (!close_bracket) return -1;
        size_t addr_len = close_bracket - host_part - 1;
        strncpy(conn->host, host_part + 1,
                addr_len < sizeof(conn->host) - 1 ? addr_len : sizeof(conn->host) - 1);
        if (close_bracket[1] == ':') port_str = close_bracket + 2;
    } else {
        char *colon = strrchr(host_part, ':');
        if (colon) {
            *colon = '\0';
            port_str = colon + 1;
            strncpy(conn->host, host_part, sizeof(conn->host) - 1);
        } else {
            strncpy(conn->host, host_part, sizeof(conn->host) - 1);
        }
    }

    if (port_str && *port_str) conn->port = atoi(port_str);
    if (conn->port <= 0) conn->port = default_port(conn->protocol);

    // Store path as base_path
    if (path_buf[0]) {
        strncpy(conn->base_path, path_buf, sizeof(conn->base_path) - 1);
    } else {
        conn->base_path[0] = '/';
        conn->base_path[1] = '\0';
    }

    snprintf(conn->name, sizeof(conn->name), "%.64s", host_part);
    return 0;
}

void remote_build_url(const RemoteConnectionConfig *conn,
                      const char *subpath,
                      char *url, size_t url_size) {
    const char *scheme = protocol_scheme(conn->protocol);
    int port = conn->port > 0 ? conn->port : default_port(conn->protocol);
    int need_port = (port != default_port(conn->protocol));

    // strip leading slash from subpath for proper concatenation
    while (*subpath == '/') subpath++;

    char creds[128] = "";
    if (conn->username[0]) {
        snprintf(creds, sizeof(creds), "%s:%s@", conn->username, conn->password);
    }

    if (conn->protocol == REMOTE_PROTOCOL_WEBDAV || conn->protocol == REMOTE_PROTOCOL_HTTP) {
        int use_https = (port == 443);
        const char *http_scheme = use_https ? "https" : "http";

        if (need_port) {
            snprintf(url, url_size, "%s://%s%s:%d/%s",
                     http_scheme, creds, conn->host, port, subpath);
        } else {
            snprintf(url, url_size, "%s://%s%s/%s",
                     http_scheme, creds, conn->host, subpath);
        }
    } else {
        if (need_port) {
            snprintf(url, url_size, "%s://%s%s:%d/%s",
                     scheme, creds, conn->host, port, subpath);
        } else {
            snprintf(url, url_size, "%s://%s%s/%s",
                     scheme, creds, conn->host, subpath);
        }
    }
}

/* ---------- directory listing ---------- */

struct write_buf {
    char *data;
    size_t len;
    size_t cap;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    struct write_buf *buf = (struct write_buf *)userdata;

    if (buf->len + total + 1 > buf->cap) {
        buf->cap = buf->len + total + 8192;
        char *tmp = realloc(buf->data, buf->cap);
        if (!tmp) return 0;
        buf->data = tmp;
    }

    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

static CURL *create_curl_handle(const RemoteConnectionConfig *conn,
                                const char *full_url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, full_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);

    if (conn->username[0]) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, conn->username);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, conn->password);
    }

    if (conn->protocol == REMOTE_PROTOCOL_SFTP && conn->private_key_path[0]) {
        curl_easy_setopt(curl, CURLOPT_SSH_PRIVATE_KEYFILE, conn->private_key_path);
        curl_easy_setopt(curl, CURLOPT_SSH_AUTH_TYPES, CURLSSH_AUTH_PUBLICKEY);
    }

    return curl;
}

/* Parse FTP MLSD / NLST response */
static int parse_ftp_listing(const char *data, size_t len,
                             RemoteDirEntry **out, int *out_count) {
    int cap = 64;
    int count = 0;
    *out = malloc(sizeof(RemoteDirEntry) * cap);
    if (!*out) return -1;

    const char *p = data;
    while (p && *p && (size_t)(p - data) < len) {
        // skip empty lines
        while (*p == '\r' || *p == '\n') p++;
        if (!*p) break;

        const char *end = strchr(p, '\n');
        if (!end) end = strchr(p, '\r');
        if (!end) end = p + strlen(p);

        size_t line_len = end - p;
        if (line_len == 0) { p = end; continue; }

        char line[1024];
        size_t copy = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
        strncpy(line, p, copy);
        line[copy] = '\0';

        int is_dir = 0;
        char fname[256] = "";

        // MLSD format: "type=dir;size=123;modify=...; /path/name"
        if (strstr(line, "type=") != NULL) {
            if (strstr(line, "type=dir")) is_dir = 1;
            const char *semi = strrchr(line, ';');
            const char *name_start = semi ? semi + 1 : line;
            while (*name_start == ' ') name_start++;
            strncpy(fname, name_start, sizeof(fname) - 1);
        } else if (line[0] == '-' || line[0] == 'd' || line[0] == 'l') {
            // UNIX ls -l format: "permissions links owner group size month day HH:MM name"
            is_dir = (line[0] == 'd');
            // Skip the first 8 space-delimited fields (perms, links, owner, group,
            // size, month, day, time/year) — then everything remaining is the filename.
            int fields = 0;
            const char *fp = line;
            while (*fp && fields < 8) {
                while (*fp == ' ') fp++;
                if (!*fp) break;
                fields++;
                while (*fp && *fp != ' ') fp++;
            }
            if (fields >= 8) {
                while (*fp == ' ') fp++;
                // Handle symlinks: strip " -> target"
                const char *arrow = strstr(fp, " -> ");
                if (arrow) {
                    size_t nlen = arrow - fp;
                    if (nlen < sizeof(fname))
                        strncpy(fname, fp, nlen);
                    fname[nlen] = '\0';
                } else {
                    strncpy(fname, fp, sizeof(fname) - 1);
                }
            } else {
                strncpy(fname, line, sizeof(fname) - 1);
            }
        } else {
            // NLST: just a filename per line
            strncpy(fname, line, sizeof(fname) - 1);
        }

        // trim trailing whitespace
        size_t flen = strlen(fname);
        while (flen > 0 && (fname[flen-1] == ' ' || fname[flen-1] == '\r')) {
            fname[--flen] = '\0';
        }

        if (fname[0] && strcmp(fname, ".") != 0 && strcmp(fname, "..") != 0) {
            if (count >= cap) {
                cap *= 2;
                RemoteDirEntry *tmp = realloc(*out, sizeof(RemoteDirEntry) * cap);
                if (!tmp) break;
                *out = tmp;
            }
            strncpy((*out)[count].name, fname, sizeof((*out)[count].name) - 1);
            (*out)[count].name[sizeof((*out)[count].name) - 1] = '\0';
            (*out)[count].is_dir = is_dir;
            count++;
        }

        p = end + 1;
    }

    *out_count = count;
    return 0;
}

/* Parse WebDAV PROPFIND XML response (minimal parser) */
static int parse_webdav_listing(const char *data, size_t len,
                                RemoteDirEntry **out, int *out_count) {
    int cap = 64;
    int count = 0;
    *out = malloc(sizeof(RemoteDirEntry) * cap);
    if (!*out) return -1;

    // Simple state machine over the XML
    const char *p = data;
    while (p && *p && (size_t)(p - data) < len) {
        // Look for <response> or <d:response> or <D:response>
        const char *resp_start = NULL;
        if ((resp_start = strstr(p, "<response>")) == NULL &&
            (resp_start = strstr(p, "<d:response>")) == NULL &&
            (resp_start = strstr(p, "<D:response>")) == NULL) {
            break;
        }
        p = resp_start + 1;

        const char *resp_end = strstr(p, "</response>");
        if (!resp_end &&
            (resp_end = strstr(p, "</d:response>")) == NULL &&
            (resp_end = strstr(p, "</D:response>")) == NULL) {
            break;
        }

        // Extract href (between <href> and </href>)
        const char *href_start = strstr(p, "<href>");
        if (!href_start || href_start > resp_end) { p = resp_end + 1; continue; }
        href_start += 6;
        const char *href_end = strstr(href_start, "</href>");
        if (!href_end) { p = resp_end + 1; continue; }

        size_t href_len = href_end - href_start;
        char href[1024];
        size_t copy = href_len < sizeof(href) - 1 ? href_len : sizeof(href) - 1;
        strncpy(href, href_start, copy);
        href[copy] = '\0';

        // Extract collection flag from <collection/> or <d:collection/>
        int is_dir = 0;
        if (strstr(p, "<collection/>") || strstr(p, "<collection />") ||
            strstr(p, "<d:collection/>") || strstr(p, "<d:collection />") ||
            strstr(p, "<D:collection/>")) {
            is_dir = 1;
        }

        // Extract just the filename from href
        const char *name = href;
        const char *last_slash = strrchr(href, '/');
        if (last_slash) name = last_slash + 1;

        // URL-decode (simple version)
        char fname[256];
        int fi = 0, si = 0;
        while (name[si] && fi < (int)sizeof(fname) - 1) {
            if (name[si] == '%' && name[si+1] && name[si+2]) {
                char hex[3] = {name[si+1], name[si+2], 0};
                fname[fi++] = (char)strtol(hex, NULL, 16);
                si += 3;
            } else if (name[si] == '+') {
                fname[fi++] = ' ';
                si++;
            } else {
                fname[fi++] = name[si++];
            }
        }
        fname[fi] = '\0';

        if (fname[0] && strcmp(fname, ".") != 0 && strcmp(fname, "..") != 0) {
            if (count >= cap) {
                cap *= 2;
                RemoteDirEntry *tmp = realloc(*out, sizeof(RemoteDirEntry) * cap);
                if (!tmp) break;
                *out = tmp;
            }
            strncpy((*out)[count].name, fname, sizeof((*out)[count].name) - 1);
            (*out)[count].name[sizeof((*out)[count].name) - 1] = '\0';
            (*out)[count].is_dir = is_dir;
            count++;
        }

        p = resp_end + 1;
    }

    *out_count = count;
    return 0;
}

/* ---------- HTML autoindex parser (nginx, Apache, python http.server) ---------- */

static int parse_html_listing(const char *data, size_t len,
                               RemoteDirEntry **out, int *out_count) {
    int cap = 64;
    int count = 0;
    *out = malloc(sizeof(RemoteDirEntry) * cap);
    if (!*out) return -1;

    const char *p = data;
    while (p && *p && (size_t)(p - data) < len) {
        // Find next <a href="
        const char *href_start = strstr(p, "<a href=\"");
        if (!href_start) break;

        href_start += 9; // skip past '<a href="'
        const char *href_end = strchr(href_start, '"');
        if (!href_end) break;

        size_t href_len = href_end - href_start;
        if (href_len == 0 || href_len >= 1024) { p = href_end + 1; continue; }

        char href[1024];
        strncpy(href, href_start, href_len);
        href[href_len] = '\0';

        // Skip parent directory and self links
        if (strcmp(href, "../") == 0 || strcmp(href, "./") == 0 ||
            strcmp(href, "..") == 0) {
            p = href_end + 1;
            continue;
        }
        // Skip query strings and anchor links
        if (strchr(href, '?') || strchr(href, '#')) {
            p = href_end + 1;
            continue;
        }

        // Find the display name: >text</a>
        const char *close_tag = strstr(href_end, "</a>");
        if (!close_tag) break;

        // Find the '>' that precedes the text
        const char *gt = href_end + 1;
        while (gt < close_tag && *gt != '>') gt++;
        if (gt >= close_tag) { p = close_tag + 4; continue; }

        const char *text_start = gt + 1;
        const char *text_end = close_tag;
        size_t text_len = text_end - text_start;

        // Skip if no visible text
        if (text_len == 0 || text_len >= 256) { p = close_tag + 4; continue; }

        char fname[256];
        strncpy(fname, text_start, text_len);
        fname[text_len] = '\0';

        // trim trailing whitespace
        size_t flen = strlen(fname);
        while (flen > 0 && (fname[flen-1] == ' ' || fname[flen-1] == '\r' || fname[flen-1] == '\n'))
            fname[--flen] = '\0';

        // Determine if directory: href ends with '/'
        int is_dir = (href_len > 0 && href[href_len - 1] == '/');

        if (fname[0]) {
            if (count >= cap) {
                cap *= 2;
                RemoteDirEntry *tmp = realloc(*out, sizeof(RemoteDirEntry) * cap);
                if (!tmp) break;
                *out = tmp;
            }
            strncpy((*out)[count].name, fname, sizeof((*out)[count].name) - 1);
            (*out)[count].name[sizeof((*out)[count].name) - 1] = '\0';
            (*out)[count].is_dir = is_dir;
            count++;
        }

        p = close_tag + 4;
    }

    *out_count = count;
    return (count > 0 || *out) ? 0 : -1;
}

int remote_list_directory(const RemoteConnectionConfig *conn,
                          const char *subpath,
                          RemoteDirEntry **out_entries,
                          int *out_count) {
    g_remote_errbuf[0] = '\0';  // clear previous error
    char url[2048];
    remote_build_url(conn, subpath, url, sizeof(url));

    CURL *curl = create_curl_handle(conn, url);
    if (!curl) return -1;

    struct write_buf buf = {0};
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res;

    if (conn->protocol == REMOTE_PROTOCOL_WEBDAV) {
        // PROPFIND for WebDAV
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Depth: 1");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");

        res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            remote_set_error(curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            free(buf.data);
            return -1;
        }
        // Check HTTP response code
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code >= 400) {
            curl_easy_cleanup(curl);
            free(buf.data);
            remote_set_error(http_code == 401 ? "Authentication failed" :
                             http_code == 404 ? "Not found" :
                             http_code == 403 ? "Forbidden" :
                             http_code == 500 ? "Server error" : "HTTP error");
            return -1;
        }
        curl_easy_cleanup(curl);

        int ret = parse_webdav_listing(buf.data, buf.len, out_entries, out_count);
        free(buf.data);
        return ret;

    } else if (conn->protocol == REMOTE_PROTOCOL_FTP) {
        // Try MLSD first, fall back to NLST
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "MLSD");
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            // fall back to NLST
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);
            curl_easy_setopt(curl, CURLOPT_URL, url); // re-set URL
            res = curl_easy_perform(curl);
        }

        if (res != CURLE_OK) {
            remote_set_error(curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            free(buf.data);
            return -1;
        }
        curl_easy_cleanup(curl);

        // Parse result into entries
        // With MLSD: lines like "type=dir;modify=...; /dirname"
        // With NLST: just filenames
        // We need to try to determine which is which - check if lines contain "type="
        int has_mlsd = (strstr(buf.data, "type=") != NULL);
        int ret = parse_ftp_listing(buf.data, buf.len, out_entries, out_count);

        // For NLST, we can't tell dirs from files without extra requests.
        // If we used NLST, try to detect dirs by appending "/" to each entry
        // and checking with CURLOPT_NOBODY. Skip this for now - treat all as files.
        if (!has_mlsd && *out_entries && *out_count > 0) {
            // All entries from NLST are marked as files initially
            // Users will see dir entries too, which is fine for browsing
        }

        free(buf.data);
        return ret;

    } else if (conn->protocol == REMOTE_PROTOCOL_HTTP) {
        // Plain HTTP GET for nginx/Apache autoindex
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            remote_set_error(curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            free(buf.data);
            return -1;
        }

        // Check HTTP response code
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code >= 400) {
            curl_easy_cleanup(curl);
            free(buf.data);
            remote_set_error(http_code == 401 ? "Authentication failed" :
                             http_code == 403 ? "Forbidden" :
                             http_code == 404 ? "Not found" : "HTTP error");
            return -1;
        }
        curl_easy_cleanup(curl);

        int ret = parse_html_listing(buf.data, buf.len, out_entries, out_count);
        free(buf.data);
        return ret;

    } else {
        // SMB and SFTP – libcurl returns raw listing lines
        curl_easy_setopt(curl, CURLOPT_URL, url);
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            remote_set_error(curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            free(buf.data);
            return -1;
        }
        curl_easy_cleanup(curl);

        // Parse: for SFTP, lines look like:
        // "drwxr-xr-x 3 user group 4096 Jan 1 00:00 dirname"
        // "-rw-r--r-- 1 user group 1234 Jan 1 00:00 filename.mp3"
        // For SMB, lines may be just filenames
        int cap = 64;
        int count = 0;
        *out_entries = malloc(sizeof(RemoteDirEntry) * cap);
        if (!*out_entries) { free(buf.data); return -1; }

        const char *p = buf.data;
        while (p && *p && (size_t)(p - buf.data) < buf.len) {
            while (*p == '\r' || *p == '\n') p++;
            if (!*p) break;

            const char *end = strchr(p, '\n');
            if (!end) end = strchr(p, '\r');
            if (!end) end = p + strlen(p);

            size_t line_len = end - p;
            if (line_len == 0 || line_len >= 2048) { p = end; continue; }

            char line[2048];
            strncpy(line, p, line_len);
            line[line_len] = '\0';

            int is_dir = 0;
            char fname[256] = "";

            // SFTP ls -l format
            if (line_len > 10 && (line[0] == 'd' || line[0] == '-')) {
                is_dir = (line[0] == 'd');
                // Find the filename after the date/time fields
                // Format: "permissions links user group size month day time name"
                // or "permissions links user group size month day year name"
                // Find the 8th space-delimited field... actually just take last field
                if (line_len > 50) {
                    const char *last_space = NULL;
                    const char *scan = line + line_len - 1;
                    while (scan > line) {
                        if (*scan == ' ') { last_space = scan; break; }
                        scan--;
                    }
                    // Find the field before last_space (the time/date field)
                    if (last_space && last_space > line + 40) {
                        const char *time_field_start = last_space - 1;
                        while (time_field_start > line && *time_field_start != ' ') time_field_start--;
                        if (*time_field_start == ' ') time_field_start++;
                        // Now name is everything after last_space
                        strncpy(fname, last_space + 1, sizeof(fname) - 1);
                    } else {
                        // Fallback: whole line as name
                        strncpy(fname, line, sizeof(fname) - 1);
                    }
                } else {
                    strncpy(fname, line, sizeof(fname) - 1);
                }
            } else {
                // SMB or other simple format
                strncpy(fname, line, sizeof(fname) - 1);
            }

            // trim
            size_t flen = strlen(fname);
            while (flen > 0 && (fname[flen-1] == ' ' || fname[flen-1] == '\r' || fname[flen-1] == '\n')) {
                fname[--flen] = '\0';
            }

            if (fname[0] && strcmp(fname, ".") != 0 && strcmp(fname, "..") != 0) {
                if (count >= cap) {
                    cap *= 2;
                    RemoteDirEntry *tmp = realloc(*out_entries, sizeof(RemoteDirEntry) * cap);
                    if (!tmp) break;
                    *out_entries = tmp;
                }
                strncpy((*out_entries)[count].name, fname, sizeof((*out_entries)[count].name) - 1);
                (*out_entries)[count].name[sizeof((*out_entries)[count].name) - 1] = '\0';
                (*out_entries)[count].is_dir = is_dir;
                count++;
            }

            p = end + 1;
        }

        *out_count = count;
        free(buf.data);
        return 0;
    }
}

void remote_free_entries(RemoteDirEntry *entries, int count) {
    (void)count;
    free(entries);
}

/* ---------- protocol name ---------- */

const char *remote_protocol_name(int protocol) {
    switch (protocol) {
        case REMOTE_PROTOCOL_SMB:    return "SMB";
        case REMOTE_PROTOCOL_SFTP:   return "SFTP";
        case REMOTE_PROTOCOL_FTP:    return "FTP";
        case REMOTE_PROTOCOL_WEBDAV: return "WebDAV";
        case REMOTE_PROTOCOL_HTTP:   return "HTTP";
        default:                     return "?";
    }
}
