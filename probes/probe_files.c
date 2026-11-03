/* probe_files.c - Phase 0 probes 4-7 and 10, run in an empty scratch directory
   (the driver makes it): what an rsync receiver does to files, through the C RTL
   with the feature logical names the driver defines.
     4  mkstemp(".name.XXXXXX") in the directory, then rename() over an existing
        name: a new version, or the old one replaced?
     5  utime() sets the modification (revision) date; stat reads it back
     6  chmod(0640) and stat's st_mode
     7  readdir: versions, directories, a file with no type, mixed case
    10  symlink() / readlink() / lstat */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

static void put(const char *name, const char *text)
{
    FILE *f = fopen(name, "w");
    if (!f) { printf("  fopen %s: %s\n", name, strerror(errno)); return; }
    fputs(text, f);
    fclose(f);
}

static void show(const char *name)
{
    char buf[100];
    FILE *f = fopen(name, "r");
    size_t n;
    if (!f) { printf("  %-14s cannot open: %s\n", name, strerror(errno)); return; }
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    if (n && buf[n - 1] == '\n') buf[n - 1] = 0;
    fclose(f);
    printf("  %-14s \"%s\"\n", name, buf);
}

int main(void)
{
    char tmpl[64], link[256];
    int fd;
    struct stat st;
    struct utimbuf ut;
    DIR *d;
    struct dirent *e;
    long n;

    printf("P4: mkstemp + rename over an existing file\n");
    put("dest.txt", "old contents\n");
    strcpy(tmpl, ".dest.txt.XXXXXX");
    fd = mkstemp(tmpl);
    printf("  mkstemp -> %d, name \"%s\" (%s)\n", fd, tmpl, fd < 0 ? strerror(errno) : "ok");
    if (fd >= 0) {
        write(fd, "new contents\n", 13);
        close(fd);
        printf("  rename(\"%s\", \"dest.txt\") -> %d %s\n", tmpl,
               rename(tmpl, "dest.txt"), strerror(errno));
    }
    show("dest.txt"); show("dest.txt;1"); show("dest.txt;2"); show("dest.txt;-1");

    printf("P5: utime\n");
    put("dated.txt", "x\n");
    ut.actime = ut.modtime = 1577934245;          /* 2020-01-02 03:04:05 UTC */
    printf("  utime -> %d %s\n", utime("dated.txt", &ut), strerror(errno));
    if (!stat("dated.txt", &st))
        printf("  st_mtime %ld (want 1577934245) %s; st_ctime %ld\n", (long) st.st_mtime,
               (long) st.st_mtime == 1577934245L ? "SAME" : "DIFFERENT", (long) st.st_ctime);

    printf("P6: chmod\n");
    put("perm.txt", "x\n");
    if (!stat("perm.txt", &st)) printf("  before: mode %o\n", (unsigned) st.st_mode & 07777);
    printf("  chmod 0640 -> %d %s\n", chmod("perm.txt", 0640), strerror(errno));
    if (!stat("perm.txt", &st)) printf("  after:  mode %o (want 640)\n", (unsigned) st.st_mode & 07777);
    printf("  chmod 0755 -> %d\n", chmod("perm.txt", 0755));
    if (!stat("perm.txt", &st)) printf("  after:  mode %o (want 755)\n", (unsigned) st.st_mode & 07777);

    printf("P7: readdir\n");
    put("README", "no type\n");
    put("Mixed.Case.txt", "mixed\n");
    put("two.txt", "v1\n"); put("two.txt", "v2\n");
    mkdir("subdir", 0755);
    d = opendir(".");
    if (!d) printf("  opendir: %s\n", strerror(errno));
    else {
        while ((e = readdir(d)) != 0) {
            int isdir = !stat(e->d_name, &st) && S_ISDIR(st.st_mode);
            printf("  [%s]%s size %ld\n", e->d_name, isdir ? " DIR" : "", isdir ? 0L : (long) st.st_size);
        }
        closedir(d);
    }

    printf("P10: symlink\n");
    put("target.txt", "target\n");
    printf("  symlink -> %d %s\n", symlink("target.txt", "link.txt"), strerror(errno));
    n = readlink("link.txt", link, sizeof link - 1);
    if (n >= 0) link[n] = 0;
    printf("  readlink -> %ld \"%s\" %s\n", n, n >= 0 ? link : "", n < 0 ? strerror(errno) : "");
    if (!lstat("link.txt", &st)) printf("  lstat: %s\n", S_ISLNK(st.st_mode) ? "a link" : "NOT a link");
    show("link.txt");
    return 1;
}
