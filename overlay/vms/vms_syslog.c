/* vms_syslog.c - syslog(3) for OpenVMS (vms-rsync): there is none, so a
   message goes to stderr with the program's name, as rsync's own do. */
#include <stdarg.h>
#include <stdio.h>
#include "syslog.h"

static const char *ident = "rsync";

void openlog(const char *id, int opt, int fac) { (void) opt; (void) fac; if (id) ident = id; }
void closelog(void) { }

void vsyslog(int pri, const char *fmt, va_list ap)
{
    (void) pri;
    fprintf(stderr, "%s: ", ident);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void syslog(int pri, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsyslog(pri, fmt, ap);
    va_end(ap);
}
