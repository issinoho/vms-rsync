/* probe_lfview.c - Phase 3 (NOTES N7): the LF view of a variable-length record
   file. Reads it with read() (the C RTL's LF lines) and prints the byte count
   and the last bytes; then copies those bytes into a delete-on-close stream file
   as vms_at.c's lf_view() does and prints that file's size and last bytes.
   Usage: probe_lfview file */
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static void tail(const char *what, const unsigned char *b, long n)
{
    long i;
    printf("%s: %ld bytes, last:", what, n);
    for (i = n > 12 ? n - 12 : 0; i < n; i++) printf(" %02x", b[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    static unsigned char all[200000], back[200000];
    struct stat st;
    long n = 0, r, m = 0;
    int in = open(argv[1], O_RDONLY), out;
    stat(argv[1], &st);
    printf("on disk: st_size %ld rfm %d rat %d\n", (long) st.st_size, st.st_fab_rfm, st.st_fab_rat);
    while ((r = read(in, all + n, sizeof all - n)) > 0) n += r;
    close(in);
    tail("read()", all, n);
    out = open("SYS$SCRATCH:PROBE_LFVIEW.TMP", O_RDWR | O_CREAT | O_TRUNC, 0600, "fop=dlt");
    write(out, all, (size_t) n);
    fstat(out, &st);
    printf("copy before lseek: fstat size %ld rfm %d\n", (long) st.st_size, st.st_fab_rfm);
    lseek(out, 0, SEEK_SET);
    while ((r = read(out, back + m, sizeof back - m)) > 0) m += r;
    tail("copy read back", back, m);
    fstat(out, &st);
    printf("copy after reading: fstat size %ld\n", (long) st.st_size);
    close(out);
    return 1;
}
