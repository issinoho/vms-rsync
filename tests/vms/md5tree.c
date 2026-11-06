/* md5tree.c - NETMD5 <tree> <relative path> <md5> for every file under <dir>,
   names as readdir() gives them under rsync's C RTL switches (linked with
   vms_crtl_init.obj) and MD5 of the bytes read (openrsync's compat MD5, from
   compats.obj). For tests/vms/net.com; stream and binary files only.
   Usage: md5tree <tree> <dir> */
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { uint32_t state[4]; uint64_t count; uint8_t buffer[64]; } MD5_CTX;
void MD5Init(MD5_CTX *);
void MD5Update(MD5_CTX *, const uint8_t *, size_t);
void MD5Final(uint8_t [16], MD5_CTX *);

static void file(const char *tree, const char *path, const char *rel)
{
    static uint8_t buf[65536];
    uint8_t d[16];
    MD5_CTX c;
    long n;
    int fd = open(path, O_RDONLY), i;
    if (fd < 0) { printf("NETMD5 %s %s (unreadable)\n", tree, rel); return; }
    MD5Init(&c);
    while ((n = read(fd, buf, sizeof buf)) > 0) MD5Update(&c, buf, (size_t) n);
    close(fd);
    MD5Final(d, &c);
    printf("NETMD5 %s %s ", tree, rel);
    for (i = 0; i < 16; i++) printf("%02X", d[i]);
    printf("\n");
}

static void walk(const char *tree, const char *dir, const char *rel)
{
    DIR *dp = opendir(dir);
    struct dirent *e;
    char path[1024], r[1024];
    struct stat st;
    if (!dp) { printf("NETMD5 %s %s (no directory)\n", tree, rel[0] ? rel : "."); return; }
    while ((e = readdir(dp)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) walk(tree, path, r);
        else file(tree, path, r);
    }
    closedir(dp);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: md5tree tree dir\n"); return 2; }
    walk(argv[1], argv[2], "");
    return 0;
}
