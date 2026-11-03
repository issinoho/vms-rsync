# vms-rsync: decisions

Each decision with the alternatives considered. Status: **proposed** until the Phase 0
exit review.

## D0. Names (proposed)
- Repository `vms-rsync`; command `rsync`.
- PCSI product `RSYNC`, unless that clashes with a VSI or freeware kit, in which case
  `VMSRSYNC` (as VMSCURL).
- Version "openrsync-<date>-vmsN", PCSI T-version while an early test.

## D1. Base: openrsync, not samba rsync (proposed)
- **Chosen:** openrsync (ISC licence; OpenBSD's rsync, also shipped by macOS),
  pinned to a master commit by SHA-256 (no signed releases), as a port: upstream +
  `patches/` + `overlay/vms/`. Its generator and receiver are one process with a
  `poll` loop, and it forks only to start the remote shell or a local `--server`.
- **Rejected: samba rsync 3.4.4 (GPLv3).** It forks in four places, and the
  receiver/generator split shares the file list by fork's memory copy. VMS has no
  `fork`, and its global state rules out threads. The 2001-04 VMS port attempt
  stalled there.
- **Rejected: a clean-room client** (as vms-beszel). The rsync protocol is large and
  thinly documented; openrsync already implements it in about 15k lines.

## D2. Transport (proposed; N1 probes 1-2)
- `rsync://` is a TCP socket, with no child process.
- `host:path` runs the `-e` program by `vfork`+`execve`, with one socketpair end as
  its stdin and stdout and **a pipe as its stderr**, relayed by rsync (N1: an
  inherited stderr fails under ssh commands).
- Default program: VSI's ssh (`SSH$ROOT:[BIN.<arch>]SSH$SSH.EXE`) if probe 9 shows it
  is binary-clean. Otherwise a small libssh2 helper, `RSYNC_SSH`, built from VFTP's
  SSH code.

## D3. Text files: record-aware (user's choice, 2026-11-03)
- Sent: VAR/VFC files with carriage control go as LF lines; anything else goes as
  its bytes.
- The LF size is found by reading the file (N1 probe 8: little more than `stat`).
- Received: a type in `RSYNC$TEXT_TYPES` (VFTP's list by default) is written as VAR
  records; anything else as STREAM_LF, byte-exact.

## D4. Versions: a new version per update (user's choice, 2026-11-03)
- `rename()` over the name already makes a new version (N1 probe 4).
- `--keep-versions=N` purges beyond N after a good update.
- `--delete` removes every version of a file the source no longer has, and only
  when asked.

## D5. Names and paths (proposed)
- ODS-5 destination only.
- C RTL feature switches set before `main` (as vms-grep's `vms_crtl_init.c`).
- VMS specs are accepted as arguments.
- Names that differ only in case are an error for that file.

## D6. Attributes (proposed)
- `-t` sets the revision date (probe 5).
- `-p` maps to protection (probe 6), and rsync always sets protection after the
  rename (probe 4).
- `-l` symlinks work (probe 10).
- `-o`/`-g` are not supported (UICs).
- `-H`, devices and specials are refused.

## D7. Safety (proposed)
- No in-place writes (`--inplace` refused).
- Deletion only with `--delete`, previewed by `-n`.
- Tests in scratch directories at both ends.

## D8. Scope of release 1 (user's choice, 2026-11-03)
- VMS as client to Linux and VMS rsync servers, over ssh and rsync://, and local
  copies.
- VMS as a server comes later.
