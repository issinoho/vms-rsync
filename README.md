# vms-rsync

rsync for OpenVMS (x86-64 and IA64), ported from [openrsync](https://github.com/kristapsdz/openrsync),
the ISC-licensed rsync that OpenBSD and macOS ship. Part of the
[ports and tools for OpenVMS](https://openvms.issinoho.com) family.

**Status: work in progress, not yet released.** Phases 0-2 of [docs/PLAN.md](docs/PLAN.md)
are done:

- **Builds** natively with VSI C on both architectures, from openrsync's own source
  plus 11 patches and a VMS layer (`overlay/vms`). There is no `fork()` on VMS:
  rsync starts its other end with `vfork`+`exec`, and a shim supplies the `*at()`
  calls over paths.
- **Local copies** VMS to VMS: exact, a second run sends nothing, and a change goes
  by delta.
- **To and from Linux rsync 3.4.1, both nodes, every file checked by MD5:**
  - over ssh (VSI's ssh client): push and pull, with `--delete`, `-n`, `-c`, `-z`,
    `--partial`, `--progress` and `--link-dest`;
  - from an `rsync://` daemon (pull only; openrsync cannot push to one or log in
    to one).
- Each update on VMS is a new version of the file.

Not yet: VMS text files with records (they would go as raw bytes), `--keep-versions`,
VMS file specifications as arguments, and VMS as an rsync server (VSI's sshd does not
give a command a clean binary channel). That is Phase 3 and later; see
[docs/DECISIONS.md](docs/DECISIONS.md) and [docs/NOTES.md](docs/NOTES.md) for what was
found on the way.

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
