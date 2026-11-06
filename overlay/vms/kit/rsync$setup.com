$! RSYNC$SETUP.COM - define the rsync command for a user
$!
$! Add to LOGIN.COM (or SYS$MANAGER:SYLOGIN.COM for everyone):
$!     $ @RSYNC$ROOT:[000000]RSYNC$SETUP.COM
$!
$! Upper-case options (-P, -H, -C, ...) need SET PROCESS/PARSE_STYLE=EXTENDED,
$! or double quotes, because traditional DCL parsing changes their case;
$! batch jobs use the traditional style.
$!
$ if f$trnlnm("RSYNC$ROOT") .eqs. ""
$ then
$   write sys$error "RSYNC$SETUP: RSYNC$ROOT is not defined; run RSYNC$STARTUP.COM first"
$   exit 44
$ endif
$ rsync :== $RSYNC$ROOT:[BIN]RSYNC.EXE
$ exit 1
