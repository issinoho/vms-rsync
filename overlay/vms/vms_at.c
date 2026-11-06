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
#include <ctype.h>
#include <rms.h>
#include <starlet.h>

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
static struct { int fd; char *path; int have; struct timespec ts[2]; char *copy_to; int renamed; } wf[MAXW];
static void track_write(int, const char *);
static void track_rename(const char *, const char *);
static void track_times(const char *, const struct timespec [2]);
static void restore_times(const char *);
static void remember_times(const char *, const struct timespec [2]);
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

/* ---- VMS text files (D3, NOTES N7) ----------------------------------------
   A text record file (variable-length or VFC records with carriage control) is
   presented as its LF lines: the bytes the C RTL's read() gives, one line per
   record. stat sizes say so (fstatat here; the sender's file list, patch 0012)
   and a read opens a delete-on-close stream copy of those bytes, so read,
   pread, mmap and fstat all agree. A received file whose type is a text type
   becomes variable-length records again when it is closed. */
#define FAB_C_VAR 2
#define FAB_C_VFC 3
#define FAB_M_FTN 1
#define FAB_M_CR  2
#define FAB_M_PRN 4

int vms_text_record(const struct stat *st)
{
    return S_ISREG(st->st_mode) && (st->st_fab_rfm == FAB_C_VAR || st->st_fab_rfm == FAB_C_VFC) &&
           (st->st_fab_rat & (FAB_M_FTN | FAB_M_CR | FAB_M_PRN));
}

/* LF sizes already counted, by file identity and date. */
#define MAXLF 512
static struct { dev_t dev; ino_t ino; time_t mtime; off_t raw, lf; } lfc[MAXLF];
static int lfc_next;

static off_t count_lf(const char *p)
{
    static char buf[32768];
    off_t n = 0;
    long r;
    int fd = open(p, O_RDONLY);
    if (fd < 0) return -1;
    while ((r = read(fd, buf, sizeof buf)) > 0) n += r;
    close(fd);
    return r < 0 ? -1 : n;
}

/* The size of path as rsync sees it: its LF size for a text record file. */
off_t vms_lf_size(const char *p, const struct stat *st)
{
    int i;
    off_t n;
    if (!vms_text_record(st)) return st->st_size;
    for (i = 0; i < MAXLF; i++)
        if (lfc[i].ino == st->st_ino && lfc[i].dev == st->st_dev && lfc[i].mtime == st->st_mtime &&
            lfc[i].raw == st->st_size)
            return lfc[i].lf;
    if ((n = count_lf(p)) < 0) return st->st_size;
    lfc[lfc_next].dev = st->st_dev; lfc[lfc_next].ino = st->st_ino;
    lfc[lfc_next].mtime = st->st_mtime; lfc[lfc_next].raw = st->st_size; lfc[lfc_next].lf = n;
    lfc_next = (lfc_next + 1) % MAXLF;
    if (trace > 0) fprintf(stderr, "vms_at: %s is text: %ld bytes as LF lines\n", p, (long) n);
    return n;
}

