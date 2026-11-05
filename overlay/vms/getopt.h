/* getopt.h - getopt_long() for OpenVMS (vms-rsync): the C RTL has getopt()
   only. vms/vms_getopt.c implements getopt_long() with the GNU and BSD
   behaviour openrsync expects: long options with = or a separate argument,
   unambiguous abbreviations, flag pointers, and operands moved after options
   (unless the short option string starts with '+'). optind, optarg, opterr
   and optopt are the C RTL's (<unistd.h>). */
#ifndef VMS_GETOPT_H
#define VMS_GETOPT_H
#include <unistd.h>

#define no_argument       0
#define required_argument 1
#define optional_argument 2

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

int getopt_long(int, char * const *, const char *, const struct option *, int *);
int getopt_long_only(int, char * const *, const char *, const struct option *, int *);
#endif
