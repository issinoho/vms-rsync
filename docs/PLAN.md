# vms-rsync: plan

## Context

The family has no way to mirror a directory tree incrementally. rsync is the obvious
gap after zip/unzip, and nobody has finished an OpenVMS port. The one attempt (John
Malmberg, rsync 2.5, 2001–04) built but stalled on `fork()`.

User's choices (2026-11-03):
- **Client first:** VMS runs rsync against Linux or other rsync servers.
- **Record-aware text,** as in VFTP.
- **Updates make a new version,** with a keep-limit.

## Feasibility verdict: feasible, on openrsync rather than samba rsync

**Why not samba rsync 3.4.4 (GPLv3):**
- It is built on `fork()` in four places: the remote shell, local copies, the
  receiver's split into receiver and generator, and the daemon (a child per
  connection).
- The receiver/generator split shares the whole file list by copying memory. VMS has
  no `fork`, so removing it would mean re-architecting rsync. Its global state rules
  out threads instead.

**Why openrsync (ISC licence; OpenBSD base; macOS since Sequoia; github.com/kristapsdz/openrsync, active 2026):**
- Its generator and receiver are **one process with a poll() event loop**.
- It forks only to start the remote shell, or a local `--server`.
- It speaks rsync protocol 27, which interoperates with rsync 3.x.
- Master now has the useful options:
  - `-a -r -l -p -t -c -z -H -n -u -v -P` (`-P` is `--partial` plus `--progress`);
  - the `--delete*` family and `--max-delete`;
  - `--filter`, `--include`/`--exclude`[-from], `--link-dest`, `--compare-dest`;
  - `--backup`, `--bwlimit`, `--stats`, `--size-only`, `-W`, `--rsync-path`, `-e`.
- It has no daemon mode, which isn't needed for the client.
- Size: about 15k lines of C.

**What the family already proved on both nodes (vms-lighttpd PHASE0, vms-mariadb PHASE0):**
- `vfork`+`execve` works, with a child's stdout going to a pipe, a socketpair or TCP.
- A **socket works as a child's stdin**.
- `socketpair`, `poll` on sockets, `pread`/`pwrite`, `mkstemp`, `mmap`, `lstat` and
  `realpath` are all present.
- Absent: `fork`, `openat`, `fstatat`, `fchdir` and the other `*at` calls, `timegm`,
  `getrlimit`.
- `poll()` doesn't work on terminals, which rsync doesn't need.

**What has to change in openrsync:**
1. **The two forks:** `fork`+`exec` becomes `vfork`+`execve`. It is already immediately
   followed by exec, with the socketpair end as the child's stdin and stdout.
2. **The `*at` calls**, in about 7 files: a shim that maps directory fds to path
   strings (`openat(dfd, name)` → `open(path(dfd)/name)`).
3. **File I/O through `fmap.c`** (mmap): for record files this goes through a VMS
   "file view" layer (below).
4. **Platform glue:** `compats.c` and the build. No configure: an MMS/DCL build as in
   vms-zstd.

**The VMS-specific risks:**
- **(a) Text conversion inside a delta protocol.** Both the sender's file list and the
  receiver's quick-check use sizes, so a text file must be read to find its LF size.
- **(b) The *at shim's correctness.**
- **(c) VMS I/O speed** on large trees.
- **(d) The ssh transport.**

None of them is a blocker on what is known now. Each gets a probe in Phase 0.

## Design (to be recorded as DECISIONS D0–D9 in Phase 0)

- **D0 Names:**
  - The repo is `vms-rsync`; the command is `rsync`.
  - The PCSI product is `RSYNC`. That is to be checked against VSI's freeware names;
    `VMSRSYNC` is the fallback, as with VMSCURL.
  - Upstream is a pinned openrsync master commit, SHA-256 only, as vms-libxml2 does
    (`upstream.conf`; `tools/fetch.sh` without GPG).
  - The version is "openrsync-<date>-vmsN", with a PCSI T-version, since it's an early test.
- **D1 Base:** openrsync, as a port (upstream + `patches/` + `overlay/vms/`), on
  vms-libssh2's skeleton.
- **D2 Transport:**
  - `rsync://host/module` is plain TCP, with no fork at all.
  - `host:path` runs the `-e` program with a socketpair as its stdin and stdout.
  - The default is a small helper image, `RSYNC_SSH`, built on libssh2. It reuses
    VFTP's SSH code (`src/net/vf_sftp.c`: connect, known_hosts check, key or
    password) and relays between the socket and an exec channel running
    `rsync --server …`.
  - If the Phase 0 probe shows VSI's own `ssh.exe` is binary-clean with a socket as
    stdin, it is offered through `-e ssh`, keeping the user's ssh config.
