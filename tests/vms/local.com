$! local.com - Phase 1 exit test: rsync -a of a stream-file tree, VMS to VMS
$! on one node, in [.LT] (made empty first, deleted at the end). rsync's
$! stdout and stderr go to files, since under an ssh command stderr is lost
$! (NOTES N1). Prints LOCAL: lines for tools/local.sh.
$ set noon
$ say = "write sys$output"
$ here = f$environment("DEFAULT")
$ rsync := $'f$parse("[.openrsync.vms]rsync.exe")'
$ cc/OBJECT=mkfiles.obj [.tests.vms]mkfiles.c
$ link/EXECUTABLE=mkfiles.exe mkfiles.obj
$ mk := $'f$parse("mkfiles.exe")'
$ if f$search("lt.dir") .nes. "" then call wipe
$ create/directory [.lt]
$ mk make lt/src
$ call run 1 "-a --stats lt/src/ lt/dst/"
$ call compare
$ call run 2 "-a --stats lt/src/ lt/dst/"
$ mk change lt/src
$! a local copy is whole-file by default, as rsync's: ask for the delta
$ call run 3 "-a --stats --no-whole-file lt/src/ lt/dst/"
$ call compare
$!
$! ---- Phase 3: versions (D4) and VMS specifications (D5)
$ call versions bin.dat "before run 4"
$ mk change2 lt/src
$ call run 4a "-a lt/src/ lt/dst/"
$ mk change lt/src
$ call run 4 "-a --keep-versions=2 lt/src/ lt/dst/"
$ call versions bin.dat "after --keep-versions=2 (want 2)"
$ call versions bin.dat "before --delete"
$ delete/nolog [.lt.src]bin.dat;*
$ call run 5 "-a --delete lt/src/ lt/dst/"
$ call versions bin.dat "after --delete (want 0)"
$ call run 6 "-a [.lt.src] [.lt.dst2]"
$ call compare dst2
$ directory/size=used/date=modified/protection [.lt...]
$ call wipe
$ say "LOCAL-DONE"
$ exit 1
$!
$run: subroutine
$ set noon
$ define/user sys$output [.lt]run'p1'.out
$ define/user sys$error [.lt]run'p1'.err
$ rsync 'p2'
$ st = $status
$ say "LOCAL: run ", p1, " (", p2, ") status ", st
$ type [.lt]run'p1'.out,[.lt]run'p1'.err
$ endsubroutine
$!
$versions: subroutine
$ set noon
$ n = 0
$vl: if f$search("[.lt.dst]''p1';*", 6) .eqs. "" then goto vd
$ n = n + 1
$ goto vl
$vd: say "LOCAL: VERSIONS ", p1, " ", p2, ": ", n
$ endsubroutine
$!
$compare: subroutine
$ set noon
$ to = "DST"
$ if p1 .nes. "" then to = p1
$ n = 0
$ bad = 0
$cloop:
$ f = f$search("[.lt.src...]*.*;0", 3)
$ if f .eqs. "" then goto cdone
$ if f$parse(f,,,"TYPE") .eqs. ".DIR" then goto cloop
$ i = f$locate(".LT.SRC", f)
$ d = f$extract(0, i, f) + ".LT." + to + f$extract(i + 7, 999, f)
$ d = f$element(0, ";", d)
$ a = "(none)"
$ b = "(none)"
$ ra = f$file_attributes(f, "RFM")
$ rb = "(none)"
$ if f$search(d) .nes. "" then rb = f$file_attributes(d, "RFM")
$ if ra .nes. "STMLF" .or. rb .nes. "STMLF" then -
     say "LOCAL: FORMAT ", f$parse(f,,,"NAME"), f$parse(f,,,"TYPE"), " ", ra, " -> ", rb
$ if ra .eqs. rb .or. rb .eqs. "(none)" then goto by_md5
$! different record formats (D3): the same lines?
$ define/user sys$output nla0:
$ differences 'f' 'd'
$ if $status then a = "same lines"
$ b = a
$ goto compared
$by_md5:
$ checksum/algorithm=md5 'f'
$ if $status then a = checksum$checksum
$ checksum/algorithm=md5 'd'
$ if $status then b = checksum$checksum
$compared:
$ n = n + 1
$ if a .nes. b .or. b .eqs. "(none)"
$ then
$   bad = bad + 1
$   say "LOCAL: DIFFERS ", f$parse(f,,,"NAME"), f$parse(f,,,"TYPE"), " ", a, " ", b
$ endif
$ goto cloop
$cdone:
$ say "LOCAL: compared ", n, " files with ", to, ", ", bad, " differ"
$ endsubroutine
$!
$wipe: subroutine
$ set noon
$! "*.*" does not match a name with no name part (".profile"): ".*" does
$ set security/protection=o:rwed [.lt...].*;*
$ delete/nolog [.lt...].*;*
$ set security/protection=o:rwed [.lt...]*.*;*
$ delete/nolog [.lt.dst2...]*.*;*
$ delete/nolog [.lt.dst2...]*.*;*
$ delete/nolog [.lt.dst2...]*.*;*
$ set security/protection=o:rwed [.lt]dst2.dir;1
$ delete/nolog [.lt]dst2.dir;1
$ delete/nolog [.lt.src.sub.deeper]*.*;*, [.lt.dst.sub.deeper]*.*;*
$ delete/nolog [.lt.src.sub]*.*;*, [.lt.dst.sub]*.*;*
$ delete/nolog [.lt.src]*.*;*, [.lt.dst]*.*;*
$ delete/nolog [.lt]*.*;*
$ set security/protection=o:rwed lt.dir;1
$ delete/nolog lt.dir;1
$ endsubroutine
