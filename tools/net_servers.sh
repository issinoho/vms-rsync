#!/usr/bin/env bash
# net_servers.sh start|stop - the Phase 2 test servers on this Linux machine, as you
# (not root), for tools/net.sh. Both listen only on the address the nodes reach,
# and accept only the nodes in tools/nodes.conf. Everything is in out/net/
# (git-ignored).
#   - an OpenSSH server for a fresh test key made per run; every command goes
#     through tools/net_gate.sh, which runs only `rsync --server` inside out/net;
#   - an rsync daemon (rsync 3.4.1 here) with one anonymous read-write module, m,
#     at out/net/daemon (openrsync has no daemon authentication, NOTES N6).
# "start" prints: address ssh-port daemon-port
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
d=$top/out/net
sshport=${RSYNC_NET_SSH_PORT:-2298}
dport=${RSYNC_NET_DAEMON_PORT:-2873}
stop() {
    for f in sshd.pid rsyncd.pid; do
        [ -f "$d/$f" ] && kill "$(cat "$d/$f")" 2>/dev/null || true
        rm -f "$d/$f"
    done
    # a daemon's per-connection child can outlive it (NOTES N6): any process
    # still running from this test's configuration goes too
    pkill -f -- "--config=$d/rsyncd.conf" 2>/dev/null || true
    sleep 1
    pkill -KILL -f -- "--config=$d/rsyncd.conf" 2>/dev/null || true
    pkill -f -- "-f $d/sshd_config" 2>/dev/null || true
    rm -f "$d/net_key"
}
case ${1:-} in
stop) stop; exit 0 ;;
start) ;;
*) echo "usage: net_servers.sh start|stop" >&2; exit 2 ;;
esac
stop
mkdir -p "$d/daemon"; chmod 700 "$d"
: > "$d/gate.log"
[ -f "$d/host_key" ] || ssh-keygen -q -t ed25519 -N '' -C rsync-net-host -f "$d/host_key"
rm -f "$d/net_key" "$d/net_key.pub"
ssh-keygen -q -t ed25519 -N '' -C rsync-net-client -f "$d/net_key"
nodes=$(awk '!/^#/ && NF {print $3}' "$top/tools/nodes.conf" | paste -sd, -)
first=$(awk '!/^#/ && NF {print $3; exit}' "$top/tools/nodes.conf")
listen=$(ip -4 route get "$first" | sed -n 's/.* src \([0-9.]*\).*/\1/p')
echo "restrict,from=\"$nodes\" $(cat "$d/net_key.pub")" > "$d/authorized_keys"
chmod 600 "$d/authorized_keys"
cat > "$d/sshd_config" <<EOF
Port $sshport
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
ForceCommand /bin/bash $top/tools/net_gate.sh $d
LogLevel VERBOSE
EOF
/usr/sbin/sshd -t -f "$d/sshd_config"
# without the caller's node lock (net.sh's fd 9): a server must not hold it
/usr/sbin/sshd -f "$d/sshd_config" -E "$d/sshd.log" 9>&-
cat > "$d/rsyncd.conf" <<EOF
pid file = $d/rsyncd.pid
address = $listen
port = $dport
use chroot = no
log file = $d/rsyncd.log
hosts allow = $(echo "$nodes" | tr , ' ')
hosts deny = *
[m]
    path = $d/daemon
    read only = no
    list = no
EOF
rsync --daemon --config="$d/rsyncd.conf" 9>&-
sleep 1
[ -f "$d/sshd.pid" ] || { echo "net_servers: sshd did not start" >&2; cat "$d/sshd.log" >&2; stop; exit 1; }
[ -f "$d/rsyncd.pid" ] || { echo "net_servers: rsync daemon did not start" >&2; cat "$d/rsyncd.log" >&2; stop; exit 1; }
echo "$listen $sshport $dport"
