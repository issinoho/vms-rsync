/* vms_config.h - OpenVMS definitions for openrsync (vms-rsync), included at the
   end of config.h (tools/prepare.sh), so before every system header in the
   sources that include config.h first.

   The C RTL has no *at() functions and cannot open() a directory (NOTES N4).
   vms/vms_at.c supplies the *at() family over plain paths: a directory "fd" is
   a real fd (the null device) registered with the directory's path. open() and
   openat() with O_DIRECTORY, or on a directory, return one. */
#ifndef VMS_CONFIG_H
#define VMS_CONFIG_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

/* x86-64 and IA64 are little-endian: BYTE_ORDER and le32toh() and friends. */
#include "endian.h"

/* rsync is single-threaded; VSI C has no thread-local storage class. */
#define __thread

/* VSI C's PATH_MAX is 256; ODS-5 specifications reach 4095 bytes. */
#undef PATH_MAX
#define PATH_MAX 4096

/* Flags the C RTL lacks: bits it does not use, removed before the real open(). */
#ifndef O_DIRECTORY
#define O_DIRECTORY 0x01000000
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW  0x02000000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC   0x04000000
#endif

#define AT_FDCWD            (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR        0x200
#define AT_EACCESS          0x400
#define AT_SYMLINK_FOLLOW   0x800
#ifndef UTIME_NOW
#define UTIME_NOW  ((1L << 30) - 1L)
#define UTIME_OMIT ((1L << 30) - 2L)
#endif

int vms_open(const char *, int, ...);
int openat(int, const char *, int, ...);
int fstatat(int, const char *, struct stat *, int);
int mkdirat(int, const char *, mode_t);
int unlinkat(int, const char *, int);
int renameat(int, const char *, int, const char *);
int symlinkat(const char *, int, const char *);
ssize_t readlinkat(int, const char *, char *, size_t);
int linkat(int, const char *, int, const char *, int);
int fchmodat(int, const char *, mode_t, int);
int fchownat(int, const char *, uid_t, gid_t, int);
int utimensat(int, const char *, const struct timespec [2], int);
int futimens(int, const struct timespec [2]);
int faccessat(int, const char *, int, int);
int mknodat(int, const char *, mode_t, dev_t);
int mkfifoat(int, const char *, mode_t);
int fchdir(int);

/* open() goes through the shim, for O_DIRECTORY and directories (vms_at.c
   calls the C RTL's own). The renamed prototype in <fcntl.h> matches ours.
   close() too: it sets times futimens() was given once the file is closed
   (closing a written file sets its revision date), and forgets directory fds. */
int vms_close(int);
#define open vms_open
#define close vms_close

/* Only compats.c's bcrypt round tuning asks for a thread CPU clock, which rsync
   never calls. */
#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID CLOCK_REALTIME
#endif

/* The compat fts walks by full path (FTS_NOCHDIR, patch 0001); given -1 for a
   directory's fd, fts_stat() stats that full path from the current directory. */
#define dirfd(d) (-1)

/* The C RTL's <termios.h> lists these as not implemented; vms/vms_tty.c
   reports "not a terminal", so compats.c's readpassphrase() cannot turn echo off
   (a daemon password would echo: to be done in Phase 2). */
struct termios;
int tcgetattr(int, struct termios *);
int tcsetattr(int, int, const struct termios *);

/* BSD <sys/param.h> and <sys/time.h> conveniences the C RTL lacks. */
#ifndef MAXPATHLEN
#define MAXPATHLEN PATH_MAX
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef roundup
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef timersub
#define timersub(a, b, r) do { \
        (r)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
        (r)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
        if ((r)->tv_usec < 0) { (r)->tv_sec--; (r)->tv_usec += 1000000; } \
    } while (0)
#endif
/* Device numbers, for the file list's devices, which rsync refuses on VMS (D6). */
#ifndef major
#define major(d) (((unsigned long) (d) >> 8) & 0xff)
#define minor(d) ((unsigned long) (d) & 0xff)
#define makedev(ma, mi) ((dev_t) ((((ma) & 0xff) << 8) | ((mi) & 0xff)))
#endif
/* <dirent.h>'s file type from a mode, as BSD: only ever compared with itself. */
#ifndef IFTODT
#define IFTODT(mode) (((mode) & 0170000) >> 12)
#endif

/* VMS text files (D3): a text record file's size as LF lines (vms_at.c;
   flist_copy_stat, patch 0012). */
int vms_text_record(const struct stat *);
off_t vms_lf_size(const char *, const struct stat *);

/* The child process for the remote shell or a local --server (patch 0002). */
int vms_spawn(char *const *, int, const char *);
int vms_child_status(int, char *const *);

#endif
