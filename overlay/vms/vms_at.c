/* vms_at.c - the *at() family for OpenVMS, over plain paths (vms-rsync, N4).

   The C RTL has none of openat(), fstatat() and the rest, and cannot open() a
   directory. Here a directory "fd" is a real descriptor (the null device, so it
   is a valid fd for close() and poll()) registered with the directory's path;
   each *at() call joins that path with its name and calls the path function.
   open() and openat() return such an fd for O_DIRECTORY or a directory.

   This file does not include config.h: it must call the C RTL's own open(). */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <unixio.h>

#include "vms_config.h"
#undef open                         /* from here on, the C RTL's open() */

#define MAXDIRS 256
static struct { int fd; char *path; } dirs[MAXDIRS];

static const char *dirpath(int fd)
{
    int i;
    for (i = 0; i < MAXDIRS; i++)
        if (dirs[i].path && dirs[i].fd == fd) return dirs[i].path;
    return NULL;
}

static void forget(int fd)
{
    int i;
    for (i = 0; i < MAXDIRS; i++)
        if (dirs[i].path && dirs[i].fd == fd) { free(dirs[i].path); dirs[i].path = NULL; }
}

/* A directory fd for path, which must be a directory. Relative paths are made
   absolute now, so a later chdir() cannot change what the fd means. */
static int opendirfd(const char *path)
{
    struct stat st;
    char cwd[PATH_MAX], *full;
    int fd, i;
    if (stat(path, &st) == -1) return -1;
    if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
    if (path[0] == '/') full = strdup(path);
    else {
        if (getcwd(cwd, sizeof cwd) == NULL) return -1;   /* Unix syntax (vms_crtl_init.c) */
        full = malloc(strlen(cwd) + strlen(path) + 2);
        if (full) sprintf(full, "%s/%s", strcmp(cwd, "/") ? cwd : "", path);
    }
    if (full == NULL) { errno = ENOMEM; return -1; }
    if ((fd = open("NLA0:", O_RDONLY)) == -1) { free(full); return -1; }
    forget(fd);                                  /* a stale entry for a reused number */
    for (i = 0; i < MAXDIRS; i++)
        if (dirs[i].path == NULL) { dirs[i].fd = fd; dirs[i].path = full; return fd; }
    close(fd); free(full);
    errno = EMFILE;
    return -1;
}

/* dfd + name -> a path in buf; NULL with errno set if dfd is not a directory fd. */
static const char *join(int dfd, const char *name, char *buf, size_t sz)
{
    const char *d;
    if (name[0] == '/' || dfd == AT_FDCWD) return name;
    if ((d = dirpath(dfd)) == NULL) { errno = EBADF; return NULL; }
    if (name[0] == '\0' || !strcmp(name, ".")) return d;
    if (snprintf(buf, sz, "%s/%s", d, name) >= (int) sz) { errno = ENAMETOOLONG; return NULL; }
    return buf;
}

static int openpath(const char *p, int flags, mode_t mode)
{
    struct stat st;
    if (flags & O_DIRECTORY) return opendirfd(p);
    if ((flags & O_NOFOLLOW) && lstat(p, &st) == 0 && S_ISLNK(st.st_mode)) { errno = ELOOP; return -1; }
    if (!(flags & (O_CREAT | O_WRONLY | O_RDWR)) && stat(p, &st) == 0 && S_ISDIR(st.st_mode))
        return opendirfd(p);
    return open(p, flags & ~(O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC), mode);
}

int vms_open(const char *path, int flags, ...)
{
    va_list ap;
    mode_t mode = 0;
    if (flags & O_CREAT) { va_start(ap, flags); mode = (mode_t) va_arg(ap, int); va_end(ap); }
    return openpath(path, flags, mode);
}

int openat(int dfd, const char *name, int flags, ...)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    va_list ap;
    mode_t mode = 0;
    if (p == NULL) return -1;
    if (flags & O_CREAT) { va_start(ap, flags); mode = (mode_t) va_arg(ap, int); va_end(ap); }
    return openpath(p, flags, mode);
}

int fstatat(int dfd, const char *name, struct stat *st, int flag)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    if (p == NULL) return -1;
    return (flag & AT_SYMLINK_NOFOLLOW) ? lstat(p, st) : stat(p, st);
}

int mkdirat(int dfd, const char *name, mode_t mode)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    return p ? mkdir(p, mode) : -1;
}

int unlinkat(int dfd, const char *name, int flag)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    if (p == NULL) return -1;
    return (flag & AT_REMOVEDIR) ? rmdir(p) : unlink(p);
}

int renameat(int fdfd, const char *from, int tdfd, const char *to)
{
    char b1[PATH_MAX], b2[PATH_MAX];
    const char *p1 = join(fdfd, from, b1, sizeof b1), *p2 = join(tdfd, to, b2, sizeof b2);
    return p1 && p2 ? rename(p1, p2) : -1;
}

int symlinkat(const char *target, int dfd, const char *name)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    return p ? symlink(target, p) : -1;
}

ssize_t readlinkat(int dfd, const char *name, char *out, size_t sz)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    return p ? readlink(p, out, sz) : -1;
}

int linkat(int fdfd, const char *from, int tdfd, const char *to, int flag)
{
    (void) fdfd; (void) from; (void) tdfd; (void) to; (void) flag;
    errno = ENOTSUP;                              /* no hard links (D6) */
    return -1;
}

int fchmodat(int dfd, const char *name, mode_t mode, int flag)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    (void) flag;
    return p ? chmod(p, mode) : -1;
}

int fchownat(int dfd, const char *name, uid_t uid, gid_t gid, int flag)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    (void) flag;
    return p ? chown(p, uid, gid) : -1;
}

/* times[0] access, times[1] modification; UTIME_NOW and UTIME_OMIT as POSIX. */
static int settimes(const char *p, const struct timespec ts[2])
{
    struct utimbuf ub;
    struct stat st;
    time_t now = time(NULL);
    if (ts == NULL) return utime(p, NULL);
    if (stat(p, &st) == -1) return -1;
    ub.actime = ts[0].tv_nsec == UTIME_OMIT ? st.st_atime : ts[0].tv_nsec == UTIME_NOW ? now : ts[0].tv_sec;
    ub.modtime = ts[1].tv_nsec == UTIME_OMIT ? st.st_mtime : ts[1].tv_nsec == UTIME_NOW ? now : ts[1].tv_sec;
    return utime(p, &ub);
}

int utimensat(int dfd, const char *name, const struct timespec ts[2], int flag)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    (void) flag;
    return p ? settimes(p, ts) : -1;
}

/* An open file's times, by its name (getname(), in Unix syntax). */
int futimens(int fd, const struct timespec ts[2])
{
    char name[PATH_MAX];
    if (getname(fd, name, 1) == NULL) return -1;
    return settimes(name, ts);
}

int faccessat(int dfd, const char *name, int mode, int flag)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    (void) flag;
    return p ? access(p, mode) : -1;
}

int mknodat(int dfd, const char *name, mode_t mode, dev_t dev)
{
    (void) dfd; (void) name; (void) mode; (void) dev;
    errno = ENOTSUP;                              /* no devices or specials (D6) */
    return -1;
}

int mkfifoat(int dfd, const char *name, mode_t mode)
{
    (void) dfd; (void) name; (void) mode;
    errno = ENOTSUP;
    return -1;
}

int fchdir(int fd)
{
    const char *d = dirpath(fd);
    if (d == NULL) { errno = EBADF; return -1; }
    return chdir(d);
}