- **D3 Text files** (the user's choice, record-aware):
  - The VMS file view layer presents each local file as a byte stream:
    - VAR/VFC with carriage control: records as LF lines (VFTP's AUTO rule);
    - stream, UDF and FIX: bytes as they are, with FIX padding taken as VFTP takes it.
  - Its LF size is found by reading the file, once per run, from the sender's list and
    the receiver's quick-check.
  - Receiving, a file whose type is in a text list (`RSYNC$TEXT_TYPES`, defaulting to
    VFTP's list in `src/core/vf_path.c`) is written as VAR records; anything else
    STREAM_LF, byte-exact.
  - Reuse VFTP's line/record code (`src/core/vf_text.[ch]`: `vf_lines_feed`/`vf_lines_end`).
- **D4 Versions** (the user's choice):
  - An updated file arrives as a **new version**: the receiver writes a temp file in
    the destination and renames it over the name, which Phase 0 must confirm makes a
    new version.
  - `--keep-versions=N` (default: keep all) purges beyond N after a successful update.
  - `--delete` removes all versions of a file the source no longer has, and only when
    asked; `--max-delete` is honoured. The log names every file deleted.
- **D5 Names and paths:**
  - An ODS-5 destination is required, and ODS-2 is refused.
  - C RTL feature switches are set at start (grep's `overlay/vms/vms_crtl_init.c`):
    EFS charset, Unix name reports, no version in readdir, drop the dot of a file with
    no type, keep the case of arguments.
  - VMS specs are accepted as arguments (`DISK$USER:[DIR]`) and translated to paths.
  - Names that differ only in case are an error for that file, not a silent merge.
- **D6 Attributes:**
  - `-t` sets the revision date.
  - `-p` maps mode bits to VMS protection.
  - `-o`/`-g` are not supported on VMS (UICs), with a warning.
  - `-l` (symlinks) only if the ODS-5 probe passes; `-H`, devices and specials are
    refused.
- **D7 Safety (family rules):**
  - Nothing is overwritten in place; `--inplace` is refused on VMS.
  - Deletions happen only with `--delete`, `-n` shows them first, and every change is
    logged with `-v`.
  - Tests work in scratch directories at both ends.
- **D8 Scope of release 1:**
  - VMS as client to Linux (rsync 3.4.x and openrsync) over ssh and rsync://.
  - VMS local-to-local copies.
  - Server-side is later: `rsync --server` under VSI sshd, or a daemon as a TCP/IP
    Services auxiliary server.
- **D9 Not ported:** `--daemon`; both ends remote; `-o`/`-g`.

## Phases (stop for review at each exit, as in the family)

**Phase 0: probes and decisions.**
- Probes in `probes/`, ~20 lines each, on both nodes:
  1. `vfork`+`execve` with a socketpair as the child's stdin and stdout: 10 MB of
     binary both ways, `poll` on the parent's end, EOF and exit status.
  2. VSI `ssh.exe -T host cat` with a socket as stdin: is the binary round trip exact?
  3. A libssh2 exec channel running `rsync --server --version` on Linux.
  4. `rename()` of a temp file over an existing name: new version or replace?
     `mkstemp` names with a leading dot on ODS-5.
  5. `utime()` sets the revision date, and `stat` reads it back.
  6. `chmod` mode → protection, and back.
  7. `readdir` with the feature switches: versions, `.DIR;1`, type-less names, case.
  8. Read speed of a VAR file through the C RTL, and the cost of LF-sizing 1,000
     small files.
  9. For later: VSI sshd exec of a C program, checking stdin and stdout are
     binary-clean and argument case survives.
  10. `symlink()`/`readlink()` on ODS-5.
- Also write `docs/PLAN.md`, `DECISIONS.md` (D0–D9) and `NOTES.md`, and fetch and pin
  openrsync.
- **Exit:** the probes are answered and the user agrees to D0–D9.
- **Status:** done. All ten probes answered (NOTES N1–N3), openrsync pinned (N4).
  Probe 9 moved VMS-to-VMS over ssh out of release 1 (D8).
  Exit: signed off by the user, 2026-11-05; D0–D8 accepted.

**Phase 1: build and local copies.**
- `patches/` for the forks, the `*at` shim (`overlay/vms/vms_at.c`), `compats` and
  `fmap`.
- `overlay/vms/build.com` and the C RTL feature init.
- openrsync's `regress/cunit` tests, natively, where they don't need a Unix shell.
- `rsync -a` of a stream-file tree VMS→VMS (local), checked by checksums.
- **Exit:** a local tree copies exactly, a second run transfers nothing, and a changed
  file goes by delta.
- **Status 2026-11-05:** done on both nodes (NOTES N5).
  - openrsync builds with 7 patches and the overlay.
  - `tools/local.sh` gives an exact copy, then 0 files on the second run, then a
    delta of 700 bytes sent and 299,300 matched.
  - Fixed on the way: times undone by close/chmod/chown, IA64's mmap, and a hang
    from inherited fds.
  - Not covered: openrsync's regress/cunit tests (they need a Unix shell or
    upstream's harness); stderr under ssh commands; readpassphrase echo.
  - Exit: signed off by the user, 2026-11-05.

**Phase 2: the network.**
- rsync:// against a Linux rsync daemon. The user starts it: the classifier refuses
  listening servers.
- `host:path` through the `RSYNC_SSH` helper (libssh2), against Linux rsync 3.4.x and
  openrsync, for push and pull.
- Exercise `--delete`, `-n`, `-c`, `-z`, `--partial`, `--progress` and `--link-dest`.
- **Exit:** a scripted interop matrix passes on both nodes (`tools/live.sh`, as VFTP's).
- **Status 2026-11-06:** `tools/net.sh` passes on both nodes against Linux rsync
  3.4.1, every file checked by MD5 (NOTES N6):
  - over ssh, push and pull with again (0 files), delta, `--delete` (with `-n`
    first), `-c`, `-z`, `--partial --progress` and `--link-dest`;
  - over rsync://, pull and again; a push is refused.
  - Four more patches (0008-0011), two of them upstream bugs that affect every
    system.
  - Not covered: openrsync as the Linux server (only rsync 3.4.1); daemon
    authentication and rsync:// push (openrsync has neither); messages under an
    ssh command (stderr); `--progress` was run but not checked.
  - Exit: signed off by the user, 2026-11-06.

**Phase 3: VMS semantics.**
- The file view layer (D3), new versions and `--keep-versions` (D4), VMS specs and
  case collisions (D5), and attributes (D6).
- Round trip VMS → Linux → VMS: VAR text comes back as VAR with the same records.
  Binary and stream files are byte-exact. Versions accumulate and purge as specified.
- **Exit:** that matrix passes on both nodes.
- **Status 2026-11-06:** `tools/local.sh` and `tools/net.sh` pass on both nodes
  (NOTES N7):
  - VMS → Linux → VMS: VAR text goes as LF lines and comes back as VAR records;
    FIX, stream and binary byte for byte.
  - `--keep-versions` purges; `--delete` removes every version; VMS specs work as
    operands; a case clash leaves one file and status 23.
  - Attributes from Linux and back: modes, symlinks (and a dangling one), owner
    = the user; a fifo left out (status 23); `-H` refused.
  - Three more patches (0013-0015).
  - Not covered: a VMS sender's own symlinks and protection other than by
    mode; text files over 32,767-byte lines in a round trip (D3a keeps them
    stream); large trees for speed.

**Phase 4: kit and release.**
- PCSI kits:
  - `RSYNC$SETUP.COM` defines `rsync` as a foreign command (DCL$PATH-friendly);
  - `RSYNC$STARTUP.COM`;
  - a README.VMS with the case-quoting note (`"-P"`, or `SET PROCESS/PARSE_STYLE=EXTENDED`).
- The family release process: banner and badges, the release, the site entry, and
  SYSTARTUP/SYLOGIN in vms-site-config.

**Later:** VMS as server: `rsync --server` under VSI sshd if probe 9 passes; otherwise
a daemon as an auxiliary server, with `@RSYNCD:` greeting and modules.

## Reuse
- **Skeleton:** `~/projects/vms-libssh2`:
  - `tools/prepare.sh`, `build.sh`, `kit.sh` and `vms.sh`;
  - `overlay/vms/kit/*` (PCSI templates);
  - `fetch.sh` in vms-libxml2's checksum-only form.
- **SSH:** `vms-vftp/src/net/vf_sftp.c` (libssh2 session, known_hosts, auth), linked
  against the vms-libssh2 kit (`LIBSSH2$ROOT`).
- **Text:**
  - `vms-vftp/src/core/vf_text.[ch]` (line↔record);
  - the AUTO rule and the text-type list in `vms-vftp/src/core/vf_path.c`.
- **C RTL init:** `vms-grep/overlay/vms/vms_crtl_init.c`.
- **Live tests:** `vms-vftp/tools/live_unix.sh` style, with the user starting the Linux
  sshd and rsync daemon; `tools/node_lock.sh`.

## Verification
- **Phase 0:** each probe's output recorded in `docs/NOTES.md`, with both nodes and the
  method.
- **Phases 1–3:**
  - Checksum (MD5 or SHA-256) comparison of every transferred file at both ends.
  - A second run must report no transfers (quick-check stable).
  - A delta run must send fewer bytes than the file (`--stats`).
  - Record checks with `DUMP/RECORD` or `ANALYZE/RMS` for VAR round trips.
  - The interop matrix: {x86, IA64} × {push, pull} × {rsync 3.4.x, openrsync} ×
    {ssh, rsync://} × {stream, VAR text, FIX binary}.
- **Phase 4:** kit install, run the matrix from the installed image, remove; the
  family's release checks.

## Effort and risk
- **Effort:** comparable to vms-findutils plus VFTP's transfer layer: the port itself
  is moderate, the VMS file view layer is the real work.
- **Main risks:**
  - the cost of reading text files for sizing on large trees (mitigation later: a size
    cache keyed by file ID and revision date);
  - *at shim bugs;
  - protocol-27 corner cases with rsync 3.4.x.
- **Kill criteria in Phase 0:** a socketpair child can't carry binary data both ways,
  or `rename` can't produce a new version without data loss.
