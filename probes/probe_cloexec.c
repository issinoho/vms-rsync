/* probe_cloexec.c - Phase 1 (NOTES N5): does a vfork+exec child inherit the
   parent's end of a socketpair, so it never sees end-of-file when the parent
   closes it? And does FD_CLOEXEC on that end prevent it? The parent writes a
   few bytes and close()s its end (no shutdown()); the child reads to EOF and
   exits. Usage: probe_cloexec [cloexec]   (child: probe_cloexec -child) */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int decc$set_child_standard_streams(int, int, int);

int main(int argc, char **argv)
{
    int sv[2], st = 0, k, nul;
    pid_t pid;
    char *av[3], buf[64];
    if (argc > 1 && !strcmp(argv[1], "-child")) {
        long n, tot = 0;
        while ((n = read(0, buf, sizeof buf)) > 0) tot += n;
        return n == 0 ? 3 : 4;                          /* 3: saw EOF */
    }
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    if (argc > 1 && !strcmp(argv[1], "cloexec")) fcntl(sv[0], F_SETFD, FD_CLOEXEC);
    nul = open("NLA0:", O_WRONLY);
    av[0] = argv[0]; av[1] = "-child"; av[2] = 0;
    pid = vfork();
    if (pid == 0) {
        decc$set_child_standard_streams(sv[1], nul, nul);
        execv(argv[0], av);
        _exit(127);
    }
    close(sv[1]);
    write(sv[0], "hello", 5);
    close(sv[0]);                                       /* no shutdown() */
    for (k = 0; k < 100 && waitpid(pid, &st, WNOHANG) == 0; k++) usleep(100000);
    printf("CLOEXEC %s: %s\n", argc > 1 ? argv[1] : "inherit",
           k == 100 ? "child still waiting after 10 s (it holds the parent's end)"
                    : WIFEXITED(st) && WEXITSTATUS(st) == 3 ? "child saw EOF and exited" : "child exited otherwise");
    if (k == 100) kill(pid, 9);
    return 1;
}
