# CLAUDE.md

Working notes for vms-rsync: rsync for OpenVMS (x86-64 and IA64), ported from
openrsync (D1). `docs/PLAN.md` is the phased plan, `docs/DECISIONS.md` the decisions
and their alternatives, `docs/NOTES.md` what was observed on the nodes and how. Read
them before starting work. Work phase by phase, and stop and ask at each phase exit.

## Ground rules (from the sibling vms-* repos)

- **Use `tools/vms.sh`** for remote work. Never raw `ssh host cmd`, never `WAIT` over
  ssh. Run long jobs with `run_in_background`. One run per node (`tools/node_lock.sh`).
- **Committed files must not contain real node details** (they live in the git-ignored
  `tools/nodes.conf`), host addresses, credentials, passwords or private keys.
- **Never overwrite or delete a user's file:** an update is a new version (D4), and
  deletion happens only with `--delete`. Tests work in scratch directories on both ends.
- **Never edit `staging/`, `cache/` or `out/`.** Upstream files change only through
  `patches/`; ours go in `overlay/vms/`.
- Prefer a 20-line probe in `probes/` over speculation. Don't claim something works
  until a test on the node shows it.
- **Probes that start children:** give every child an explicit stderr (N1), never let
  a child read an inherited stdin, and never block in `waitpid`: a hung probe leaves
  processes on the node that must be stopped by hand.

## Hard constraints

- C99, VSI C, 32-bit pointers. Build from CLEAN after changing headers or qualifiers.
- No `fork`: `vfork`+`execve` with `decc$set_child_standard_streams` (N1).

## Commits

Commit in logical steps with messages that explain why, and record what was verified
and how. Don't push unless the user asks; there is no GitHub repository yet.
