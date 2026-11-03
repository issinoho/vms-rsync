$! probes.com - Phase 0: build and run probe_pair, probe_files and probe_var in
$! [.PSCRATCH] (made empty first; deleted at the end), with the C RTL feature
$! switches rsync would set (process logical names, deassigned at the end).
$ set noon
$ here = f$environment("DEFAULT")
$ say = "write sys$output"
$ say "DISK ", f$getdvi(f$parse(here,,,"DEVICE"),"ACPTYPE"), " ", f$getsyi("ARCH_NAME"), " ", f$getsyi("VERSION")
$ cc/STANDARD=C99/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/OBJECT=probe_pair.obj [.probes]probe_pair.c
$ link/EXECUTABLE=probe_pair.exe probe_pair.obj
$ cc/STANDARD=C99/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/OBJECT=probe_files.obj [.probes]probe_files.c
$ link/EXECUTABLE=probe_files.exe probe_files.obj
$ cc/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/OBJECT=probe_var.obj [.probes]probe_var.c
$ link/EXECUTABLE=probe_var.exe probe_var.obj
$ define/process DECC$EFS_CHARSET ENABLE
$ define/process DECC$FILENAME_UNIX_REPORT ENABLE
$ define/process DECC$FILENAME_UNIX_NO_VERSION ENABLE
$ define/process DECC$READDIR_DROPDOTNOTYPE ENABLE
$ define/process DECC$EFS_CASE_PRESERVE ENABLE
$ define/process DECC$EFS_FILE_TIMESTAMPS ENABLE
$ define/process DECC$POSIX_SEEK_STREAM_FILE ENABLE
$ say "=== PROBE 1"
$ pair := $'here'probe_pair.exe
$ pair 10
$ if f$search("PSCRATCH.DIR") .nes. "" then call wipe
$ create/directory [.pscratch]
$ set default [.pscratch]
$ say "=== PROBES 4-7, 10"
$ files := $'here'probe_files.exe
$ files
$ say "--- DCL's view"
$ directory/size=all/date=(created,modified)/protection/width=(filename=26)
$ say "=== PROBE 8"
$ var := $'here'probe_var.exe
$ var 100000 1000
$ directory/size=all/full big.txt
$ set default 'here'
$ call wipe
$ deassign/process DECC$EFS_CHARSET
$ deassign/process DECC$FILENAME_UNIX_REPORT
$ deassign/process DECC$FILENAME_UNIX_NO_VERSION
$ deassign/process DECC$READDIR_DROPDOTNOTYPE
$ deassign/process DECC$EFS_CASE_PRESERVE
$ deassign/process DECC$EFS_FILE_TIMESTAMPS
$ deassign/process DECC$POSIX_SEEK_STREAM_FILE
$ exit 1
$wipe: subroutine
$ set security/protection=o:rwed [.pscratch...]*.*;*
$ delete/nolog [.pscratch.subdir]*.*;*
$ delete/nolog [.pscratch]*.*;*
$ set security/protection=o:rwed pscratch.dir;1
$ delete/nolog pscratch.dir;1
$ endsubroutine