/* A delete-on-close stream file holding p's LF lines, with p's dates. */
static int lf_view(const char *p, const struct stat *st)
{
    static int seq;
    static char buf[32768];
    char tmp[64];
    struct timespec ts[2];
    long r;
    int in, out;
    sprintf(tmp, "SYS$SCRATCH:RSYNC_%08X_%d.TMP", (unsigned) getpid(), ++seq);
    if ((in = open(p, O_RDONLY)) < 0) return -1;
    if ((out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) { close(in); return -1; }
    while ((r = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, (size_t) r) != r) { r = -1; break; }
    close(in);
    /* closed and opened again: while a new file is open, its size in the
       header (fstat, mmap) is 0 until the close (NOTES N7) */
    if (close(out) == -1 || r < 0) { remove(tmp); return -1; }
    ts[0].tv_sec = st->st_atime; ts[0].tv_nsec = 0;
    ts[1].tv_sec = st->st_mtime; ts[1].tv_nsec = 0;
    settimes(tmp, ts);
    if ((out = open(tmp, O_RDONLY, 0, "fop=dlt")) < 0) { remove(tmp); return -1; }
    tr("text, read as LF lines", p, out);
    return out;
}

/* The text types (RSYNC$TEXT_TYPES, else VFTP's list): a received file of one
   of these types is written as variable-length records. */
static const char *default_types =
    ".TXT .C .H .CC .CPP .HPP .COM .MMS .MAR .FOR .F .F90 .PAS .BAS .COB .PL .PY .SH "
    ".HTML .HTM .CSS .JS .JSON .XML .YAML .YML .MD .RST .CSV .TSV .INI .CONF .CFG .LOG "
    ".LIS .MAP .OPT .SQL .DIFF .PATCH .MAK .AWK .SED .TEX .LIST .README .PEM .PUB";

static int text_type(const char *path)
{
    const char *base = strrchr(path, '/'), *dot, *list = getenv("RSYNC$TEXT_TYPES"), *q;
    size_t n;
    base = base ? base + 1 : path;
    if ((dot = strrchr(base, '.')) == NULL || dot == base || dot[1] == '\0') return 0;
    if (list == NULL) list = default_types;
    n = strlen(dot);
    for (q = list; (q = strchr(q, '.')) != NULL; q++) {
        size_t i;
        for (i = 0; i < n && q[i] && toupper((unsigned char) q[i]) == toupper((unsigned char) dot[i]); i++) ;
        if (i == n && (q[n] == '\0' || q[n] == ' ' || q[n] == ',')) return 1;
    }
    return 0;
}

/* Rewrite a just-received stream file of a text type as variable-length records
   (a new version; the stream version is deleted). Left as it is, and said so
   with the trace, if it does not end with a newline or has a line too long
   for a record: reading it back would not give the same bytes. */
static int to_records(const char *path)
{
    static char data[32768], line[32768];
    char old[PATH_MAX];
    struct stat st;
    long r, i, len = 0;
    int in, last_nl = 1, ok = 1;
    if (!text_type(path) || stat(path, &st) == -1 || !S_ISREG(st.st_mode) || vms_text_record(&st)) return 0;
    if ((in = open(path, O_RDONLY)) < 0) return -1;
    while ((r = read(in, data, sizeof data)) > 0)          /* pass 1: will it fit? */
        for (i = 0; i < r; i++) {
            if (data[i] == '\n') len = 0;
            else if (++len > 32767) ok = 0;
            last_nl = data[i] == '\n';
        }
    if (!ok || !last_nl || st.st_size == 0) {
        close(in);
        tr(ok ? "text type, kept as a stream (no final newline)" : "text type, kept as a stream (a line too long)", path, 0);
        return 0;
    }
    if (getname(in, old, 1) == NULL) { close(in); return -1; }
    lseek(in, 0, SEEK_SET);
    /* pass 2: one RMS $PUT per line. A zero-length write() makes no record
       (empty lines were lost), and stdio on a variable-length file wrote one
       record per character (NOTES N7). */
    {
        struct FAB fab = cc$rms_fab;
        struct RAB rab = cc$rms_rab;
        struct NAML naml = cc$rms_naml;
        char name[PATH_MAX], *semi;
        unsigned int sts;
        strcpy(name, old);
        if ((semi = strrchr(name, ';')) != NULL) *semi = '\0';   /* a new version */
        fab.fab$l_naml = &naml;
        fab.fab$l_fna = (char *) -1; fab.fab$b_fns = 0;
        naml.naml$l_long_filename = name;
        naml.naml$l_long_filename_size = strlen(name);
        fab.fab$b_rfm = FAB$C_VAR; fab.fab$b_rat = FAB$M_CR; fab.fab$b_fac = FAB$M_PUT;
        sts = sys$create(&fab);
        if (!(sts & 1)) { close(in); errno = EVMSERR; vaxc$errno = sts; return -1; }
        rab.rab$l_fab = &fab;
        if (!((sts = sys$connect(&rab)) & 1)) ok = 0;
        len = 0;
        while (ok && (r = read(in, data, sizeof data)) > 0)
            for (i = 0; i < r && ok; i++) {
                if (data[i] != '\n') { line[len++] = data[i]; continue; }
                rab.rab$l_rbf = line; rab.rab$w_rsz = (unsigned short) len;
                if (!((sts = sys$put(&rab)) & 1)) ok = 0;
                len = 0;
            }
        close(in);
        sys$disconnect(&rab);
        if (!(sys$close(&fab) & 1)) ok = 0;
        if (!ok) { tr("to records failed", path, -1); return -1; }
    }
    (void) chmod(path, st.st_mode & 07777);
    remove(old);                                           /* the stream version */
    tr("written as records", path, 0);
    return 0;
}

static int openpath(const char *p, int flags, mode_t mode)
{
    struct stat st;
    int fd;
    if (flags & O_DIRECTORY) return opendirfd(p);
    if ((flags & O_NOFOLLOW) && lstat(p, &st) == 0 && S_ISLNK(st.st_mode)) { errno = ELOOP; return -1; }
    if (!(flags & (O_CREAT | O_WRONLY | O_RDWR)) && stat(p, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return opendirfd(p);
        if (vms_text_record(&st)) return lf_view(p, &st);
    }
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
    int rc;
    if (p == NULL) return -1;
    rc = (flag & AT_SYMLINK_NOFOLLOW) ? lstat(p, st) : stat(p, st);
    if (rc == 0 && vms_text_record(st)) st->st_size = vms_lf_size(p, st);
    return rc;
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

/* No hard links on VMS (D6): a link is made as a copy, with the source's
   protection and times, so --link-dest gives what --copy-dest would, and -H
   copies (NOTES N6). */
int linkat(int fdfd, const char *from, int tdfd, const char *to, int flag)
{
    static char buf[32768];
    char b1[PATH_MAX], b2[PATH_MAX];
    const char *p1 = join(fdfd, from, b1, sizeof b1), *p2 = join(tdfd, to, b2, sizeof b2);
    struct stat st;
    struct timespec ts[2];
    long n;
    int in, out, rc = 0;
    (void) flag;
    if (!p1 || !p2) return -1;
    if (stat(p1, &st) == -1) return -1;
    if ((in = open(p1, O_RDONLY)) == -1) return -1;
    if ((out = open(p2, O_WRONLY | O_CREAT | O_EXCL, st.st_mode & 07777)) == -1) { close(in); return -1; }
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, (size_t) n) != n) { rc = -1; break; }
    if (n < 0) rc = -1;
    close(in);
    if (close(out) == -1) rc = -1;
    if (rc == 0) {
        ts[0].tv_sec = st.st_atime; ts[0].tv_nsec = 0;
        ts[1].tv_sec = st.st_mtime; ts[1].tv_nsec = 0;
        (void) chmod(p2, st.st_mode & 07777);
        settimes(p2, ts);                         /* after chmod: it sets the date */
        remember_times(p2, ts);
    }
    tr("link by copy", p2, rc);
    return rc;
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
            if ((wf[i].path = strdup(p)) != NULL) { wf[i].fd = fd; wf[i].have = 0; wf[i].copy_to = NULL; wf[i].renamed = 0; }
            return;
        }
}

static void track_rename(const char *from, const char *to)
{
    int i;
    for (i = 0; i < MAXW; i++)
        if (wf[i].path && !strcmp(wf[i].path, from)) {
            char *n = strdup(to);
            if (n) { free(wf[i].path); wf[i].path = n; wf[i].renamed = 1; }
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
            } else if (wf[i].renamed && to_records(now) == -1) { rc = -1; e = errno; }
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
