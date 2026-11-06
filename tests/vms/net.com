$! net.com - Phase 2: rsync on this node against Linux rsync 3.4.1, over ssh
$! (VSI's ssh client as rsync's -e program) and rsync://. Run by tools/net.sh,
$! which starts the servers and checks the Linux side.
$!   P1 host  P2 ssh port  P3 user  P4 daemon port  P5 the servers' directory
$! The test key is [.NET]NET_KEY. (deleted at the end); the scratch tree is
$! [.NT] (made empty first, deleted at the end). Prints NET: and NETMD5 lines.
$ set noon
$ say = "write sys$output"
$ rsync := $'f$parse("[.openrsync.vms]rsync.exe")'
$! the test tree's maker, built here so it is always the current one
$ cc/OBJECT=mkfiles.obj [.tests.vms]mkfiles.c
$ link/EXECUTABLE=mkfiles.exe mkfiles.obj
$ mk := $'f$parse("mkfiles.exe")'
$ if f$search("nt.dir") .nes. "" then call wipe nt
$ create/directory [.nt]
$! md5tree, with openrsync's compat MD5 from the built objects
$ library/create/object [.nt]rsynclib.olb
$ ctx = ""
$lib: o = f$search("[.openrsync.vms.obj]*.obj", 5)
$ if o .eqs. "" then goto libdone
$ if f$parse(o,,,"NAME") .nes. "MAIN" then library/insert [.nt]rsynclib.olb 'o'
$ goto lib
$libdone:
$ cc/NAMES=(AS_IS,SHORTENED)/OBJECT=[.nt]md5tree.obj [.tests.vms]md5tree.c
$ link/EXECUTABLE=[.nt]md5tree.exe [.nt]md5tree.obj,[.nt]rsynclib.olb/library,[.openrsync.vms.obj]vms_crtl_init.obj
$ md5tree := $'f$parse("[.nt]md5tree.exe")'
$ key = f$parse("[.net]net_key.")
$ set security/protection=(s:rwed,o:rwed,g,w) 'key'
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
$ define/process RSYNC_VMS_TRACE 1
$! Phase 3: the pushed tree pulled back (D3: records again for text types)
$ call run s12 "-a --stats -e ""''e'"" ''r'/push/ nt/back/"
$ call md5s "back" back
$ call md5s "src3" src
$ call rfms "back" back
$! names that differ only in case: one written, the other refused (D5)
$ call run s13 "-a -e ""''e'"" ''r'/lcase/ nt/case/"
$ call md5s "case" case
$ call run s4 "-a -vvvv --stats -e ""''e'"" ''r'/lsrc/ nt/pull/"
$ deassign/process RSYNC_VMS_TRACE
$ call run s5 "-a --stats -e ""''e'"" ''r'/lsrc/ nt/pull/"
$ call md5s "pull" pull
$ delete/nolog [.nt.src.sub.deeper]c.txt;*
$ call run s6 "-a -n -v --delete -e ""''e'"" nt/src/ ''r'/push/"
$ call run s7 "-a --stats --delete -e ""''e'"" nt/src/ ''r'/push/"
$ call run s8 "-a -c --stats -e ""''e'"" nt/src/ ''r'/push/"
$ call run s9 "-a -z --stats -e ""''e'"" ''r'/lsrc/ nt/pullz/"
$ call md5s "pullz" pullz
$ call run s10 "-a --partial --progress -e ""''e'"" nt/src/ ''r'/push2/"
$ call run s11 "-a --stats --link-dest=../pull -e ""''e'"" ''r'/lsrc/ nt/pull2/"
$ call md5s "pull2" pull2
$!
$! ---- over rsync://
$ call run d1 "-a --stats ''d'/lsrc/ nt/dpull/"
$ call run d2 "-a --stats ''d'/lsrc/ nt/dpull/"
$! openrsync pulls from a daemon only: a push must fail cleanly
$ call run d3 "-a nt/src/ ''d'/push/"
$ call md5s "dpull" dpull
$!
$ call md5s "src" src
$! VSI ssh's own exit status for a remote command that ends 0 and one that ends 1
$! (the gate's exit-status commands); -n: ssh must not wait on its own input
$ ssh_img = "SSH$ROOT:[BIN.''f$edit(f$getsyi("ARCH_NAME"),"UPCASE")']SSH$SSH.EXE"
$ vssh := $'ssh_img'
$ define/user sys$output nla0:
$ define/user sys$error nla0:
$ vssh -n -p 'p2' -i 'key' -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=NLA0: 'p3'@'p1' "exit-status 0"
$ say "NET: ssh exit for a remote 0: ", $status
$ define/user sys$output nla0:
$ define/user sys$error nla0:
$ vssh -n -p 'p2' -i 'key' -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=NLA0: 'p3'@'p1' "exit-status 1"
$ say "NET: ssh exit for a remote 1: ", $status
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
$! every version: rsync's child may write its own version of the stderr file
$ type [.nt]'p1'.out;*,[.nt]'p1'.err;*
$ endsubroutine
$!
$! NETMD5 <tree> <relative path> <md5> for every file under nt/<p2> (md5tree.c)
$md5s: subroutine
$ set noon
$ md5tree 'p1' nt/'p2'
$ endsubroutine
$!
$! NETRFM <tree> <name> <record format> for every file in [.NT.<p2>]
$rfms: subroutine
$ set noon
$rl: f = f$search("[.NT.''p2']*.*;0", 7)
$ if f .eqs. "" then exit 1
$ if f$parse(f,,,"TYPE") .eqs. ".DIR" then goto rl
$ say "NETRFM ", p1, " ", f$parse(f,,,"NAME"), f$parse(f,,,"TYPE"), " ", f$file_attributes(f, "RFM")
$ goto rl
$ endsubroutine
$!
$wipe: subroutine
$ set noon
$! "*.*" does not match a name with no name part (".hidden"): ".*" does
$ set security/protection=o:rwed [.'p1'...].*;*
$ delete/nolog [.'p1'...].*;*
$ set security/protection=o:rwed [.'p1'...]*.*;*
$ i = 0
$wl: i = i + 1
$ delete/nolog [.'p1'...]*.*;*
$ if i .lt. 6 then goto wl
$ set security/protection=o:rwed 'p1'.dir;1
$ delete/nolog 'p1'.dir;1
$ endsubroutine
