$! probe_exit.com - $STATUS after exit(n) under /MAIN=POSIX_EXIT, for rsync's codes
$ set noon
$ cc/MAIN=POSIX_EXIT/DEFINE=_POSIX_EXIT/OBJECT=probe_exit.obj [.probes]probe_exit.c
$ link/EXECUTABLE=probe_exit.exe probe_exit.obj
$ pe := $'f$parse("probe_exit.exe")'
$ n = "0,1,2,5,10,11,12,20,23,24,25,30,35"
$ i = 0
$loop: c = f$element(i, ",", n)
$ if c .eqs. "," then goto done
$ pe 'c'
$ st = $status
$ sev = st .and. 7
$ write sys$output "EXIT ", c, " status ", st, " severity ", sev
$ i = i + 1
$ goto loop
$done:
$ delete/nolog probe_exit.obj;*,probe_exit.exe;*
$ exit 1
