#!/usr/bin/env bash
# build.sh <node> [CLEAN] - push the prepared tree and run [.OPENRSYNC.VMS]BUILD.COM
# on <node> (one run per node). The log is printed and kept in out/build-<node>.log.
# BUILD.COM compiles every source even after a failure and lists the failures, so
# one run shows all the porting work left.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: build.sh <node> [CLEAN]}
what=${2:-}
. "$top/tools/node_lock.sh"
node_lock "$node" 9
read -r _ _ _ _ _ WORKDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
"$top/tools/push.sh" "$node"
mkdir -p "$top/out"
job=$top/cache/build-$node.com
cat > "$job" <<DCL
\$ set noon
\$ @${WORKDIR%]}.OPENRSYNC.VMS]BUILD.COM "$what"
DCL
VMS_TIMEOUT=${VMS_BUILD_TIMEOUT:-3600} "$top/tools/vms.sh" "$node" run "$job" | tr -d '\r' > "$top/out/build-$node.log"
grep -aE '^BUILD:|%CC-[EF]-|%I?LINK-[EFW]-' "$top/out/build-$node.log" | head -60
grep -q 'BUILD: done, 0 failed' "$top/out/build-$node.log"
