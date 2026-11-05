/* syslog.h - what openrsync's log.c takes from <syslog.h>, for OpenVMS
   (vms-rsync). There is no syslog on VMS: a message log.c would send there
   goes to stderr instead (vms/vms_syslog.c). */
#ifndef VMS_SYSLOG_H
#define VMS_SYSLOG_H
#include <stdarg.h>
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7
#define LOG_DAEMON  (3 << 3)
#define LOG_PID     0x01
#define LOG_NDELAY  0x08
void openlog(const char *, int, int);
void syslog(int, const char *, ...);
void vsyslog(int, const char *, va_list);
void closelog(void);
#endif
