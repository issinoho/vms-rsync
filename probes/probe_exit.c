/* probe_exit.c - the VMS status a POSIX_EXIT program leaves for exit(n), as
   rsync's exit codes go to DCL (Phase 4: does a failure have error severity?).
   Built with /MAIN=POSIX_EXIT/DEFINE=_POSIX_EXIT, as rsync. Usage: probe_exit n */
#include <stdlib.h>
int main(int argc, char **argv)
{
    exit(argc > 1 ? atoi(argv[1]) : 0);
}
