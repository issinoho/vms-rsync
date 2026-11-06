/* vms_spawn.c - start rsync's other end: the remote shell, or a local --server
   (vms-rsync, D2; patch 0002 calls it where upstream forks).

   VMS has no fork(): vfork() + execv(), with the child's standard streams set
   by decc$set_child_standard_streams() between them (anything else "in the
   child" happens in the parent). The child gets sock as stdin and stdout, and
   an explicit stderr: inherited, the child fails to start when rsync itself
   runs under an ssh command (NOTES N1). There is no PATH: "ssh" means VSI's
   client, and "rsync" (or the name rsync_path gives) this image.

   This file does not include config.h. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <descrip.h>
#include <jpidef.h>
#include <lib$routines.h>
#include <syidef.h>

int decc$set_child_standard_streams(int, int, int);

static void image_name(char *out, size_t sz)
{
    char buf[256];
    unsigned short len = 0;
    int item = JPI$_IMAGNAME;
    struct dsc$descriptor_s d;
    d.dsc$w_length = sizeof buf - 1; d.dsc$b_dtype = DSC$K_DTYPE_T;
    d.dsc$b_class = DSC$K_CLASS_S; d.dsc$a_pointer = buf;
    if (!(lib$getjpi(&item, 0, 0, 0, &d, &len) & 1)) len = 0;
    buf[len] = '\0';
    snprintf(out, sz, "%s", buf);
}

static void ssh_image(char *out, size_t sz)
{
    char arch[32];
    unsigned short len = 0;
    int item = SYI$_ARCH_NAME;
    struct dsc$descriptor_s d;
    d.dsc$w_length = sizeof arch - 1; d.dsc$b_dtype = DSC$K_DTYPE_T;
    d.dsc$b_class = DSC$K_CLASS_S; d.dsc$a_pointer = arch;
    if (!(lib$getsyi(&item, 0, &d, &len, 0, 0) & 1)) len = 0;
    arch[len] = '\0';
    while (len && arch[len - 1] == ' ') arch[--len] = '\0';
    snprintf(out, sz, "SSH$ROOT:[BIN.%s]SSH$SSH.EXE", strcmp(arch, "x86_64") ? arch : "X86_64");
}

/* The image to run for args[0]. */
static const char *resolve(const char *name, const char *self_name, char *out, size_t sz)
{
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (!strcmp(base, "ssh")) { ssh_image(out, sz); return out; }
    if (!strcmp(base, "rsync") || !strcmp(base, "openrsync") || (self_name && !strcmp(name, self_name))) {
        image_name(out, sz);
        return out;
    }
    return name;                    /* a VMS image spec or path given with -e */
}

/* The child's wait status as rsync should read it. VSI's ssh client exits
   with a VMS status, of which waitpid() keeps the low byte: 27 (%X0764D01B)
   when the remote command ended 0 (NOTES N6). That is success. */
int vms_child_status(int st, char *const *args)
{
    const char *base = strrchr(args[0], '/');
    base = base ? base + 1 : args[0];
    if ((st & 0xff) == 0 && ((st >> 8) & 0xff) == 27 &&
        (!strcmp(base, "ssh") || strstr(args[0], "SSH$SSH") || strstr(args[0], "ssh$ssh")))
        return 0;
    return st;
}

/* Start args[] with sock as its stdin and stdout. Returns the pid, or -1. */
int vms_spawn(char *const *args, int sock, const char *self_name)
{
    char image[512];
    const char *path = resolve(args[0], self_name, image, sizeof image);
    struct stat st;
    int errfd = -1, pid;
    /* rsync's own stderr if it is usable by a child (a terminal or a file);
       otherwise the null device: the child's messages are lost (NOTES N1). */
    if (isatty(2) || (fstat(2, &st) == 0 && S_ISREG(st.st_mode))) errfd = 2;
    else errfd = open("NLA0:", O_WRONLY);
    /* A vfork+exec child inherits every open fd, where upstream's fork child
       closes the parent's end of the socketpair. Holding both ends, the child
       never sees end-of-file when the parent stops, and both wait forever
       (NOTES N5). So nothing but its own streams goes to the child. */
    {
        int fd;
        for (fd = 3; fd < 256; fd++)
            if (fd != sock && fd != errfd) (void) fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    /* openrsync makes both ends non-blocking; VSI's ssh client crashes
       (ACCVIO) writing a stream of data to a non-blocking stdout (NOTES N6).
       The parent never uses this end, so give the child a blocking one. */
    (void) fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) & ~O_NONBLOCK);
    pid = vfork();
    if (pid == 0) {
        decc$set_child_standard_streams(sock, sock, errfd);
        execv(path, args);
        _exit(127);
    }
    if (errfd > 2) close(errfd);
    if (pid < 0) errno = EAGAIN;
    return pid;
}
