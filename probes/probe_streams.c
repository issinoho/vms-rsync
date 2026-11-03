/* probe_streams.c - Phase 0 probe 1b: which kinds of fd can be a vfork+exec
   child's stdin and stdout (decc$set_child_standard_streams)? For each
   combination the child, if it starts, writes what it sees to CHILD_<n>.TXT:
   fstat of fds 0 and 1, and one read and one write. The parent reports whether
   the child ran, its exit status, and what came back on its stdout. */
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

int decc$set_child_standard_streams(int, int, int);
static char *self;

static int child(const char *n, int readin)
{
    char name[40], buf[64];
    struct stat s0, s1;
    long r, w;
    FILE *f;
    sprintf(name, "child_%s.txt", n);
    f = fopen(name, "w");
    r = readin ? read(0, buf, sizeof buf - 1) : -9;
    w = write(1, "OUT-OK", 6);
    if (f) {
        fprintf(f, "fd0 %s mode %o; fd1 %s mode %o; read %ld; write %ld errno %d\n",
                fstat(0, &s0) ? "nostat" : "stat", (unsigned) s0.st_mode,
                fstat(1, &s1) ? "nostat" : "stat", (unsigned) s1.st_mode, r, w, errno);
        fclose(f);
    }
    return 3;
}

static void tcp_pair(int sv[2])
{
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(ls, (struct sockaddr *) &sa, sizeof sa); listen(ls, 1);
    getsockname(ls, (struct sockaddr *) &sa, &len);
    sv[0] = socket(AF_INET, SOCK_STREAM, 0);
    connect(sv[0], (struct sockaddr *) &sa, sizeof sa);
    sv[1] = accept(ls, 0, 0);
    close(ls);
}

/* kind: u = AF_UNIX socketpair, t = TCP, p = pipe, - = inherit */
static void mk(char kind, int v[2], int forchild_reads)
{
    v[0] = v[1] = -1;
    if (kind == 'u') socketpair(AF_UNIX, SOCK_STREAM, 0, v);
    else if (kind == 't') tcp_pair(v);
    else if (kind == 'p') {
        int p[2];
        pipe(p);                           /* p[0] read end, p[1] write end */
        if (forchild_reads) { v[0] = p[1]; v[1] = p[0]; } else { v[0] = p[0]; v[1] = p[1]; }
    }
}

static void run(const char *n, char ki, char ko)
{
    int a[2], b[2], st = -1;
    char buf[200], name[40], *av[4];
    long got = 0, r;
    pid_t pid;
    FILE *f;
    mk(ki, a, 1); mk(ko, b, 0);
    av[0] = self; av[1] = ki == '-' ? "-childnr" : "-child"; av[2] = (char *) n; av[3] = 0;
    pid = vfork();
    if (pid == 0) {
        decc$set_child_standard_streams(ki == '-' ? -1 : a[1], ko == '-' ? -1 : b[1], -1);
        execv(self, av);
        _exit(127);
    }
    if (ki != '-') { close(a[1]); write(a[0], "IN-DATA", 7); if (ki != 'p') shutdown(a[0], SHUT_WR); else close(a[0]); }
    if (ko != '-') {
        close(b[1]);
        for (;;) {
            struct pollfd p; p.fd = b[0]; p.events = POLLIN; p.revents = 0;
            if (poll(&p, 1, 10000) <= 0) break;
            r = read(b[0], buf + got, sizeof buf - 1 - got);
            if (r <= 0) break;
            got += r;
        }
    }
    buf[got] = 0;
    if (pid > 0) {
        int k;
        for (k = 0; k < 100 && waitpid(pid, &st, WNOHANG) == 0; k++) usleep(100000);
        if (k == 100) { printf("%s: child still running after 10 s; killing it\n", n); kill(pid, 9); waitpid(pid, &st, 0); }
    }
    sprintf(name, "child_%s.txt", n);
    printf("%s in=%c out=%c: exit %d, stdout [%.80s]\n", n, ki, ko, WIFEXITED(st) ? WEXITSTATUS(st) : -1, buf);
    f = fopen(name, "r");
    if (f) { if (fgets(buf, sizeof buf, f)) printf("     child saw: %s", buf); fclose(f); remove(name); }
    else printf("     the child did not run\n");
}

int main(int argc, char **argv)
{
    static const char *combos[] = { "-u", "-t", "-p", "u-", "t-", "p-", "uu", "tt", "pp", "up", "pu", "tp", "pt", 0 };
    int i;
    char n[4];
    if (argc > 2 && !strcmp(argv[1], "-child")) return child(argv[2], 1);
    if (argc > 2 && !strcmp(argv[1], "-childnr")) return child(argv[2], 0);
    self = argv[0];
    for (i = 0; combos[i]; i++) {
        sprintf(n, "%d", i);
        run(n, combos[i][0], combos[i][1]);
    }
    return 1;
}
