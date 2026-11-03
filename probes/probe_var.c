/* probe_var.c - Phase 0 probe 8: what sizing a VMS text file costs.
   Makes a variable-length record file (rat=cr) of N records and M small ones,
   then: stat's st_size against the LF length read through the C RTL (the
   bytes rsync would send for it), the read speed, and the time to LF-size the
   M small files (an rsync quick-check over a tree). Compiled without
   /STANDARD=C99 so creat() takes RMS keywords. Usage: probe_var [records] [files] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void make(const char *name, long recs)
{
    char line[120];
    long i;
    int fd = creat(name, 0644, "rfm=var", "rat=cr");
    for (i = 0; i < recs; i++) {
        int n = sprintf(line, "line %ld of a VMS text file, some ordinary words to fill it", i);
        write(fd, line, n);                     /* one write = one record */
    }
    close(fd);
}

static long lfsize(const char *name)
{
    static char buf[32768];
    long tot = 0, n;
    int fd = open(name, O_RDONLY);
    if (fd < 0) return -1;
    while ((n = read(fd, buf, sizeof buf)) > 0) tot += n;
    close(fd);
    return tot;
}

int main(int argc, char **argv)
{
    long recs = argc > 1 ? atol(argv[1]) : 100000, files = argc > 2 ? atol(argv[2]) : 1000, i, lf;
    struct stat st;
    double t0, t1;
    char name[64];
    make("big.txt", recs);
    stat("big.txt", &st);
    t0 = now();
    lf = lfsize("big.txt");
    t1 = now();
    printf("P8: %ld records: st_size %ld, LF bytes read %ld, %.2f s (%.1f MB/s)\n", recs,
           (long) st.st_size, lf, t1 - t0, lf / 1e6 / (t1 - t0 > 0 ? t1 - t0 : 1e-9));
    for (i = 0; i < files; i++) { sprintf(name, "s%04ld.txt", i); make(name, 20); }
    t0 = now();
    for (i = 0, lf = 0; i < files; i++) { sprintf(name, "s%04ld.txt", i); lf += lfsize(name); }
    t1 = now();
    printf("P8: %ld small files LF-sized in %.2f s (%.2f ms each), %ld bytes\n", files, t1 - t0,
           (t1 - t0) * 1000 / files, lf);
    t0 = now();
    for (i = 0; i < files; i++) { sprintf(name, "s%04ld.txt", i); stat(name, &st); }
    t1 = now();
    printf("P8: the same %ld files stat()ed only: %.2f s\n", files, t1 - t0);
    return 1;
}
