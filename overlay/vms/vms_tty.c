/* vms_tty.c - tcgetattr() and tcsetattr() for OpenVMS (vms-rsync): the C RTL
   declares them as not implemented. Both say "not a terminal", which
   compats.c's readpassphrase() takes as "cannot turn echo off". */
#include <errno.h>

struct termios;

int tcgetattr(int fd, struct termios *t) { (void) fd; (void) t; errno = ENOTTY; return -1; }
int tcsetattr(int fd, int how, const struct termios *t) { (void) fd; (void) how; (void) t; errno = ENOTTY; return -1; }
