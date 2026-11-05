/* search.h - the POSIX hash table (hcreate, hsearch, hdestroy) for OpenVMS
   (vms-rsync): the C RTL has none. vms/vms_search.c implements it. */
#ifndef VMS_SEARCH_H
#define VMS_SEARCH_H
#include <stddef.h>
typedef struct entry { char *key; void *data; } ENTRY;
typedef enum { FIND, ENTER } ACTION;
int hcreate(size_t);
ENTRY *hsearch(ENTRY, ACTION);
void hdestroy(void);
#endif
