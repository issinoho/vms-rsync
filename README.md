# vms-rsync

rsync for OpenVMS (x86-64 and IA64), ported from [openrsync](https://github.com/kristapsdz/openrsync),
the ISC-licensed rsync that OpenBSD and macOS ship. Part of the
[ports and tools for OpenVMS](https://openvms.issinoho.com) family.

**Status: work in progress, not yet released.** Phases 0-3 of [docs/PLAN.md](docs/PLAN.md)
are done, tested on both architectures:

- **Builds** natively with VSI C, from openrsync's own source plus 15 patches and a
  VMS layer (`overlay/vms`). There is no `fork()` on VMS: rsync starts its other end
  with `vfork`+`exec`, and a shim supplies the `*at()` calls over paths.
- **Local copies** VMS to VMS: exact, a second run sends nothing, and a change goes
  by delta.
- **To and from Linux rsync 3.4.1, every file checked by MD5:**
  - over ssh (VSI's ssh client): push and pull, with `--delete`, `-n`, `-c`, `-z`,
    `--partial`, `--progress` and `--link-dest`;
  - from an `rsync://` daemon (pull only; openrsync cannot push to one or log in
    to one).
- **VMS text files:** variable-length record files go as LF lines, and arrive as
  records again when their file type is a text type (`RSYNC$TEXT_TYPES`, by default
  the same list as VFTP). Everything else goes byte for byte, as stream files.
- **Versions:** each update is a new version of the file. `--keep-versions=N` keeps
  the newest N, and `--delete` removes every version of a file the source no longer
  has.
- **VMS names:** VMS file specifications work as arguments (`[.DIR]`, `DISK:[DIR]`).
  Names that differ only in case are one file on VMS: the second is left out with a
  message, and the exit status is 23.
- **Attributes:** times, and modes as protection; symlinks. Files belong to the user
  running rsync (`-o`/`-g` are ignored). `-H` is refused, and devices and fifos are
  left out.

Options in capitals need quotes in DCL (`"-P"`), or `SET PROCESS/PARSE_STYLE=EXTENDED`.

Not yet: a PCSI kit (Phase 4), and VMS as an rsync server (VSI's sshd does not
give a command a clean binary channel). See [docs/DECISIONS.md](docs/DECISIONS.md)
and [docs/NOTES.md](docs/NOTES.md) for what was found on the way.

## Building

```sh
tools/features.sh             # openrsync's configure tests, run natively (once)
tools/prepare.sh              # fetch the pinned source, patch, overlay, config.h
tools/build.sh x86 CLEAN      # build on a node ([.OPENRSYNC.VMS]RSYNC.EXE)
tools/local.sh x86            # the local-copy test
tools/net.sh x86              # the network test, with Linux test servers
```

`tools/nodes.conf` (git-ignored; see `tools/nodes.conf.example`) names the nodes. The
node needs VSI C, and the vms-zlib kit (`ZLIB$ROOT`).

## Licence

openrsync's ISC licence (in the source); our patches and VMS files are under the same
terms.

OpenVMS is a trademark of VMS Software, Inc. This project is not affiliated with VMS
Software, Inc. or with the openrsync project.
