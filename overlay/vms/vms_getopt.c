/* vms_getopt.c - getopt_long() for OpenVMS (vms-rsync); see getopt.h.

   At the start of a scan (optind 1) the operands are moved after the options,
   as GNU and BSD getopt_long do, unless optstring begins with '+' or "--"
   ends the options. The scan itself then never permutes. optind, optarg,
   opterr and optopt are the C RTL's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "getopt.h"

static int sp = 1;                       /* position inside a cluster of short options */

static const char *shorts(const char *os) { return (*os == '+' || *os == '-') ? os + 1 : os; }

/* Does short option c take an argument? 0 no, 1 required, 2 optional, -1 unknown. */
static int short_arg(const char *os, int c)
{
    const char *p;
    if (c == ':' || (p = strchr(shorts(os), c)) == NULL) return -1;
    if (p[1] != ':') return 0;
    return p[2] == ':' ? 2 : 1;
}

/* The long option for "name" or "name=value": exact, else a unique prefix.
   Returns its index, -1 if none, -2 if ambiguous. */
static int find_long(const char *arg, const struct option *lo)
{
    size_t n = strcspn(arg, "=");
    int i, hit = -1;
    for (i = 0; lo[i].name; i++) {
        if (strncmp(lo[i].name, arg, n)) continue;
        if (strlen(lo[i].name) == n) return i;
        if (hit >= 0) return -2;
        hit = i;
    }
    return hit;
}

/* Move the operands after the options, keeping both in order. */
static void permute(int argc, char **argv, const char *os, const struct option *lo)
{
    char **opts, **ops;
    int i, no = 0, nr = 0, k, rest = argc;
    if (*os == '+' || getenv("POSIXLY_CORRECT")) return;
    opts = malloc(sizeof *opts * (size_t) argc);
    ops = malloc(sizeof *ops * (size_t) argc);
    if (!opts || !ops) { free(opts); free(ops); return; }
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--")) { rest = i; break; }
        if (a[0] != '-' || a[1] == '\0') { ops[nr++] = argv[i]; continue; }
        opts[no++] = argv[i];
        if (a[1] == '-') {                                /* --name, --name=value */
            int li = lo ? find_long(a + 2, lo) : -1;
            if (li >= 0 && lo[li].has_arg == required_argument && !strchr(a, '=') && i + 1 < argc)
                opts[no++] = argv[++i];
        } else {                                          /* -abc, -fvalue, -f value */
            for (k = 1; a[k]; k++) {
                int t = short_arg(os, a[k]);
                if (t == 1 || t == 2) {
                    if (a[k + 1] == '\0' && t == 1 && i + 1 < argc) opts[no++] = argv[++i];
                    break;
                }
            }
        }
    }
    k = 1;
    for (i = 0; i < no; i++) argv[k++] = opts[i];
    if (rest < argc) argv[k++] = argv[rest];              /* "--" stays between them */
    for (i = 0; i < nr; i++) argv[k++] = ops[i];
    free(opts); free(ops);
}

static int scan(int argc, char * const *argv, const char *os, const struct option *lo, int *li, int only)
{
    const char *a;
    int quiet = *shorts(os) == ':';
    if (optind == 0) optind = 1;
    if (optind == 1 && sp == 1) permute(argc, (char **) argv, os, lo);
    optarg = NULL;
    if (sp == 1) {
        if (optind >= argc) return -1;
        a = argv[optind];
        if (a[0] != '-' || a[1] == '\0') return -1;
        if (!strcmp(a, "--")) { optind++; return -1; }
        if (lo && (a[1] == '-' || (only && a[2] != '\0'))) {   /* a long option */
            const char *name = a + (a[1] == '-' ? 2 : 1), *eq = strchr(name, '=');
            int i = find_long(name, lo);
            if (i == -2) {
                if (opterr) fprintf(stderr, "%s: option '%s' is ambiguous\n", argv[0], a);
                optind++; optopt = 0; return '?';
            }
            if (i >= 0) {
                optind++;
                if (eq) {
                    if (lo[i].has_arg == no_argument) {
                        if (opterr) fprintf(stderr, "%s: option '--%s' takes no argument\n", argv[0], lo[i].name);
                        optopt = lo[i].val; return '?';
                    }
                    optarg = (char *) eq + 1;
                } else if (lo[i].has_arg == required_argument) {
                    if (optind >= argc) {
                        if (opterr && !quiet) fprintf(stderr, "%s: option '--%s' requires an argument\n", argv[0], lo[i].name);
                        optopt = lo[i].val; return quiet ? ':' : '?';
                    }
                    optarg = argv[optind++];
                }
                if (li) *li = i;
                if (lo[i].flag) { *lo[i].flag = lo[i].val; return 0; }
                return lo[i].val;
            }
            if (a[1] == '-') {
                if (opterr) fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], a);
                optind++; optopt = 0; return '?';
            }
        }
    }
    {                                                      /* a short option */
        int c = (unsigned char) argv[optind][sp], t = short_arg(os, c);
        a = argv[optind];
        optopt = c;
        if (t < 0) {
            if (opterr && !quiet) fprintf(stderr, "%s: unknown option -- %c\n", argv[0], c);
            if (a[++sp] == '\0') { optind++; sp = 1; }
            return '?';
        }
        if (t == 0) {
            if (a[++sp] == '\0') { optind++; sp = 1; }
            return c;
        }
        if (a[sp + 1] != '\0') optarg = (char *) a + sp + 1;
        else if (t == 1) {
            if (optind + 1 >= argc) {
                if (opterr && !quiet) fprintf(stderr, "%s: option requires an argument -- %c\n", argv[0], c);
                optind++; sp = 1;
                return quiet ? ':' : '?';
            }
            optarg = argv[++optind];
        }
        optind++; sp = 1;
        return c;
    }
}

int getopt_long(int argc, char * const *argv, const char *os, const struct option *lo, int *li)
{
    return scan(argc, argv, os, lo, li, 0);
}

int getopt_long_only(int argc, char * const *argv, const char *os, const struct option *lo, int *li)
{
    return scan(argc, argv, os, lo, li, 1);
}
