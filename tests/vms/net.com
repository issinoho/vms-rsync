$! net.com - Phase 2: rsync on this node against Linux rsync 3.4.1, over ssh
$! (VSI's ssh client as rsync's -e program) and rsync://. Run by tools/net.sh,
$! which starts the servers and checks the Linux side.
$!   P1 host  P2 ssh port  P3 user  P4 daemon port  P5 the servers' directory
$! The test key is [.NET]NET_KEY. (deleted at the end); the scratch tree is
$! [.NT] (made empty first, deleted at the end). Prints NET: and NETMD5 lines.
$ set noon
$ say = "write sys$output"
$ rsync := $'f$parse("[.openrsync.vms]rsync.exe")'
$ mk := $'f$parse("mkfiles.exe")'
$ key = f$parse("[.net]net_key.")
$ set security/protection=(s:rwed,o:rwed,g,w) 'key'
$ if f$search("nt.dir") .nes. "" then call wipe nt
$ create/directory [.nt]
$ mk make nt/src
$ e = "ssh -p ''p2' -i ''key' -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=NLA0:"
$ r = p3 + "@" + p1 + ":" + p5
$ d = "rsync://" + p1 + ":" + p4 + "/m"
$!
$! ---- over ssh
$ call run s1 "-a --stats -e ""''e'"" nt/src/ ''r'/push/"
$ call run s2 "-a --stats -e ""''e'"" nt/src/ ''r'/push/"
$ mk change nt/src
$ call run s3 "-a --stats -e ""''e'"" nt/src/ ''r'/push/"
$ call run s4 "-a --stats -e ""''e'"" ''r'/lsrc/ nt/pull/"
$ call run s5 "-a --stats -e ""''e'"" ''r'/lsrc/ nt/pull/"
$ call md5s pull pull
$ delete/nolog [.nt.src.sub.deeper]c.txt;*
$ call run s6 "-a -n -v --delete -e ""''e'"" nt/src/ ''r'/push/"
$ call run s7 "-a --stats --delete -e ""''e'"" nt/src/ ''r'/push/"
$ call run s8 "-a -c --stats -e ""''e'"" nt/src/ ''r'/push/"
$ call run s9 "-a -z --stats -e ""''e'"" ''r'/lsrc/ nt/pullz/"
$ call md5s pullz pullz
$ call run s10 "-a --partial --progress -e ""''e'"" nt/src/ ''r'/push2/"
$ call run s11 "-a --stats --link-dest=../pull -e ""''e'"" ''r'/lsrc/ nt/pull2/"
$ call md5s pull2 pull2
$!
$! ---- over rsync://
$ call run d1 "-a --stats nt/src/ ''d'/push/"
$ call run d2 "-a --stats nt/src/ ''d'/push/"
$ call run d3 "-a --stats ''d'/push/ nt/dpull/"
$ call md5s dpull dpull
$!
$ call md5s src src
$ delete/nolog 'key';*
$ set security/protection=o:rwed net.dir;1
$ delete/nolog net.dir;1
$ call wipe nt
$ say "NET-DONE"
$ exit 1
$!
$run: subroutine
$ set noon
$ define/user sys$output [.nt]'p1'.out
$ define/user sys$error [.nt]'p1'.err
$ rsync 'p2'
$ st = $status
$ say "NET: ", p1, " status ", st
$ type [.nt]'p1'.out,[.nt]'p1'.err
$ endsubroutine
$!
$! NETMD5 <tree> <relative path> <md5> for every file under [.NT.<p2>]
$md5s: subroutine
$ set noon
$ top = "[.NT." + p2 + "...]*.*;0"
$ base = f$parse("[.NT." + p2 + "]",,,"DIRECTORY") - "]"
$mloop:
$ f = f$search(top, 4)
$ if f .eqs. "" then exit 1
$ if f$parse(f,,,"TYPE") .eqs. ".DIR" then goto mloop
$ dir = f$parse(f,,,"DIRECTORY") - "]"
$ rel = dir - base
$ if rel .nes. "" then rel = f$extract(1, 999, rel) + "."
$ name = f$parse(f,,,"NAME") + f$parse(f,,,"TYPE")
$ checksum/algorithm=md5 'f'
$ m = "(none)"
$ if $status then m = checksum$checksum
$ say "NETMD5 ", p1, " ", rel, name, " ", m
$ goto mloop
$ endsubroutine
$!
$wipe: subroutine
$ set noon
$ set security/protection=o:rwed [.'p1'...]*.*;*
$ i = 0
$wl: i = i + 1
$ delete/nolog [.'p1'...]*.*;*
$ if i .lt. 6 then goto wl
$ set security/protection=o:rwed 'p1'.dir;1
$ delete/nolog 'p1'.dir;1
$ endsubroutine
