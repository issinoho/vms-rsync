/* probe_exitmacro.c - does our "#define exit vms_exit" survive <stdlib.h>
   when _POSIX_EXIT is defined (as rsync's build)? Prints which exit ran. */
#include <stdio.h>
void vms_exit(int);
#define exit vms_exit
#include <stdlib.h>
#undef exit
void vms_exit(int n) { printf("PROBE: our vms_exit(%d)\n", n); exit(n); }
#define exit vms_exit
int main(void)
{
    exit(23);
}
