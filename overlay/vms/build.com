$! BUILD.COM - build openrsync for OpenVMS (vms-rsync) with VSI C.
$!
$!   @[.VMS]BUILD [CLEAN]
$!
$! Compiles [.VMS]SOURCES.TXT (the Makefile's OBJS, then main) and our
$! [.VMS]*.C into [.VMS.OBJ], every one even after a failure, then links
$! RSYNC.EXE with the installed zlib kit (ZLIB$ROOT). Ends with
$! "BUILD: done, <n> failed".
$ set noon
$ say = "write sys$output"
$ proc = f$environment("PROCEDURE")
$ vms = f$parse(proc,,,"DEVICE") + f$parse(proc,,,"DIRECTORY")
$ top = vms - ".VMS]" + "]"
$ set default 'top'
$ if f$search("[.VMS]OBJ.DIR") .eqs. "" then create/directory [.VMS.OBJ]
$ if p1 .eqs. "CLEAN" then delete/nolog [.VMS.OBJ]*.*;*
$ if f$trnlnm("ZLIB$ROOT") .eqs. ""
$ then
$   say "BUILD: ZLIB$ROOT is not defined: install the vms-zlib kit"
$   exit 44
$ endif
$! VSI C's default mode, as the family's GNU ports (vms-grep): it takes C99 and,
$! unlike /STANDARD=C99 (which defines _ANSI_C_SOURCE), declares the POSIX
$! functions. An implicitly declared function is an error, so nothing missing
$! slips through. 32-bit pointers; X/Open stat (st_ino a scalar), large files,
$! BSD 4.4 sockaddr; POSIX exit statuses so rsync's exit codes reach the caller.
$! Any warning fails a compile, except BADPTRARITH: arithmetic on a void *, which
$! openrsync does in io.c, receiver.c and sender.c and VSI C treats as char *,
$! exactly as GNU C does.
$ cflags = "/NAMES=(AS_IS,SHORTENED)/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES" + -
    "/WARNINGS=(ERRORS=IMPLICITFUNC,DISABLE=(MIXFUNCVOID,BADPTRARITH))/MAIN=POSIX_EXIT" + -
    "/DEFINE=(_USE_STD_STAT,_LARGEFILE,_POSIX_EXIT,_SOCKADDR_LEN)" + -
    "/INCLUDE=([],[.VMS],ZLIB$ROOT:[INCLUDE])/NOLIST"
$ failed == 0
$ objs == ""
$ open/read src [.VMS]SOURCES.TXT
$next:
$ read/end=srcdone src name
$ name = f$edit(name, "TRIM")
$ if name .eqs. "" then goto next
$ call compile 'name' []
$ goto next
$srcdone:
$ close src
$ours:
$ f = f$search("[.VMS]*.C", 1)
$ if f .eqs. "" then goto link
$ call compile 'f$parse(f,,,"NAME")' [.VMS]
$ goto ours
$link:
$ if failed .ne. 0
$ then
$   say "BUILD: done, ", failed, " failed (no link)"
$   exit 1
$ endif
$ open/write opt [.VMS.OBJ]RSYNC.OPT
$ i = 0
$optl:
$ o = f$element(i, ",", objs)
$ if o .eqs. "," .or. o .eqs. "" then goto optdone
$ write opt "[.VMS.OBJ]", o, ".OBJ"
$ i = i + 1
$ goto optl
$optdone:
$ write opt "ZLIB$ROOT:[LIB]LIBZ.OLB/LIBRARY"
$ close opt
$ link/EXECUTABLE=[.VMS]RSYNC.EXE/MAP=[.VMS.OBJ]RSYNC.MAP [.VMS.OBJ]RSYNC.OPT/OPTIONS
$ if .not. $status
$ then
$   say "BUILD: LINK FAILED"
$   failed = 1
$ endif
$ say "BUILD: done, ", failed, " failed"
$ exit 1
$!
$compile: subroutine
$! a subroutine starts with ON ERROR THEN EXIT: a failed compile must not end it
$ set noon
$ obj = "[.VMS.OBJ]" + p1 + ".OBJ"
$ if f$search(obj) .eqs. "" then goto compile_it
$ if f$cvtime(f$file_attributes(obj,"RDT")) .gts. f$cvtime(f$file_attributes(p2 + p1 + ".C","RDT")) -
     then goto have
$compile_it:
$ cc'cflags'/OBJECT='obj' 'p2''p1'.C
$ if .not. $status
$ then
$   say "BUILD: FAILED ", p1
$   failed == failed + 1
$   if f$search(obj) .nes. "" then delete/nolog 'obj';*
$   exit 1
$ endif
$have:
$ if objs .eqs. ""
$ then
$   objs == p1
$ else
$   objs == objs + "," + p1
$ endif
$ exit 1
$ endsubroutine
