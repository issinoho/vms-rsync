/* probe_pair.c - Phase 0 probe 1: can rsync's transport model work on VMS?
   The parent makes a socketpair, starts itself as a child with vfork+execv and
   the pair's other end as the child's stdin and stdout
   (decc$set_child_standard_streams, as vms-lighttpd's r_spawn.c), then pushes
   N bytes of every byte value through the child (which echoes stdin to
   stdout) with poll(), half-closes, reads to EOF and compares. Reports the
   child's exit status. Usage: probe_pair [megabytes [same|dup|two]]
   (child: probe_pair -child). A third argument chooses the child's stderr:
   inherit (default), null (the null device) or pipe (relayed). same: one socket as stdin and stdout; dup: a
   dup() of it as stdout; two: a socketpair each way. */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

int decc$set_child_standard_streams(int, int, int);

static unsigned char pat(long i) { return (unsigned char) ((i * 7 + (i >> 9)) & 0xFF); }

static int child(void)
{
    static char buf[16384];
    long n, tot = 0, w, o;
    while ((n = read(0, buf, sizeof buf)) > 0) {
        for (o = 0; o < n; o += w)
            if ((w = write(1, buf + o, (size_t) (n - o))) <= 0) return 2;
        tot += n;
    }
    fprintf(stderr, "CHILD: echoed %ld bytes, read ended %ld errno %d\n", tot, n, n < 0 ? errno : 0);
    return n < 0 ? 4 : 3;     /* 3: clean EOF (distinct from 1, VMS success) */
}

int main(int argc, char **argv)
{
    static unsigned char out[16384], in[16384];
    int sv[2], sw[2], rd, wr, cin, cout, st = 0, eof = 0, closed = 0, bad = -1;
    long total, sent = 0, got = 0, i;
    pid_t pid;
    char *av[3], *av_ssh[18], *target = 0;
    if (argc > 1 && !strcasecmp(argv[1], "-child")) {
        fprintf(stderr, "CHILD: argv[1] arrived as [%s]\n", argv[1]);
        return child();
    }
    total = (argc > 1 ? atol(argv[1]) : 10) * 1024L * 1024L;
    const char *how = argc > 2 ? argv[2] : "same", *errhow = argc > 3 ? argv[3] : "inherit";
    if (!strcmp(how, "ssh")) errhow = "pipe";
    int cerr = -1, ep[2] = { -1, -1 }, k;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) { perror("socketpair"); return 2; }
    rd = wr = sv[0]; cin = cout = sv[1];
    if (!strcmp(how, "dup")) cout = dup(sv[1]);
    if (!strcmp(how, "two")) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sw)) { perror("socketpair"); return 2; }
        rd = sw[0]; cout = sw[1];
    }
    if (!strcmp(errhow, "null")) cerr = open("NLA0:", O_WRONLY);
    if (!strcmp(errhow, "pipe") && !pipe(ep)) cerr = ep[1];
    printf("PAIR: mode %s, stderr %s (fd %d)\n", how, errhow, cerr);
    av[0] = argv[0]; av[1] = "-child"; av[2] = 0;
    {   /* probe_pair <MB> ssh <ssh image> <user@host> <port> <remote command> [key]:
           probe 2, VSI's ssh client as rsync's -e program, echoing through the remote command */
        static char *sv_av[18], remote[300];
        if (argc > 6 && !strcmp(how, "ssh")) {
            int k2 = 0;
            snprintf(remote, sizeof remote, "%s", argv[6]);
            sv_av[k2++] = "ssh"; sv_av[k2++] = "-T"; sv_av[k2++] = "-p"; sv_av[k2++] = argv[5];
            if (argc > 7) { sv_av[k2++] = "-i"; sv_av[k2++] = argv[7]; }
            sv_av[k2++] = "-o"; sv_av[k2++] = "BatchMode=yes";
            sv_av[k2++] = "-o"; sv_av[k2++] = "StrictHostKeyChecking=no";
            sv_av[k2++] = "-o"; sv_av[k2++] = "UserKnownHostsFile=NLA0:";
            sv_av[k2++] = argv[4]; sv_av[k2++] = remote; sv_av[k2] = 0;
            target = argv[3];
            memcpy(av_ssh, sv_av, sizeof sv_av);
        }
    }
    pid = vfork();
    if (pid == 0) {
        decc$set_child_standard_streams(cin, cout, cerr);
        if (target) execv(target, av_ssh); else execv(argv[0], av);
        _exit(127);
    }
    if (pid < 0) { perror("vfork"); return 2; }
    close(cin); if (cout != cin) close(cout);
    while (!eof) {
        struct pollfd pp[2], *pw = &pp[0], *pr = &pp[1];
        pw->fd = sent < total ? wr : -1; pw->events = POLLOUT; pw->revents = 0;
        pr->fd = rd; pr->events = POLLIN; pr->revents = 0;
        if (poll(pp, 2, 30000) <= 0) { printf("PAIR: poll timeout/error after sent %ld got %ld\n", sent, got); break; }
        if ((pw->revents & POLLOUT) && sent < total) {
            long n = total - sent < (long) sizeof out ? total - sent : (long) sizeof out, w;
            for (i = 0; i < n; i++) out[i] = pat(sent + i);
            w = write(wr, out, (size_t) n);
            if (w > 0) sent += w;
            if (sent >= total && !closed) { shutdown(wr, SHUT_WR); closed = 1; }
        }
        if (pr->revents & (POLLIN | POLLHUP)) {
            long n = read(rd, in, sizeof in);
            if (n <= 0) eof = 1;
            for (i = 0; i < n; i++)
                if (bad < 0 && in[i] != pat(got + i)) bad = (int) (got + i);
            if (n > 0) {
                FILE *dbg = fopen("pair_back.bin", got ? "a" : "w");
                if (dbg) { fwrite(in, 1, (size_t) n, dbg); fclose(dbg); }
                got += n;
            }
        }
    }
    if (bad >= 0 && got > 0) {
        long k;
        printf("PAIR: the child sent back: [");
        for (k = 0; k < got && k < 200; k++) putchar(in[k] >= 32 && in[k] < 127 ? in[k] : '.');
        printf("]\n");
    }
    if (ep[1] >= 0) {
        char eb[2000]; long en, et = 0;
        close(ep[1]);
        while (et < (long) sizeof eb - 1 && (en = read(ep[0], eb + et, sizeof eb - 1 - et)) > 0) et += en;
        eb[et] = 0;
        printf("PAIR: child stderr (%ld bytes): %s\n", et, eb);
    }
    for (k = 0; k < 100 && waitpid(pid, &st, WNOHANG) == 0; k++) usleep(100000);
    if (k == 100) printf("PAIR: the child has not exited after 10 s\n");
    printf("PAIR: sent %ld, got back %ld, first mismatch %d, child status %d (exited %d)\n",
           sent, got, bad, st, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    printf("PAIR: %s\n", sent == total && got == total && bad < 0 ? "OK" : "FAILED");
    return 1;
}
