# vms-rsync: notes

What was observed on the nodes, and how. x86-64 is E9.2-4 (VSI C 7.7), IA64 is
V8.4-2L3 (VSI C 7.4); both work directories are on ODS-5 disks (F11V5).

## N1. Phase 0 probes, 2026-11-03 (probes/, run by tools/vms.sh)

The C RTL feature switches rsync would set were defined as process logical names for
probes 4-8 and 10 (`probes/probes.com`): EFS_CHARSET, FILENAME_UNIX_REPORT,
FILENAME_UNIX_NO_VERSION, READDIR_DROPDOTNOTYPE, EFS_CASE_PRESERVE,
EFS_FILE_TIMESTAMPS, POSIX_SEEK_STREAM_FILE. Results are the same on both nodes
unless noted.

**Probe 1, the transport (`probe_pair.c`).**
- **It works:** a parent makes a socketpair and starts itself with `vfork`+`execv`. The
  child gets the pair's other end as **both stdin and stdout**
  (`decc$set_child_standard_streams`), and echoes.
- 10 MB of every byte value went through and came back identical, with `poll` on
  the parent's end and a half-close (`shutdown(SHUT_WR)`). The child saw EOF and
  exited with its own status (3). The same held with two socketpairs (one each way)
  and with a `dup`.
- **The catch:** the child's **stderr must be given explicitly**. With -1 (inherit),
  the child never starts: `%DCL-E-OPENOUT, error opening _X86VMS$DKA0:[...].DAT as
  output`, exit 74. That happens when the parent runs under an ssh command
  (`vms.sh run`, mode NETWORK), where SYS$ERROR translates to a bare device
  (`_X86VMS$DKA0:`, `_MYI64$DKA800:`). With the null device (`NLA0:`) or a pipe
  that the parent relays, it works in that mode on both nodes.
- In batch (`vms.sh batch`), vms-lighttpd's `r_spawn.c` passes as it did in October:
  stdout to a pipe, a socketpair or TCP; a socket as stdin; about 84 ms per spawn
  on x86.
- **Rule for the port:** every child gets an explicit stderr, a pipe the parent copies
  to its own stderr.
- Probe hygiene: a child whose stdin is inherited and reads it blocks forever (it
  reads the session's input). Two probe runs hung that way and their processes
  were stopped (`STOP/ID`, ours only). `probe_streams.c` and `probe_pair.c` now
  never read an inherited stdin and never block in `waitpid` (WNOHANG, 10 s).

**Probe 2, VSI's ssh client as the `-e` program (`probe_pair.c` ssh mode).**
- `SSH$ROOT:[BIN.<arch>]SSH$SSH.EXE` started as a `vfork` child, with the socket as
  stdin and stdout and a pipe as stderr. It connected to the node's own sshd and
  reported through the pipe.
- Login failed, `Permission denied (publickey,password,keyboard-interactive)` in
  batch mode: there is no key for logging in to yourself. So the binary round trip
  through the ssh client and sshd (probe 9) is still untested.

**Probe 4: rename over an existing name.**
- `mkstemp(".dest.txt.XXXXXX")` made `.dest.txt.0146f3`.
- `rename()` of it onto `dest.txt` gave **a new version**: `dest.txt;2` has the new
  contents and `dest.txt;1` keeps the old. This is D4's behaviour, with no code.
- The new version has mkstemp's protection (RWD,RWD,,), so rsync must set the
  protection after the rename (or from `-p`).
- With FILENAME_UNIX_NO_VERSION, `fopen("dest.txt;1")` fails: an older version
  must be named in VMS form.

**Probe 5:** `utime()` set the modification date to 2-JAN-2020 03:04:05 (DCL's
"Modified" shows it), and `stat` read back the same `st_mtime`.

**Probe 6:** `chmod(0640)` and `chmod(0755)` both read back exactly through `stat`.
DCL shows 0755 as (RWED,RWED,RE,RE).

**Probe 7: readdir.**
- It lists the **highest version only** (`two.txt` once, with the size of `;2`), and
  `README` with no dot.
- Case is kept (`Mixed.Case.txt`, which DCL shows as `Mixed^.Case.txt`).
- A directory is a plain name (`subdir`), with `stat` saying S_ISDIR.

**Probe 8: sizing VMS text files (`probe_var.c`).**
- A 100,000-record VAR file (rat=cr): `st_size` 6,379,800, but **6,188,890 bytes**
  read through the C RTL as LF lines. The size on disk counts record overhead.
- Reading it took 1.26 s on x86 (4.9 MB/s; a KVM guest) and 0.33 s on IA64
  (19 MB/s).
- 1,000 small VAR files LF-sized: 4.44 s on x86, against **4.18 s for `stat` alone**;
  0.58 s against 0.50 s on IA64. Reading a text file for its LF size adds little to
  the `stat` rsync does anyway.

**Probe 10:** `symlink()`, `readlink()` and `lstat` (S_ISLNK) all work on ODS-5; DCL
shows `link.txt;1 -> target.txt`. Opening the link opens the target.

**Still to do:**
- Probe 3, a libssh2 exec channel to Linux `rsync --server`.
- Probe 9, a binary round trip through VSI sshd: it needs a key for logging in to
  yourself, or a Linux sshd.

## N2. Probe 9: a program under VSI sshd is not binary-clean, 2026-11-05 (tools/probe9.sh)

- **Method:** from Linux, `ssh host "mcr <work dir>probe_pair.exe -Child"` with the
  usual key. That is how Linux rsync would start `rsync --server` on VMS. 1 KB and
  10 MB of every byte value went in on stdin, and stdout was compared with them.
  The user ran it: the permission check refuses a raw ssh command from the session.
- **The data is mangled on both nodes.** The command runs on a terminal device, not
  a pipe:
  - IA64, 10 MB: the reply began `CR LF *OUTPUT OFF* CR LF` (byte 0x0F taken as
    Ctrl/O), then stalled until the 120 s limit (probably 0x13, Ctrl/S).
  - x86, 10 MB: 484,706 bytes came back, starting `#`, CR LF, and lines padded with
    spaces: terminal formatting.
  - 1 KB: nothing came back on either node.
  - The child's stderr report (how `-Child` arrived) didn't come back at all.
- **Consequence for the server side (later than release 1):** Linux rsync cannot
  run `rsync --server` on VMS over VSI sshd as it stands. VFTP's SFTP works over the
  same sshd because the sftp subsystem gets a binary channel. The routes left:
  - an rsync daemon as a TCP/IP Services auxiliary server (rsync://, optionally
    through an ssh tunnel);
  - an sshd `Subsystem` entry, which Linux rsync would need a wrapper to reach.
- **No effect on release 1 (VMS as the client):** there, the remote end is Linux sshd
  with pipes, and our side's ssh client has a socket, not a terminal (N1 probe 2).
  Probe 2's binary round trip against a Linux sshd is still to run.
