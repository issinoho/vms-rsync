#!/usr/bin/env bash
# installcheck.sh <node> - install the node's kit (tools/kit.sh first), verify
# it, run rsync from it (version, a local copy with text records, the text type
# list, --keep-versions, error severity), and remove it again. This changes the
# system while it runs (PCSI database, SYS$COMMON:[RSYNC], RSYNC$ROOT).
# Output: out/install-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: installcheck.sh <node>}
. "$top/tools/node_lock.sh"
node_lock "$node" 9
log=$top/out/install-$node.txt
VMS_TIMEOUT=1800 "$top/tools/vms.sh" "$node" run tools/vms_installcheck.com 2>&1 | tr -d '\r' > "$log" || true
grep -aE 'install status|remove status|startup procedure|RSYNC_[A-Z_]*: |local copy:|other.dat|versions of|failed run|after removal|for OpenVMS' "$log"
fails=0
for check in VERSION LOCAL_COPY STREAM_LF TEXT_TYPES KEEP_VERSIONS ERROR_SEVERITY; do
    grep -aq "RSYNC_$check: PASS" "$log" || { echo "installcheck: RSYNC_$check did not pass" >&2; fails=1; }
done
grep -aq 'RSYNC\$ROOT after removal: \[\]' "$log" && grep -aq 'files after removal: \[\]' "$log" &&
    grep -aq 'startup after removal: \[\]' "$log" || { echo "installcheck: removal left something" >&2; fails=1; }
exit $fails
