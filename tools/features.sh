#!/usr/bin/env bash
# features.sh [node...] - run openrsync's own configure tests natively
# (probes/features.com) on each node, and write overlay/vms/configure.local from
# the answers: a feature is 1 only where every node said 1. Prints the answers that
# differ between nodes. Output per node: out/features-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
. "$top/upstream.conf"
"$top/tools/fetch.sh" > /dev/null
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
tar -xzf "$top/cache/$UPSTREAM_NAME-$UPSTREAM_COMMIT.tar.gz" -C "$tmp" "$UPSTREAM_NAME-$UPSTREAM_COMMIT/tests.c"
mkdir -p "$top/out"
nodes=("$@"); [ ${#nodes[@]} -gt 0 ] || mapfile -t nodes < <(awk '!/^#/ && NF{print $1}' "$top/tools/nodes.conf")
for n in "${nodes[@]}"; do
    if [ -z "${FEATURES_REUSE:-}" ]; then      # FEATURES_REUSE=1: only rebuild configure.local
        "$top/tools/vms.sh" "$n" put "$tmp/$UPSTREAM_NAME-$UPSTREAM_COMMIT/tests.c" -- feat > /dev/null 2>&1 || true
        VMS_TIMEOUT=1800 "$top/tools/vms.sh" "$n" run probes/features.com | tr -d '\r' > "$top/out/features-$n.raw" || true
        grep -q '^FEATURES-DONE' "$top/out/features-$n.raw" ||
            { echo "features: $n did not finish (out/features-$n.raw)" >&2; exit 1; }
        grep -a '^FEATURE ' "$top/out/features-$n.raw" > "$top/out/features-$n.txt"
    fi
    [ -s "$top/out/features-$n.txt" ] || { echo "features: no answers for $n" >&2; exit 1; }
    echo "features: $n: $(grep -c ' 1$' "$top/out/features-$n.txt") of $(wc -l < "$top/out/features-$n.txt") present"
done
# 1 only where all nodes said 1
out=$top/overlay/vms/configure.local
{
    echo "# configure.local - openrsync's oconfigure answers for OpenVMS (x86-64 and IA64),"
    echo "# from its own tests compiled and linked natively by tools/features.sh"
    echo "# (probes/features.com) on: ${nodes[*]}. A feature is 1 only where every node"
    echo "# has it. tools/prepare.sh runs ./configure with this file to make config.h."
    cat "$top"/out/features-*.txt | awk '{n[$2]++; s[$2] += $3} END {for (k in n) print k, (s[k] == n[k]) ? 1 : 0}' |
        sort | awk '{printf "HAVE_%s=%s\n", $1, $2}'
    if [ -f "$top/overlay/vms/configure.overrides" ]; then
        echo "# Overrides, each with its reason (overlay/vms/configure.overrides):"
        cat "$top/overlay/vms/configure.overrides"
    fi
} > "$out"
cat "$top"/out/features-*.txt | awk '{s[$2] = s[$2] $3} END {for (k in s) if (s[k] ~ /0/ && s[k] ~ /1/) print "features: differs between nodes:", k}'
echo "features: wrote overlay/vms/configure.local"
