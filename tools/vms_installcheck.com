$! VMS_INSTALLCHECK.COM - install the RSYNC kit from [.OPENRSYNC.KIT_<arch>],
$! verify it, run rsync from it, then remove it. Changes the system while it
$! runs (PCSI database, SYS$COMMON:[RSYNC], system logical RSYNC$ROOT); leaves
$! it as it was. Scratch files in [.IC] (made empty first, deleted at the end).
$ set noon
$ say = "write sys$output"
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ base = "I64VMS"
$ if arch .eqs. "X86_64" then base = "X86VMS"
$ kitdir = f$environment("DEFAULT") - "]" + ".OPENRSYNC.KIT_''arch']"
$ say "=== INSTALL from ", kitdir
$ product install RSYNC /producer=ISSINOHO /base_system='base' /source='kitdir' /options=noconfirm /log
$ say "=== install status ", $status
$ product show product RSYNC /producer=ISSINOHO
$ say "=== VERIFY"
$ say "startup procedure: [", f$search("SYS$STARTUP:RSYNC$STARTUP.COM"), "]"
$ show logical RSYNC$ROOT
$ directory/nohead/notrail RSYNC$ROOT:[000000...]*.*
$ say "=== RSYNC FROM THE INSTALLED KIT"
$ @RSYNC$ROOT:[000000]RSYNC$SETUP.COM
$ define/user sys$error ic_version.txt
$ rsync --version
$ sev = $severity
$ type ic_version.txt
$ found = 0
$ open/read v ic_version.txt
$vl: read/end=vd v line
$ if f$locate("rsync for OpenVMS openrsync-", line) .lt. f$length(line) then found = 1
$ goto vl
$vd: close v
$ delete/nolog ic_version.txt;*
$ call verdict VERSION 'f$string(sev .eq. 1 .and. found)'
$!
$ if f$search("IC.DIR") .nes. "" then call wipe ic
$ create/directory [.ic.src]
$! a VAR text file (CREATE makes variable-length records with CR)
$ create [.ic.src]notes.txt
first line

third line, after an empty one
$ create [.ic.src]other.dat
a text file of a type not in the list
$! a local copy: rsync starts its own image from RSYNC$ROOT as the other end
$ rsync -a ic/src/ ic/dst/
$ sev = $severity
$ rfm = f$file_attributes("[.ic.dst]notes.txt", "RFM")
$ differences/output=nla0: [.ic.src]notes.txt [.ic.dst]notes.txt
$ same = $severity .eq. 1
$ say "local copy: status severity ", sev, ", notes.txt ", rfm, ", same lines ", same
$ call verdict LOCAL_COPY 'f$string(sev .eq. 1 .and. rfm .eqs. "VAR" .and. same)'
$ rfm = f$file_attributes("[.ic.dst]other.dat", "RFM")
$ say "other.dat (not a text type): ", rfm
$ call verdict STREAM_LF 'f$string(rfm .eqs. "STMLF")'
$! RSYNC$TEXT_TYPES replaces the list
$ define/user RSYNC$TEXT_TYPES ".DAT"
$ rsync -a ic/src/ ic/dst2/
$ rfm = f$file_attributes("[.ic.dst2]other.dat", "RFM")
$ say "other.dat with RSYNC$TEXT_TYPES .DAT: ", rfm
$ call verdict TEXT_TYPES 'f$string(rfm .eqs. "VAR")'
$! three updates with --keep-versions=2 leave two versions
$ i = 0
$ul: i = i + 1
$ open/append f [.ic.src]notes.txt
$ write f "update ", i
$ close f
$ rsync -a "--keep-versions=2" ic/src/ ic/dst/
$ if i .lt. 3 then goto ul
$ n = 0
$cl: if f$search("[.ic.dst]notes.txt;*", 3) .eqs. "" then goto cd
$ n = n + 1
$ goto cl
$cd: say "versions of notes.txt after 3 updates with --keep-versions=2: ", n
$ call verdict KEEP_VERSIONS 'f$string(n .eq. 2)'
$! a failed run has error severity, and the code is (status & %X7F8) / 8
$ define/user sys$error nla0:
$ rsync -a ic/nonexistent/ ic/dst3/
$ st = $status
$ sev = st .and. 7
$ code = (st .and. %X7F8) / 8
$ say "failed run status ", st, " severity ", sev, " code ", code
$ call verdict ERROR_SEVERITY 'f$string(sev .eq. 2 .and. code .ne. 0)'
$ call wipe ic
$ delete/symbol/global rsync
$ say "=== REMOVE"
$ product remove RSYNC /producer=ISSINOHO /options=noconfirm /log
$ say "=== remove status ", $status
$ say "RSYNC$ROOT after removal: [", f$trnlnm("RSYNC$ROOT"), "]"
$ say "files after removal: [", f$search("SYS$COMMON:[RSYNC...]*.*"), "]"
$ say "startup after removal: [", f$search("SYS$STARTUP:RSYNC$STARTUP.COM"), "]"
$ product show product RSYNC /producer=ISSINOHO
$ exit 1
$!
$verdict: subroutine
$ if p2 then write sys$output "RSYNC_''p1': PASS"
$ if .not. p2 then write sys$output "RSYNC_''p1': FAIL"
$ endsubroutine
$!
$wipe: subroutine
$ set noon
$ set security/protection=o:rwed [.'p1'...]*.*;*
$ i = 0
$wl: i = i + 1
$ delete/nolog [.'p1'...]*.*;*
$ if i .lt. 4 then goto wl
$ set security/protection=o:rwed 'p1'.dir;1
$ delete/nolog 'p1'.dir;1
$ endsubroutine
