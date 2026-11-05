/* vms_search.c - hcreate(), hsearch() and hdestroy() for OpenVMS (vms-rsync),
   as POSIX describes them: one table per process, string keys compared with
   strcmp(), entries never removed. Open addressing with linear probing, at
   most half full; the table grows when it would pass that. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "search.h"

static ENTRY *tab;
static size_t cap, used;

static size_t hash(const char *s)
{
    size_t h = 5381;
    while (*s) h = h * 33 + (unsigned char) *s++;
    return h;
}

static int resize(size_t want)
{
    ENTRY *old = tab;
    size_t oldcap = cap, i, n = 16;
    while (n < want * 2) n *= 2;
    if ((tab = calloc(n, sizeof *tab)) == NULL) { tab = old; errno = ENOMEM; return 0; }
    cap = n;
    for (i = 0; i < oldcap; i++)
        if (old[i].key) {
            size_t j = hash(old[i].key) & (cap - 1);
            while (tab[j].key) j = (j + 1) & (cap - 1);
            tab[j] = old[i];
        }
    free(old);
    return 1;
}

int hcreate(size_t nel)
{
    if (tab) return 1;
    used = 0; cap = 0;
    return resize(nel ? nel : 1);
}

ENTRY *hsearch(ENTRY item, ACTION action)
{
    size_t i;
    if (tab == NULL) { errno = ESRCH; return NULL; }
    for (i = hash(item.key) & (cap - 1); tab[i].key; i = (i + 1) & (cap - 1))
        if (!strcmp(tab[i].key, item.key)) return &tab[i];
    if (action == FIND) { errno = ESRCH; return NULL; }
    if ((used + 1) * 2 > cap) {
        if (!resize(used + 1)) return NULL;
        for (i = hash(item.key) & (cap - 1); tab[i].key; i = (i + 1) & (cap - 1)) ;
    }
    tab[i] = item;
    used++;
    return &tab[i];
}

void hdestroy(void)
{
    free(tab);
    tab = NULL; cap = used = 0;
}
