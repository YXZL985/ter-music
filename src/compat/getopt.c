/**
 * @file compat/getopt.c
 * @brief getopt / getopt_long standalone implementation for MSVC
 *
 * BSD-style implementation, does not depend on any non-standard headers.
 * Windows (MSVC) lacks <getopt.h>; this file provides a self-contained
 * replacement.  On non-WIN32 platforms this file is intentionally empty
 * (the system <getopt.h> is used instead).
 *
 * Compile only under WIN32:
 *   #ifdef _WIN32  →  compile this file
 *
 * @author ter-music team
 */

#ifdef _WIN32

#include "compat/getopt.h"
#include <string.h>
#include <stdio.h>

/* ── Global state ───────────────────────────────────────────── */
char *optarg = NULL;
int   optind = 1;
int   opterr = 1;
int   optopt = 0;
int   optreset = 0;

/* Internal scanner state */
static int  sp     = 1;       /* character index inside current argv element */
static int  have_short = 0;  /* non-zero when a short-option scan is active */

/* ── Forward ────────────────────────────────────────────────── */
static int scan_long(int argc, char * const argv[],
                     const char *optstring,
                     const struct option *longopts, int *longindex);

/* ── getopt (short options only) ────────────────────────────── */

int getopt(int argc, char * const argv[], const char *optstring)
{
    int c;
    const char *cp;

    if (optreset) { sp = 1; optreset = 0; have_short = 0; }

    if (sp == 1) {
        /* End of args, or current arg is not an option */
        if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
            return -1;

        /* "--" means end of options */
        if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
            optind++;
            return -1;
        }

        /* Long option starting with -- */
        if (argv[optind][1] == '-' && argv[optind][2] != '\0') {
            have_short = 0;
            return scan_long(argc, argv, optstring, NULL, NULL);
        }

        have_short = 1;
    }

    /* Short options */
    if (!have_short) {
        /* We were in long-option mode; reset for next arg */
        sp = 1;
        optind++;
        return getopt(argc, argv, optstring);
    }

    c = (unsigned char)argv[optind][sp];
    optopt = c;

    cp = strchr(optstring, c);
    if (c == ':' || cp == NULL) {
        if (argv[optind][++sp] == '\0') { sp = 1; optind++; }
        if (opterr)
            fprintf(stderr, "%s: illegal option -- %c\n", argv[0], c);
        return '?';
    }

    if (cp[1] == ':') {
        /* Option requires an argument */
        if (cp[2] == ':') {
            /* Optional argument */
            if (argv[optind][sp + 1] != '\0') {
                optarg = &argv[optind][sp + 1];
                sp = 1; optind++;
            } else {
                optarg = NULL;
                sp = 1; optind++;
            }
        } else {
            /* Required argument */
            if (argv[optind][sp + 1] != '\0') {
                optarg = &argv[optind][sp + 1];
                sp = 1; optind++;
            } else if (++optind >= argc) {
                sp = 1;
                if (opterr)
                    fprintf(stderr, "%s: option requires an argument -- %c\n", argv[0], c);
                return (optstring[0] == ':') ? ':' : '?';
            } else {
                optarg = argv[optind++];
                sp = 1;
            }
        }
    } else {
        /* No argument */
        if (argv[optind][++sp] == '\0') { sp = 1; optind++; }
        optarg = NULL;
    }

    return c;
}

/* ── getopt_long ────────────────────────────────────────────── */

int getopt_long(int argc, char * const argv[],
                const char *optstring,
                const struct option *longopts, int *longindex)
{
    int r;

    if (optreset) { sp = 1; optreset = 0; have_short = 0; }

    /* If we are in the middle of a short-option cluster, finish it first */
    if (have_short && sp > 1 && argv[optind] && argv[optind][sp] != '\0') {
        return getopt(argc, argv, optstring);
    }

    /* Check for long option */
    if (optind < argc && argv[optind] &&
        argv[optind][0] == '-' && argv[optind][1] == '-' && argv[optind][2] != '\0') {
        have_short = 0;
        r = scan_long(argc, argv, optstring, longopts, longindex);
        if (r != -1) return r;
        /* scan_long returned -1 → not a known long opt → fall through */
    }

    /* Otherwise use short-option scanner */
    have_short = 1;
    return getopt(argc, argv, optstring);
}

/* ── Long option scanner ────────────────────────────────────── */

static int scan_long(int argc, char * const argv[],
                     const char *optstring,
                     const struct option *longopts, int *longindex)
{
    const char *name;
    int  match_count = 0;
    int  match_index = -1;
    size_t name_len;

    if (optind >= argc) return -1;

    name    = argv[optind] + 2;  /* skip "--" */
    name_len = strcspn(name, "=");

    if (longopts) {
        /* Scan the long option table */
        for (int i = 0; longopts[i].name != NULL; i++) {
            if (strncmp(longopts[i].name, name, name_len) == 0 &&
                strlen(longopts[i].name) == name_len) {
                match_count++;
                match_index = i;
            }
        }
    }

    if (match_count == 1) {
        /* Exact match */
        const struct option *lo = &longopts[match_index];
        if (longindex) *longindex = match_index;

        if (lo->has_arg == required_argument) {
            /* --opt=value or --opt value */
            if (name[name_len] == '=') {
                optarg = (char *)&name[name_len + 1];
                optind++;
            } else if (optind + 1 < argc) {
                optarg = argv[optind + 1];
                optind += 2;
            } else {
                /* Missing argument */
                if (opterr)
                    fprintf(stderr, "%s: option '--%s' requires an argument\n",
                            argv[0], lo->name);
                optind++;
                return (optstring && optstring[0] == ':') ? ':' : '?';
            }
        } else if (lo->has_arg == optional_argument) {
            if (name[name_len] == '=') {
                optarg = (char *)&name[name_len + 1];
            } else {
                optarg = NULL;
            }
            optind++;
        } else {
            /* no_argument */
            optarg = NULL;
            optind++;
        }

        if (lo->flag) {
            *(lo->flag) = lo->val;
            return 0;
        }
        return lo->val;
    }

    if (match_count == 0) {
        /* Unknown long option */
        if (opterr)
            fprintf(stderr, "%s: unrecognized option '--%s'\n", argv[0], name);
        optarg = NULL;
        optopt = 0;
        optind++;
        return '?';
    }

    /* Ambiguous */
    if (opterr)
        fprintf(stderr, "%s: option '--%s' is ambiguous\n", argv[0], name);
    optarg = NULL;
    optopt = 0;
    optind++;
    return '?';
}

#endif /* _WIN32 */
