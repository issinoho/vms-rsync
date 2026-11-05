$! features.com - answer openrsync's oconfigure tests natively: compile and link
$! [.FEAT]TESTS.C (upstream's) once per feature with /DEFINE=TEST_<name>, as
$! `configure` does with cc on Unix, and print FEATURE <name> 1|0. The qualifiers
$! are the port's (BUILD.COM). tools/features.sh turns the answers into
$! overlay/vms/configure.local.
$ set noon
$ list = "ARC4RANDOM,BLOWFISH,B64_NTOP,CAPSICUM,CRYPT,CRYPT_NEWHASH,ENDIAN_H,ERR," + -
         "EXPLICIT_BZERO,FTS,GETEXECNAME,GETPROGNAME,INFTIM,LANDLOCK,LIB_SOCKET,MD5," + -
         "MEMMEM,MEMRCHR,MEMSET_S,MKFIFOAT,MKNODAT,OSBYTEORDER_H,PASSWORD_LEN,PATH_MAX," + -
         "PLEDGE,PROGRAM_INVOCATION_SHORT_NAME,READPASSPHRASE,REALLOCARRAY," + -
         "RECALLOCARRAY,SANDBOX_INIT,SCAN_SCALED,SECCOMP_FILTER,SETRESGID,SETRESUID," + -
         "SHA2,SOCK_NONBLOCK,STRLCAT,STRLCPY,STRNDUP,STRNLEN,STRTONUM," + -
         "SYS_BYTEORDER_H,SYS_CDEFS_H,SYS_ENDIAN_H,SYS_MKDEV_H,SYS_SYSMACROS_H," + -
         "SYS_QUEUE,SYS_TREE,TERMIOS,TIMINGSAFE_BCMP,UNVEIL,WAIT_ANY,__PROGNAME"
$ i = 0
$loop:
$ t = f$element(i, ",", list)
$ if t .eqs. "," then goto done
$ define/user sys$output nla0:
$ define/user sys$error nla0:
$ cc/NAMES=(AS_IS,SHORTENED)/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/WARNINGS=(ERRORS=IMPLICITFUNC) -
     /DEFINE=(_USE_STD_STAT,_LARGEFILE,_POSIX_EXIT,_SOCKADDR_LEN,TEST_'t') -
     /NOLIST/OBJECT=[.FEAT]T.OBJ [.FEAT]TESTS.C
$ ok = $severity .eq. 1
$ if ok
$ then
$   define/user sys$output nla0:
$   define/user sys$error nla0:
$   link/EXECUTABLE=[.FEAT]T.EXE [.FEAT]T.OBJ
$   ok = $severity .eq. 1
$ endif
$ v = 0
$ if ok then v = 1
$ write sys$output "FEATURE ", t, " ", v
$ if f$search("[.FEAT]T.OBJ") .nes. "" then delete/nolog [.FEAT]T.OBJ;*
$ if f$search("[.FEAT]T.EXE") .nes. "" then delete/nolog [.FEAT]T.EXE;*
$ i = i + 1
$ goto loop
$done:
$ write sys$output "FEATURES-DONE"
$ exit 1
