/* probe_ssh2.c - Phase 0 probe 3: rsync's ssh transport through libssh2, in
   process (the RSYNC_SSH helper's alternative, D2). Connects to a Linux sshd,
   logs in with a key file, then
     1. exec "cat": N MB of every byte value written and read back without
        blocking (poll on the socket), EOF sent, the echo compared;
     2. exec "rsync --server --sender -r . <dir>/": reads the 4-byte protocol
        version that an rsync server writes first.
   The server's key is printed, not checked: a probe against a test sshd.
   Usage: probe_ssh2 host port user keyfile dir [megabytes] */
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <libssh2.h>

static unsigned char pat(long i) { return (unsigned char) ((i * 7 + (i >> 9)) & 0xFF); }

static void waitsock(int sock, LIBSSH2_SESSION *s)
{
    struct pollfd p;
    int dir = libssh2_session_block_directions(s);
    p.fd = sock; p.revents = 0;
    p.events = (dir & LIBSSH2_SESSION_BLOCK_INBOUND ? POLLIN : 0) | (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND ? POLLOUT : 0);
    if (!p.events) p.events = POLLIN;
    poll(&p, 1, 1000);
}

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints, *ai, *a;
    int s = -1;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &ai)) return -1;
    for (a = ai; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s >= 0 && connect(s, a->ai_addr, a->ai_addrlen) == 0) break;
        if (s >= 0) close(s);
        s = -1;
    }
    freeaddrinfo(ai);
    return s;
}

int main(int argc, char **argv)
{
    static unsigned char out[16384], in[16384];
    LIBSSH2_SESSION *s;
    LIBSSH2_CHANNEL *c;
    const char *fp;
    char cmd[400], *msg;
    long total, sent = 0, got = 0, i, n;
    int sock, bad = -1, eofsent = 0, rc, k;
    unsigned char v[4];
    if (argc < 6) { printf("usage: probe_ssh2 host port user keyfile dir [MB]\n"); return 1; }
    total = (argc > 6 ? atol(argv[6]) : 10) * 1024L * 1024L;
    if (libssh2_init(0)) { printf("SSH2: libssh2_init failed\n"); return 1; }
    printf("SSH2: libssh2 %s\n", libssh2_version(0));
    if ((sock = connect_to(argv[1], argv[2])) < 0) { printf("SSH2: connect failed: %s\n", strerror(errno)); return 1; }
    s = libssh2_session_init();
    if (libssh2_session_handshake(s, sock)) {
        libssh2_session_last_error(s, &msg, 0, 0);
        printf("SSH2: handshake failed: %s\n", msg); return 1;
    }
    fp = libssh2_hostkey_hash(s, LIBSSH2_HOSTKEY_HASH_SHA256);
    printf("SSH2: handshake ok, %s, %s; host key SHA-256 %s\n",
           libssh2_session_methods(s, LIBSSH2_METHOD_KEX), libssh2_session_methods(s, LIBSSH2_METHOD_CRYPT_CS),
           fp ? "received" : "missing");
    if (libssh2_userauth_publickey_fromfile(s, argv[3], NULL, argv[4], NULL)) {
        libssh2_session_last_error(s, &msg, 0, 0);
        printf("SSH2: key login failed: %s\n", msg); return 1;
    }
    printf("SSH2: key login ok\n");

    /* 1. cat */
    c = libssh2_channel_open_session(s);
    if (!c || libssh2_channel_exec(c, "cat")) { printf("SSH2: exec cat failed\n"); return 1; }
    libssh2_session_set_blocking(s, 0);
    for (;;) {
        int progress = 0;
        if (sent < total) {
            long m = total - sent < (long) sizeof out ? total - sent : (long) sizeof out;
            for (i = 0; i < m; i++) out[i] = pat(sent + i);
            n = libssh2_channel_write(c, (char *) out, (size_t) m);
            if (n > 0) { sent += n; progress = 1; }
            else if (n != LIBSSH2_ERROR_EAGAIN) { printf("SSH2: write error %ld\n", n); break; }
        } else if (!eofsent) {
            rc = libssh2_channel_send_eof(c);
            if (rc == 0) { eofsent = 1; progress = 1; }
            else if (rc != LIBSSH2_ERROR_EAGAIN) { printf("SSH2: send_eof error %d\n", rc); break; }
        }
        n = libssh2_channel_read(c, (char *) in, sizeof in);
        if (n > 0) {
            for (i = 0; i < n; i++)
                if (bad < 0 && in[i] != pat(got + i)) bad = (int) (got + i);
            got += n; progress = 1;
        } else if (n != LIBSSH2_ERROR_EAGAIN && n < 0) { printf("SSH2: read error %ld\n", n); break; }
        if (eofsent && libssh2_channel_eof(c)) break;
        if (!progress) waitsock(sock, s);
    }
    printf("SSH2: cat: sent %ld, got back %ld, first mismatch %d: %s\n", sent, got, bad,
           sent == total && got == total && bad < 0 ? "OK" : "FAILED");
    while (libssh2_channel_close(c) == LIBSSH2_ERROR_EAGAIN) waitsock(sock, s);
    printf("SSH2: cat exit status %d\n", libssh2_channel_get_exit_status(c));
    libssh2_channel_free(c);
    libssh2_session_set_blocking(s, 1);

    /* 2. rsync --server: its first 4 bytes are its protocol version (little-endian) */
    snprintf(cmd, sizeof cmd, "rsync --server --sender -r . %s/", argv[5]);
    c = libssh2_channel_open_session(s);
    if (!c || libssh2_channel_exec(c, cmd)) { printf("SSH2: exec rsync failed\n"); return 1; }
    for (k = 0; k < 4; ) {
        n = libssh2_channel_read(c, (char *) v + k, (size_t) (4 - k));
        if (n <= 0) break;
        k += (int) n;
    }
    if (k == 4) printf("SSH2: rsync --server says protocol %d\n", v[0] | v[1] << 8 | v[2] << 16 | v[3] << 24);
    else printf("SSH2: rsync --server: only %d bytes\n", k);
    libssh2_channel_send_eof(c);
    libssh2_channel_close(c);
    libssh2_channel_free(c);
    libssh2_session_disconnect(s, "probe done");
    libssh2_session_free(s);
    close(sock);
    libssh2_exit();
    return 1;
}
