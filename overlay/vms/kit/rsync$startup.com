$! RSYNC$STARTUP.COM - system startup for rsync on OpenVMS
$!
$! Installed by PCSI into SYS$STARTUP.  Defines the system logical name
$! RSYNC$ROOT, pointing at the installed [RSYNC] directory.  To run it at every
$! boot, add this line to SYS$MANAGER:SYSTARTUP_VMS.COM:
$!
$!     $ @SYS$STARTUP:RSYNC$STARTUP.COM
$!
$! P1 = "INSTALL": also print the post-installation tasks (PCSI runs it so).
$! P1 = "REMOVE":  deassign RSYNC$ROOT instead (PCSI runs it so at removal).
$!
$! Users then define the rsync command with
$!     $ @RSYNC$ROOT:[000000]RSYNC$SETUP.COM
$!
$ set noon
$ mode = f$edit(p1, "UPCASE")
$ if mode .eqs. "REMOVE"
$ then
$   if f$trnlnm("RSYNC$ROOT", "LNM$SYSTEM_TABLE") .nes. "" then -
        deassign/system/executive_mode RSYNC$ROOT
$   exit 1
$ endif
$!
$! This procedure sits in <destination>[SYS$STARTUP]; the product is in
$! <destination>[RSYNC].  Rooted logicals need the physical form:
$! DKA0:[SYS0.SYSCOMMON.SYS$STARTUP] -> DKA0:[SYS0.SYSCOMMON.RSYNC.]
$ proc = f$environment("PROCEDURE")
$ dev = f$parse(proc,,,"DEVICE","NO_CONCEAL")
$ dir = f$edit(f$parse(proc,,,"DIRECTORY","NO_CONCEAL"), "UPCASE") - "]["
$ root = dir - "SYS$STARTUP]" + "RSYNC.]"
$ if root .eqs. dir + "RSYNC.]"
$ then
$   write sys$error "RSYNC$STARTUP: expected to be in a [SYS$STARTUP] directory, not ''dir'"
$   exit 44
$ endif
$ root = root - ".000000"
$ define/system/executive_mode/translation_attributes=concealed RSYNC$ROOT 'dev''root'
$ if f$search("RSYNC$ROOT:[BIN]RSYNC.EXE") .eqs. ""
$ then
$   write sys$error "RSYNC$STARTUP: RSYNC.EXE not found under ''dev'''root'"
$   exit 44
$ endif
$ if mode .nes. "INSTALL" then exit 1
$ say = "write sys$output"
$ say ""
$ say "    Post-installation tasks for rsync"
$ say ""
$ say "    At system startup: to define RSYNC$ROOT at every boot, add this line to"
$ say "    SYS$MANAGER:SYSTARTUP_VMS.COM:"
$ say "    $ @SYS$STARTUP:RSYNC$STARTUP.COM"
$ say "    For each user: to define the rsync command, add this line to LOGIN.COM:"
$ say "    $ @RSYNC$ROOT:[000000]RSYNC$SETUP.COM"
$ say ""
$ say "    PRODUCT REMOVE RSYNC removes the product and deassigns RSYNC$ROOT."
$ say ""
$ exit 1
