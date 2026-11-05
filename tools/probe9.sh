#!/usr/bin/env bash
# probe9.sh [node...] - Phase 0 probe 9: is a program's stdin and stdout under
# VSI sshd binary-clean, the way Linux rsync would run `rsync --server` on VMS?
# For each node (default: all in tools/nodes.conf) it runs the probe's echo child
# (PROBE_PAIR.EXE -Child, built by probes/probes.com in the work directory) with
# `ssh host "mcr ...probe_pair.exe -Child"`, sends 1 KB and 10 MB of every byte
# value, and compares what comes back. The child reports on stderr how its
# argument arrived (case) and whether it saw EOF.
#
# This is the one deliberate raw `ssh host cmd`: the ssh exec path is what is
# under test. Each run is limited to 120 s. Output: out/probe9-<node>.txt.
set -uo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
key=${VMS_SSH_KEY:-$HOME/.ssh/vms_ed25519}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$top/out"
python3 - "$tmp" <<'EOF'
import sys
d = sys.argv[1]
for name, n in (("in1k", 1024), ("in10m", 10 * 1024 * 1024)):
    open(f"{d}/{name}.bin", "wb").write(bytes(((i * 7 + (i >> 9)) & 0xFF) for i in range(n)))
EOF
nodes=("$@"); [ ${#nodes[@]} -gt 0 ] || mapfile -t nodes < <(awk '!/^#/ && NF{print $1}' "$top/tools/nodes.conf")
for n in "${nodes[@]}"; do
    read -r _ _ host port user wd _ < <(awk -v n="$n" '$1==n' "$top/tools/nodes.conf")
    [ -n "${host:-}" ] || { echo "probe9: unknown node $n" >&2; continue; }
    log=$top/out/probe9-$n.txt; : > "$log"
    for f in in1k in10m; do
        start=$(date +%s.%N)
        timeout 120 ssh -i "$key" -o BatchMode=yes -p "$port" "$user@$host" \
            "mcr ${wd}probe_pair.exe -Child" < "$tmp/$f.bin" > "$tmp/$n-$f.out" 2> "$tmp/$n-$f.err"
        rc=$?
        secs=$(echo "$(date +%s.%N) - $start" | bc)
        sent=$(stat -c%s "$tmp/$f.bin"); got=$(stat -c%s "$tmp/$n-$f.out")
        same=NO; cmp -s "$tmp/$f.bin" "$tmp/$n-$f.out" && same=yes
        first=$(cmp "$tmp/$f.bin" "$tmp/$n-$f.out" 2>&1 | head -1)
        { echo "PROBE9 $n $f: ssh rc=$rc, ${secs}s, sent $sent, got $got, identical: $same ${first:+($first)}"
          tr -d '\r' < "$tmp/$n-$f.err" | grep -av '^\s*$' | grep -av Welcome | sed 's/^/    stderr: /' | head -5
          [ "$same" = yes ] || { echo "    first 64 bytes back:"; head -c 64 "$tmp/$n-$f.out" | xxd | sed 's/^/    /'; }
        } | sed -e "s/$host/<$n-host>/g" | tee -a "$log"
    done
done
