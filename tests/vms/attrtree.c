/* attrtree.c - attributes under <dir>, for tests/vms/net.com (D6):
     NETME <uid> <gid>                              who is running this
     NETATTR <tree> <relative path> <type> <mode> <uid> <gid> [<link target>]
   type f, d, l (symlink) or o; mode is the permission bits in octal, from
   lstat(). Linked with vms_crtl_init.obj for rsync's C RTL switches.
   Usage: attrtree <tree> <dir> */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void walk(const char *tree, const char *dir, const char *rel)
{
    DIR *dp = opendir(dir);
    struct dirent *e;
    char path[1024], r[1024], target[1024];
    struct stat st;
    long n;
    if (!dp) { printf("NETATTR %s %s (no directory)\n", tree, rel[0] ? rel : "."); return; }
    while ((e = readdir(dp)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        if (lstat(path, &st) != 0) { printf("NETATTR %s %s (no lstat)\n", tree, r); continue; }
        printf("NETATTR %s %s %c %o %u %u", tree, r,
               S_ISDIR(st.st_mode) ? 'd' : S_ISLNK(st.st_mode) ? 'l' : S_ISREG(st.st_mode) ? 'f' : 'o',
               (unsigned) (st.st_mode & 07777), (unsigned) st.st_uid, (unsigned) st.st_gid);
        if (S_ISLNK(st.st_mode) && (n = readlink(path, target, sizeof target - 1)) >= 0) {
            target[n] = '\0';
            printf(" %s", target);
        }
        printf("\n");
        if (S_ISDIR(st.st_mode)) walk(tree, path, r);
    }
    closedir(dp);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: attrtree tree dir\n"); return 2; }
    printf("NETME %u %u\n", (unsigned) getuid(), (unsigned) getgid());
    walk(argv[1], argv[2], "");
    return 0;
}
