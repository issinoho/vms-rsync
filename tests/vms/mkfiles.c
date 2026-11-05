/* mkfiles.c - the local-copy test's source tree (tests/vms/local.com), all
   stream-LF or byte files as fopen() makes them (text files of VMS record
   formats are Phase 3):
     mkfiles make <dir>     a.txt (10 KB of lines), empty.txt, bin.dat (300 KB
                            of every byte value), sub/b.txt, sub/deeper/c.txt
     mkfiles change <dir>   rewrite 100 bytes in the middle of bin.dat in place */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void text(const char *path, int lines)
{
    FILE *f = fopen(path, "w");
    int i;
    if (!f) { perror(path); exit(2); }
    for (i = 0; i < lines; i++) fprintf(f, "line %d of %s, some words to fill it out\n", i, path);
    fclose(f);
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
    } else {
        sprintf(p, "%s/bin.dat", argv[2]);
        if (!(f = fopen(p, "r+b"))) { perror(p); return 2; }
        fseek(f, 150000, SEEK_SET);
        for (i = 0; i < 100; i++) fputc('X', f);
        fclose(f);
    }
    return 0;
}
