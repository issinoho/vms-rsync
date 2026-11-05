$! probe23.com - Phase 0 probes 2 and 3 against the test sshd on the Linux machine
$! (tools/probe23.sh starts it, uploads the test key to [.P23] and runs this).
$!   P1 host  P2 port  P3 user  P4 the vms-libssh2 install tree  P5 the server's dir
$! Probe 2: VSI's ssh client as rsync's -e program (probe_pair ssh mode), 10 MB
$! through "cat". Probe 3: libssh2 in process (probe_ssh2): the same, then
$! rsync --server's protocol version. The key and [.P23] are deleted at the end.
$ set noon
$ here = f$environment("DEFAULT")
$ key = here - "]" + ".P23]P23_KEY."
$ set security/protection=(s:rwed,o:rwed,g,w) 'key'
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ ssh = "SSH$ROOT:[BIN.''arch']SSH$SSH.EXE"
$ cc/STANDARD=C99/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/OBJECT=probe_pair.obj [.probes]probe_pair.c
$ link/EXECUTABLE=probe_pair.exe probe_pair.obj
$ write sys$output "=== PROBE 2 (VSI ssh, ", arch, ")"
$ mcr sys$disk:[]probe_pair.exe 10 ssh 'ssh' 'p3'@'p1' 'p2' cat 'key'
$ write sys$output "=== PROBE 3 (libssh2)"
$ define/process/nolog OPENSSL SSL3$INCLUDE:
$ inc = p4 - "]" + ".INCLUDE]"
$ opt = p4 - "]" + ".LIB]LIBSSH2.OPT"
$ cc/NAMES=(AS_IS,SHORTENED)/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES -
     /DEFINE=(_USE_STD_STAT=1,_LARGEFILE)/INCLUDE=('inc')/OBJECT=probe_ssh2.obj [.probes]probe_ssh2.c
$ link/EXECUTABLE=probe_ssh2.exe probe_ssh2.obj,'opt'/options
$ write sys$output "link status ", $status
$ mcr sys$disk:[]probe_ssh2.exe 'p1' 'p2' 'p3' 'key' 'p5' 10
$ deassign/process OPENSSL
$ delete/nolog 'key';*
$ set security/protection=o:rwed p23.dir;1
$ delete/nolog p23.dir;1
$ if f$search("pair_back.bin") .nes. "" then delete/nolog pair_back.bin;*
$ exit 1
