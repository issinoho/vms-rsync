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

## N3. Probes 2 and 3: the ssh transport to Linux works both ways, 2026-11-05 (tools/probe23.sh)

- **Method:** the user ran `tools/probe23.sh`.
  - It started a private sshd on the Linux machine: a fresh test key, accepted only
    from the nodes, and every command through `tools/probe_gate.sh`, which allows
    only `cat` and one `rsync --server` command.
  - On each node it ran `probes/probe23.com`, then deleted the key and `[.P23]`
    (checked afterwards).
  - The users' `known_hosts` files were not touched (`UserKnownHostsFile=NLA0:`):
    no line for the test port on either node.
- **Probe 2, VSI's ssh client as rsync's `-e` program:** `SSH$SSH.EXE`, started by
  `vfork`+`execv` with one socketpair end as stdin and stdout and a pipe as stderr.
  10 MB of every byte value through Linux `cat` came back **identical**, on both
  nodes. The client's exit status arrived as 27 (`WEXITSTATUS`) for a remote
  `cat` that ended 0. VMS status mapping is to be understood before rsync trusts it
  (Phase 2).
- **Probe 3, libssh2 1.11.1 in process** (the vms-libssh2 trees, as VFTP links it):
  - handshake curve25519-sha256 with chacha20-poly1305, and key login;
  - 10 MB through `cat`, written and read non-blocking with `poll` on the socket:
    **identical**, exit status 0;
  - `rsync --server --sender -r . <dir>/` (Linux rsync 3.4.1) sent its first 4
    bytes: **protocol 32**.
- **So both transports in D2 work for release 1.** VSI's ssh keeps the user's own
  ssh configuration, keys and known_hosts. libssh2 needs no external program.

## N4. openrsync pinned, 2026-11-05

- Pinned commit `8235c68` (master, 2026-07-30), archive SHA-256 in `upstream.conf`.
  It is 28,945 lines of C and headers, `compats.c` and the tests included.
- **What VMS lacks**, counted outside `compats.c`:
  - `fork` (5 calls) and `execvp`, all in one file;
  - the `*at` family, about 150 calls in 8 files: `openat` 23, `fstatat` 30,
    `unlinkat` 35, plus `mkdirat`, `renameat`, `symlinkat`, `readlinkat`, `linkat`,
    `fchmodat`, `fchownat`, `utimensat`, `futimens`, `mkstempat`, `mknodat` and
    `faccessat`;
  - `mmap` (7 calls in 4 files).
- `pledge` and `unveil` (53 calls) are OpenBSD-only and compiled out elsewhere
  already.
- **The `*at` calls point to a shim library, not per-call patches.** It needs
  directory fds, and VMS can't `open()` a directory (vms-mariadb's probes), so a
  directory "fd" will be a stand-in registered with its path.

## N5. Phase 1: openrsync built, and local copies, 2026-11-05 (tools/build.sh, tools/local.sh)

**Building.**
- `config.h` is openrsync's own `configure`, run on Linux with answers measured on
  the nodes:
  - `tools/features.sh` compiles and links upstream's `tests.c` once per feature on
    each node, as `configure` does. Both nodes agree: 7 of 53 present (crypt,
    INFTIM, PATH_MAX, strndup, strnlen, termios, the socket library).
  - Overrides with their reasons are in `overlay/vms/configure.overrides`.
- **Not `/STANDARD=C99`:** it defines `_ANSI_C_SOURCE`, which hides `strnlen`,
  `PATH_MAX` and other POSIX declarations, so a first measurement found 3 features.
  The VSI C default mode accepts openrsync's C99 and declares them. Implicitly
  declared functions are errors, as in the family's GNU ports.
- **VSI C rejects:**
  - GNU `##__VA_ARGS__` (patch 0003);
  - indexing a `void *` (0005).
- **VSI C warns about** (allowed, with the reason, in `BUILD.COM`):
  - arithmetic on a `void *`, which it treats as `char *`, as GNU C does;
  - `%zu` on IA64 (VSI C 7.4). IA64's C RTL prints `%zu`, `%zd` and `%5zu`
    correctly (checked with a test program).
- **Missing, so supplied in `overlay/vms`:**
  - `<fts.h>`, `<getopt.h>` (a `getopt_long` with GNU argument reordering, checked
    on Linux), `<search.h>` (`hsearch`), `<syslog.h>` and `<paths.h>`;
  - `getentropy` (not for secrets: it seeds only temp names and a server's
    checksum seed), `dirfd`, `major`/`minor`, `timersub`, `roundup`, `MAX`,
    `MAXPATHLEN`, `IFTODT`, `__progname`;
  - termios stubs. readpassphrase cannot turn echo off: Phase 2.
- **The *at() shim** (`vms_at.c`): ~150 calls run over paths, with directory "fds"
  that are null-device fds registered with an absolute path. `open` and `close` go
  through it via macros in `vms_config.h`. `RSYNC_VMS_TRACE` traces every call.

**Running.**
- **stderr:** under `vms.sh` (an ssh command) stderr is a bare device, so rsync's
  messages (and `--version`) vanish. The tests send stdout and stderr to files.
- **Local copies start the server through `vms_spawn`**, unchanged from upstream's
  protocol: it starts its own image as `rsync --server`.
- **Times (`-t`) needed three fixes,** found with the trace:
  1. Closing a written file sets its revision date. The shim tracks files opened
     for writing (following renames) and sets their times again after `close`.
  2. `chown` and `chmod` rewrite the file header and also set the revision date.
     openrsync sets times first, then owner and mode, so the shim sets the last
     times again after either.
  3. Header changes need a CLEAN build: `BUILD.COM` recompiles only sources newer
     than their objects, which left `close` unwrapped in `downloader.c` for one
     run.
- **The receiver's first open of a destination that does not exist yet** failed
  as it should here. In one single-file run it created an empty `;1` before the
  real `;2`. To check in Phase 3, with the version rules.

**IA64 differs in two ways:**
- **`mmap()` of a file fails** ("not enough core"). Patch 0007 reads the file into
  memory instead (`pread`, the fmap's unused `buf`). Large files are bounded by
  memory there; Phase 3's file view replaces this.
- **That error hung both processes** (stopped by hand). A `vfork`+`exec` child
  inherits every fd, including the parent's end of the socketpair, which upstream's
  `fork` child closes. Holding both ends, the child never sees end-of-file when the
  parent gives up.
  - `probes/probe_cloexec.c`, on both nodes: without `FD_CLOEXEC` the child is
    still waiting after 10 s; with it, the child sees EOF and exits.
  - `vms_spawn` now marks every other fd close-on-exec before the `vfork`.
  - After the fix, an unreadable source file (all access denied, privileges
    dropped) gave a warning and rsync's partial-transfer status 23 on both nodes,
    with no hang.

**`tools/local.sh` (tests/vms/local.com), x86 and IA64, identical results.** A scratch tree of stream-LF text,
a 300 KB binary, an empty file and two levels of subdirectory:
- run 1 `rsync -a`: 5 files, each identical by MD5;
- run 2: **0 files transferred** (quick check stable);
- run 3 after 100 bytes changed in the binary, `--no-whole-file` (a local copy is
  whole-file by default, as rsync's): 1 file, **700 bytes sent, 299,300 matched**,
  all identical.
- Each update is a new version (D4), as probe 4 predicted.
