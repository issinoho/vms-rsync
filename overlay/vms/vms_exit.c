/* vms_exit.c - exit() for rsync on VMS (vms_config.h makes exit vms_exit).
   Built /MAIN=POSIX_EXIT, exit(n) leaves %X0035A000 + n*8 with success
   severity for every n but 1 (probes/probe_exit.c), so under DCL a partial
   transfer (23) or a timeout (30) looked like success. Run from DCL, code 0
   is success and any other code N an error-severity status in the C RTL's
   POSIX range with the message suppressed: %X1035A002 + N*8, so ON ERROR
   fires and N is ($STATUS .AND. %X7F8) / 8.
   Kept as a POSIX exit: under a Unix shell (GNV bash: SHELL set and not
   "DCL"), and in a --server child, whose parent decodes its status. */
#include <stdlib.h>
#include <string.h>
#undef exit

void decc$exit(int);
void decc$__posix_exit(int);

int vms_server;

void vms_exit(int status)
{
    const char *shell = getenv("SHELL");
    if (vms_server || (shell != NULL && strcmp(shell, "DCL") != 0))
        decc$__posix_exit(status);
    if (status == 0)
        decc$exit(1);
    decc$exit(0x10000000 | 0x35A000 | ((status & 0xFF) << 3) | 2);
}
