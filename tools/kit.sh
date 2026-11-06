#!/usr/bin/env bash
# kit.sh <node> - build rsync on <node>, then its PCSI kit ([.OPENRSYNC.VMS.KIT]
# MAKE_KIT.COM), and fetch the kit to out/kits/. Builds first so the kit always
# matches the prepared tree: run tools/prepare.sh after any change.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: kit.sh <node>}
. "$top/upstream.conf"
read -r _ ARCH _ _ _ WORKDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
"$top/tools/build.sh" "$node" > /dev/null ||
    { echo "kit: build failed (out/build-$node.log)" >&2; exit 1; }
. "$top/tools/node_lock.sh"
node_lock "$node" 9
job=$top/cache/kit-$node.com
printf '$ set noon\n$ @%s.OPENRSYNC.VMS.KIT]MAKE_KIT.COM\n' "${WORKDIR%]}" > "$job"
VMS_TIMEOUT=1800 "$top/tools/vms.sh" "$node" run "$job" | tr -d '\r' > "$top/out/kit-$node.log"
grep -a '^MAKE_KIT\|%PCSI-[EFW]' "$top/out/kit-$node.log" || true
kit=$(sed -n 's/^MAKE_KIT: kit .*\]\([^;]*\);.*/\1/p' "$top/out/kit-$node.log" | head -1)
[ -n "$kit" ] || { echo "kit: no kit produced (see out/kit-$node.log)" >&2; exit 1; }
mkdir -p "$top/out/kits"
"$top/tools/vms.sh" "$node" get "openrsync/KIT_$ARCH/$kit" "$top/out/kits/$kit" > /dev/null
ls -la "$top/out/kits/$kit"
