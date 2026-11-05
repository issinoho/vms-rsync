/* vms_entropy.c - getentropy() for OpenVMS (vms-rsync), which has no kernel
   random source for it. It seeds openrsync's compat arc4random(), used only for
   the checksum seed rsync picks as a server and for temporary file names
   (created exclusively): neither is a secret, and this is NOT suitable for one.
   It mixes the 100-ns system clock, the process ID, the C clock and a counter
   through splitmix64. */
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <starlet.h>

static unsigned long long mix(unsigned long long x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

int getentropy(void *buf, size_t n)
{
    static unsigned long long counter;
    unsigned long long t = 0, s;
    unsigned char *p = buf;
    if (n > 256) { errno = EIO; return -1; }
    sys$gettim((void *) &t);
    s = t ^ ((unsigned long long) getpid() << 32) ^ (unsigned long long) clock()
        ^ (unsigned long long) (size_t) &t;
    while (n > 0) {
        unsigned long long v = mix(s ^ ++counter);
        size_t k = n < sizeof v ? n : sizeof v;
        memcpy(p, &v, k);
        p += k; n -= k;
        s = v;
    }
    return 0;
}
