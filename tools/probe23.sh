#!/usr/bin/env bash
# probe23.sh [node...] - Phase 0 probes 2 and 3: rsync's ssh transport from the
# nodes to a Linux sshd (the release-1 direction, D8).
#
# It starts a private OpenSSH server on this machine (as you, not root; after
# vms-vftp's unix_sftpd.sh): a fresh test key made here (out/p23, git-ignored),
# accepted only from the nodes in tools/nodes.conf, every command through
# tools/probe_gate.sh, which runs only `cat` and the one `rsync --server` command.
# For each node it uploads the test key to [.P23], runs probes/probe23.com, and
# the procedure deletes the key. The server stops when this script ends.
# Output: out/probe23-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
d=$top/out/p23
port=${RSYNC_PROBE_PORT:-2298}
mkdir -p "$d/remote"; chmod 700 "$d"
printf 'a file for rsync --server\n' > "$d/remote/one.txt"
: > "$d/gate.log"

[ -f "$d/host_key" ] || ssh-keygen -q -t ed25519 -N '' -C rsync-probe-host -f "$d/host_key"
rm -f "$d/p23_key" "$d/p23_key.pub"
ssh-keygen -q -t ed25519 -N '' -C rsync-probe-client -f "$d/p23_key"
nodes=$(awk '!/^#/ && NF {print $3}' "$top/tools/nodes.conf" | paste -sd, -)
first=$(awk '!/^#/ && NF {print $3; exit}' "$top/tools/nodes.conf")
listen=$(ip -4 route get "$first" | sed -n 's/.* src \([0-9.]*\).*/\1/p')
echo "restrict,from=\"$nodes\" $(cat "$d/p23_key.pub")" > "$d/authorized_keys"
chmod 600 "$d/authorized_keys"
cat > "$d/sshd_config" <<EOF
Port $port
ListenAddress $listen
HostKey $d/host_key
PidFile $d/sshd.pid
AuthorizedKeysFile $d/authorized_keys
AllowUsers $(id -un)
PubkeyAuthentication yes
PasswordAuthentication no
KbdInteractiveAuthentication no
UsePAM no
StrictModes no
AllowTcpForwarding no
X11Forwarding no
PermitTTY no
ForceCommand /bin/bash $top/tools/probe_gate.sh $d
LogLevel VERBOSE
EOF
stop() { [ -f "$d/sshd.pid" ] && kill "$(cat "$d/sshd.pid")" 2>/dev/null; rm -f "$d/sshd.pid" "$d/p23_key"; }
trap stop EXIT
/usr/sbin/sshd -t -f "$d/sshd_config"
/usr/sbin/sshd -f "$d/sshd_config" -E "$d/sshd.log"
sleep 1
[ -f "$d/sshd.pid" ] || { echo "probe23: sshd did not start" >&2; cat "$d/sshd.log" >&2; exit 1; }
echo "probe23: test sshd on port $port"

nodes=("$@"); [ ${#nodes[@]} -gt 0 ] || mapfile -t nodes < <(awk '!/^#/ && NF{print $1}' "$top/tools/nodes.conf")
for n in "${nodes[@]}"; do
    read -r _ _ host _ _ _ _ tree < <(awk -v n="$n" '$1==n' "$top/tools/nodes.conf")
    out=$top/out/probe23-$n.txt
    "$top/tools/vms.sh" "$n" put probes/probe_pair.c probes/probe_ssh2.c -- probes > /dev/null 2>&1 || true
    "$top/tools/vms.sh" "$n" put "$d/p23_key" -- p23 > /dev/null 2>&1 || true
    VMS_TIMEOUT=300 "$top/tools/vms.sh" "$n" run probes/probe23.com "$listen" "$port" "$(id -un)" "$tree" "$d/remote" 2>&1 |
        tr -d '\r' | grep -av '^\s*$' | sed -e "s/$listen/<linux-host>/g" -e "s/$host/<$n-host>/g" > "$out" || true
    echo "#### $n"
    grep -aE '^===|PAIR|SSH2|CHILD|%CC-[EF]|%LINK|link status' "$out" || cat "$out"
done
echo "#### the server's gate log"
cat "$d/gate.log"
