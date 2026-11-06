/* mkfiles.c - the local-copy test's source tree (tests/vms/local.com), all
   stream-LF or byte files as fopen() makes them (text files of VMS record
   formats are Phase 3):
     mkfiles make <dir>     a.txt (10 KB of lines), empty.txt, bin.dat (300 KB
                            of every byte value), sub/b.txt, sub/deeper/c.txt,
                            README (no type) and .profile (no name part);
                            notes.txt and data.dat (variable-length records),
                            fix.bin (fixed 512-byte records)
     mkfiles change <dir>   rewrite 100 bytes in the middle of bin.dat in place */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void text(const char *path, int lines)
{
    FILE *f = fopen(path, "w");
    int i;
    if (!f) { perror(path); exit(2); }
    for (i = 0; i < lines; i++) fprintf(f, "line %d of %s, some words to fill it out\n", i, path);
    fclose(f);
}

/* A file of records: one write() is one record. */
static void records(const char *path, int n, const char *rfm, const char *rat)
{
    char line[200];
    int fd = creat(path, 0644, rfm, rat), i, len;
    if (fd < 0) { perror(path); exit(2); }
    for (i = 0; i < n; i++) {
        len = sprintf(line, "record %d of %s%s", i, path, i % 7 == 3 ? "" : ", with some words");
        if (i % 11 == 5) len = 0;                       /* empty records too */
        write(fd, line, (size_t) len);
    }
    close(fd);
}

/* 4 records of 512 bytes, every byte value. */
static void fixed(const char *path)
{
    unsigned char rec[512];
    int fd = creat(path, 0644, "rfm=fix", "mrs=512"), i, j;
    if (fd < 0) { perror(path); exit(2); }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 512; j++) rec[j] = (unsigned char) (i * 512 + j);
        write(fd, rec, sizeof rec);
    }
    close(fd);
}

int main(int argc, char **argv)
{
    char p[512];
    long i;
    FILE *f;
    if (argc < 3) { fprintf(stderr, "usage: mkfiles make|change dir\n"); return 2; }
    if (!strcmp(argv[1], "make")) {
        mkdir(argv[2], 0755);
        sprintf(p, "%s/a.txt", argv[2]); text(p, 200);
        sprintf(p, "%s/empty.txt", argv[2]); text(p, 0);
        sprintf(p, "%s/bin.dat", argv[2]);
        if (!(f = fopen(p, "wb"))) { perror(p); return 2; }
        for (i = 0; i < 300000; i++) fputc((int) ((i * 7 + (i >> 9)) & 0xff), f);
        fclose(f);
        sprintf(p, "%s/sub", argv[2]); mkdir(p, 0755);
        sprintf(p, "%s/sub/b.txt", argv[2]); text(p, 50);
        sprintf(p, "%s/sub/deeper", argv[2]); mkdir(p, 0755);
        sprintf(p, "%s/sub/deeper/c.txt", argv[2]); text(p, 5);
        sprintf(p, "%s/README", argv[2]); text(p, 3);       /* no type: NOTES N6 */
        /* VMS record files (Phase 3, D3): variable-length text of a text type
           and of another type, and fixed 512-byte binary records */
        sprintf(p, "%s/notes.txt", argv[2]); records(p, 120, "rfm=var", "rat=cr");
        sprintf(p, "%s/data.dat", argv[2]); records(p, 40, "rfm=var", "rat=cr");
        sprintf(p, "%s/fix.bin", argv[2]); fixed(p);
        sprintf(p, "%s/blank.txt", argv[2]);                /* empty lines must survive */
        if (!(f = fopen(p, "w"))) { perror(p); return 2; }
        fputs("first\n\nafter one empty line\n\n\nafter two\n\n", f);
        fclose(f);
        sprintf(p, "%s/.profile", argv[2]); text(p, 2);     /* no name part */
    } else if (!strcmp(argv[1], "change2")) {               /* another change: 'Y' */
        sprintf(p, "%s/bin.dat", argv[2]);
        if (!(f = fopen(p, "r+b"))) { perror(p); return 2; }
        fseek(f, 1000, SEEK_SET);
        for (i = 0; i < 100; i++) fputc('Y', f);
        fclose(f);
    } else {
        sprintf(p, "%s/bin.dat", argv[2]);
        if (!(f = fopen(p, "r+b"))) { perror(p); return 2; }
        fseek(f, 150000, SEEK_SET);
        for (i = 0; i < 100; i++) fputc('X', f);
        fclose(f);
    }
    return 0;
}
