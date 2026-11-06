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
#undef open                         /* from here on, the C RTL's open() and close() */
#undef close

/* RSYNC_VMS_TRACE (a logical name or environment variable) traces every
   call here to stderr, with its result. */
static int trace = -1;
static void tr(const char *fn, const char *p, long rc)
{
    if (trace < 0) trace = getenv("RSYNC_VMS_TRACE") != NULL;
    if (trace) fprintf(stderr, "vms_at: %s(%s) = %ld%s%s\n", fn, p ? p : "-", rc,
                       rc < 0 ? " " : "", rc < 0 ? strerror(errno) : "");
}

/* Files opened for writing (see vms_close()). */
#define MAXW 64
static struct { int fd; char *path; int have; struct timespec ts[2]; char *copy_to; } wf[MAXW];
static void track_write(int, const char *);
static void track_rename(const char *, const char *);
static void track_times(const char *, const struct timespec [2]);
static void restore_times(const char *);
static int settimes(const char *, const struct timespec [2]);

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
    int fd;
    if (flags & O_DIRECTORY) return opendirfd(p);
    if ((flags & O_NOFOLLOW) && lstat(p, &st) == 0 && S_ISLNK(st.st_mode)) { errno = ELOOP; return -1; }
    if (!(flags & (O_CREAT | O_WRONLY | O_RDWR)) && stat(p, &st) == 0 && S_ISDIR(st.st_mode))
        return opendirfd(p);
    fd = open(p, flags & ~(O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC), mode);
    tr("open", p, fd);
    if (fd >= 0 && (flags & (O_WRONLY | O_RDWR | O_CREAT))) track_write(fd, p);
    return fd;
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

/* Copy a file's bytes from one name to another (the target a new file or a
   new version), keeping the source's protection; then delete the source. */
static int copy_then_delete(const char *from, const char *to)
{
    static char buf[32768];
    struct stat st;
    long n;
    int in, out, rc = 0;
    if (stat(from, &st) == -1) return -1;
    if ((in = open(from, O_RDONLY)) == -1) return -1;
    if ((out = open(to, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777)) == -1) { close(in); return -1; }
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, (size_t) n) != n) { rc = -1; break; }
    if (n < 0) rc = -1;
    close(in);
    if (close(out) == -1) rc = -1;
    if (rc == 0) rc = unlink(from);
    return rc;
}

/* rename() on VMS fills a field missing from the new name from the old one
   (RMS defaults, NOTES N6): "README" from ".README.XXXXXX" would become
   "README.XXXXXX", and ".hidden" from "..hidden.XXXXXX" "..hidden.hidden".
   A name without a type gets an explicit empty one ("README."); a name with
   no name part (".hidden") cannot be said so that the old name is not used,
   so the file is copied there instead. */
int renameat(int fdfd, const char *from, int tdfd, const char *to)
{
    char b1[PATH_MAX], b2[PATH_MAX], b3[PATH_MAX];
    const char *p1 = join(fdfd, from, b1, sizeof b1), *p2 = join(tdfd, to, b2, sizeof b2), *base;
    int rc;
    if (!p1 || !p2) return -1;
    base = strrchr(p2, '/');
    base = base ? base + 1 : p2;
    if (base[0] == '.' && base[1] != '\0' && strcmp(base, "..") && strchr(base + 1, '.') == NULL) {
        /* openrsync renames its temporary file before it closes it, and an
           open file cannot be deleted (or reliably copied): if it is open
           here, the copy is done by vms_close(). */
        int i;
        for (i = 0; i < MAXW; i++)
            if (wf[i].path && !strcmp(wf[i].path, p1)) {
                free(wf[i].copy_to);
                if ((wf[i].copy_to = strdup(p2)) == NULL) { errno = ENOMEM; return -1; }
                tr("rename by copy, at close", p2, 0);
                return 0;
            }
        rc = copy_then_delete(p1, p2);
        tr("rename by copy", p2, rc);
    } else {
        if (base[0] != '\0' && strchr(base, '.') == NULL && strlen(p2) + 2 <= sizeof b3) {
            sprintf(b3, "%s.", p2);
            rc = rename(p1, b3);
        } else rc = rename(p1, p2);
        tr("rename", p2, rc);
    }
    if (rc == 0) track_rename(p1, p2);
    return rc;
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
    int rc;
    (void) flag;
    if (p == NULL) return -1;
    rc = chmod(p, mode);
    tr("chmod", p, rc);
    restore_times(p);
    return rc;
}

int fchownat(int dfd, const char *name, uid_t uid, gid_t gid, int flag)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    int rc;
    (void) flag;
    if (p == NULL) return -1;
    rc = chown(p, uid, gid);
    tr("chown", p, rc);
    restore_times(p);
    return rc;
}

