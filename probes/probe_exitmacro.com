$! probe_exitmacro.com - our exit macro with and without _POSIX_EXIT
$ set noon
$ cc/MAIN=POSIX_EXIT/DEFINE=_POSIX_EXIT/OBJECT=pem1.obj [.probes]probe_exitmacro.c
$ link/EXECUTABLE=pem1.exe pem1.obj
$ write sys$output "with _POSIX_EXIT:"
$ run pem1.exe
$ write sys$output "status ", $status
$ cc/MAIN=POSIX_EXIT/OBJECT=pem2.obj [.probes]probe_exitmacro.c
$ link/EXECUTABLE=pem2.exe pem2.obj
$ write sys$output "without _POSIX_EXIT:"
$ run pem2.exe
$ write sys$output "status ", $status
$ delete/nolog pem1.obj;*,pem1.exe;*,pem2.obj;*,pem2.exe;*
$ exit 1
