#!/usr/bin/env bash
# local.sh <node> - the Phase 1 exit test on <node> (tests/vms/local.com): a local
# rsync -a of a stream-file tree, a second run, and a delta after a change.
# Builds first. Output: out/local-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: local.sh <node>}
"$top/tools/build.sh" "$node" > /dev/null || { echo "local: build failed (out/build-$node.log)" >&2; exit 1; }
"$top/tools/vms.sh" "$node" put "$top/tests/vms/mkfiles.c" -- tests/vms > /dev/null 2>&1 || true
VMS_TIMEOUT=900 "$top/tools/vms.sh" "$node" run tests/vms/local.com | tr -d '\r' > "$top/out/local-$node.txt"
grep -a -v '^\s*$' "$top/out/local-$node.txt"