/* times[0] access, times[1] modification; UTIME_NOW and UTIME_OMIT as POSIX. */
static int settimes(const char *p, const struct timespec ts[2])
{
    struct utimbuf ub;
    struct stat st;
    time_t now = time(NULL);
    int rc;
    if (ts == NULL) { rc = utime(p, NULL); tr("utime(now)", p, rc); return rc; }
    if (stat(p, &st) == -1) { tr("settimes stat", p, -1); return -1; }
    ub.actime = ts[0].tv_nsec == UTIME_OMIT ? st.st_atime : ts[0].tv_nsec == UTIME_NOW ? now : ts[0].tv_sec;
    ub.modtime = ts[1].tv_nsec == UTIME_OMIT ? st.st_mtime : ts[1].tv_nsec == UTIME_NOW ? now : ts[1].tv_sec;
    rc = utime(p, &ub);
    if (trace > 0) fprintf(stderr, "vms_at: utime(%s, mtime %ld) = %d; stat now says %ld\n", p, (long) ub.modtime, rc,
                           stat(p, &st) == 0 ? (long) st.st_mtime : -1L);
    return rc;
}

int utimensat(int dfd, const char *name, const struct timespec ts[2], int flag)
{
    char buf[PATH_MAX];
    const char *p = join(dfd, name, buf, sizeof buf);
    (void) flag;
    tr("utimensat", p ? p : name, p ? 0 : -1);
    if (p == NULL) return -1;
    track_times(p, ts);
    return settimes(p, ts);
}

/* Times and closing. On VMS, closing a file that was written sets its
   revision date, undoing any time set while it was open, and openrsync sets
   times by name (utimensat) or fd (futimens) before it closes the file
   (NOTES N5). So every file opened for writing is tracked by fd with its
   current name (renames followed); the times last set on that name are set
   again by vms_close() after the real close. */

static void track_write(int fd, const char *p)
{
    int i;
    for (i = 0; i < MAXW; i++)
        if (wf[i].path && wf[i].fd == fd) { free(wf[i].path); wf[i].path = NULL; }
    for (i = 0; i < MAXW; i++)
        if (!wf[i].path) {
            if ((wf[i].path = strdup(p)) != NULL) { wf[i].fd = fd; wf[i].have = 0; wf[i].copy_to = NULL; }
            return;
        }
}

static void track_rename(const char *from, const char *to)
{
    int i;
    for (i = 0; i < MAXW; i++)
        if (wf[i].path && !strcmp(wf[i].path, from)) {
            char *n = strdup(to);
            if (n) { free(wf[i].path); wf[i].path = n; }
        }
}

/* The times last set on each path: chmod() and chown() rewrite the file
   header and so set the revision date too (NOTES N5); openrsync sets the
   times first, then the owner and mode, so they are set again after. */
#define MAXT 32
static struct { char *path; struct timespec ts[2]; } lt[MAXT];
static int lt_next;

static void remember_times(const char *p, const struct timespec ts[2])
{
    int i;
    if (ts == NULL) return;
    for (i = 0; i < MAXT; i++)
        if (lt[i].path && !strcmp(lt[i].path, p)) { lt[i].ts[0] = ts[0]; lt[i].ts[1] = ts[1]; return; }
    free(lt[lt_next].path);
    if ((lt[lt_next].path = strdup(p)) != NULL) { lt[lt_next].ts[0] = ts[0]; lt[lt_next].ts[1] = ts[1]; }
    lt_next = (lt_next + 1) % MAXT;
}

static void restore_times(const char *p)
{
    int i;
    for (i = 0; i < MAXT; i++)
        if (lt[i].path && !strcmp(lt[i].path, p)) {
            if (trace > 0) fprintf(stderr, "vms_at: times set again on %s\n", p);
            settimes(p, lt[i].ts);
        }
}

static void track_times(const char *p, const struct timespec ts[2])
{
    int i;
    remember_times(p, ts);
    for (i = 0; i < MAXW; i++)
        if (wf[i].path && !strcmp(wf[i].path, p) && ts) {
            wf[i].have = 1; wf[i].ts[0] = ts[0]; wf[i].ts[1] = ts[1];
        }
}

int futimens(int fd, const struct timespec ts[2])
{
    int i;
    for (i = 0; i < MAXW; i++)
        if (wf[i].path && wf[i].fd == fd) {
            track_times(wf[i].path, ts);
            return settimes(wf[i].path, ts);
        }
    {   /* not opened through the shim: by its name now */
        char name[PATH_MAX];
        if (getname(fd, name, 1) == NULL) return -1;
        return settimes(name, ts);
    }
}

int vms_close(int fd)
{
    int i, rc = close(fd), e = errno;
    if (trace > 0) {
        const char *what = "-";
        for (i = 0; i < MAXW; i++) if (wf[i].path && wf[i].fd == fd) what = wf[i].path;
        fprintf(stderr, "vms_at: close(%d) %s = %d\n", fd, what, rc);
    }
    for (i = 0; i < MAXW; i++)
        if (wf[i].path && wf[i].fd == fd) {
            const char *now = wf[i].path;
            if (wf[i].copy_to) {                   /* a rename to ".name" */
                int r = copy_then_delete(wf[i].path, wf[i].copy_to);
                tr("close: copied to", wf[i].copy_to, r);
                if (r == -1) { rc = -1; e = errno; }
                now = wf[i].copy_to;
            }
            if (wf[i].have) {
                if (trace > 0) fprintf(stderr, "vms_at: close(%d) sets the times again\n", fd);
                settimes(now, wf[i].ts);
            }
            free(wf[i].path);
            free(wf[i].copy_to);
            wf[i].path = NULL;
            wf[i].copy_to = NULL;
        }
    forget(fd);
    errno = e;
    return rc;
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
