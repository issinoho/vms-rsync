#!/usr/bin/env bash
# net.sh <node> - Phase 2: rsync on <node> against Linux rsync 3.4.1 over ssh and
# rsync:// (tests/vms/net.com), with the test servers on this machine
# (tools/net_servers.sh, started and stopped here). Checks every transferred file
# by MD5 on both sides. Output: out/net-<node>.txt; prints NET lines and CHECK
# results, and exits 1 if a check fails.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: net.sh <node>}
. "$top/tools/node_lock.sh"
node_lock "$node" 9
d=$top/out/net
read -r addr sport dport < <("$top/tools/net_servers.sh" start)
trap '"$top/tools/net_servers.sh" stop' EXIT
echo "net: servers on port $sport (ssh) and $dport (rsync://)"
rm -rf "$d/push" "$d/push2" "$d/lsrc" "$d/daemon"; mkdir -p "$d/daemon"

# the Linux tree the node pulls: text, binary, an empty file, a name with a
# space, a dot file, mixed case, two levels
python3 - "$d/lsrc" <<'EOF'
import os, sys
r = sys.argv[1]
os.makedirs(r + "/dir/sub", exist_ok=True)
open(r + "/hello.txt", "w").write("".join("hello %d from Linux\n" % i for i in range(300)))
open(r + "/data.bin", "wb").write(os.urandom(500000))
open(r + "/empty.txt", "w").close()
open(r + "/with space.txt", "w").write("a name with a space\n")
open(r + "/.hidden", "w").write("a dot file\n")
open(r + "/Mixed.Case.TXT", "w").write("mixed case\n")
open(r + "/dir/x.txt", "w").write("in dir\n")
open(r + "/dir/sub/y.bin", "wb").write(os.urandom(70000))
EOF

"$top/tools/build.sh" "$node" > /dev/null || { echo "net: build failed (out/build-$node.log)" >&2; exit 1; }
"$top/tools/vms.sh" "$node" put "$top/tests/vms/mkfiles.c" -- tests/vms > /dev/null 2>&1 || true
"$top/tools/vms.sh" "$node" put "$d/net_key" -- net > /dev/null 2>&1 || true
out=$top/out/net-$node.txt
VMS_TIMEOUT=1800 "$top/tools/vms.sh" "$node" run tests/vms/net.com "$addr" "$sport" "$(id -un)" "$dport" "$d" |
    tr -d '\r' | sed -e "s/$addr/<linux-host>/g" > "$out" || true
grep -a '^NET:\|NET-DONE' "$out"

python3 - "$out" "$d" <<'EOF'
import hashlib, os, sys
out, d = sys.argv[1], sys.argv[2]
vms = {}
for line in open(out, errors="replace"):
    if line.startswith("NETMD5 "):
        _, tree, rel, md5 = line.split(None, 3)
        vms.setdefault(tree, {})[rel.upper()] = md5.strip().upper()
def unvms(rel):   # VMS name -> as the file was named: ^_ is a space, ^. a dot
    return rel.replace("^_", " ").replace("^.", ".")
def linux(root):
    m = {}
    for dp, dn, fn in os.walk(root):
        for f in fn:
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, root)
            m[rel.upper()] = hashlib.md5(open(p, "rb").read()).hexdigest().upper()
    return m
def vmsview(tree):  # VMS names lose a trailing dot for no type: "README." -> "README"
    return {unvms(k[:-1] if k.endswith(".") else k): v for k, v in vms.get(tree, {}).items()}
fails = 0
def check(what, a, b, an, bn):
    global fails
    ok = a == b
    if not ok:
        fails += 1
        for k in sorted(set(a) | set(b)):
            if a.get(k) != b.get(k):
                print("   ", k, an, a.get(k), bn, b.get(k))
    print("CHECK %-58s %s (%d files)" % (what, "ok" if ok else "FAILED", len(a)))
src = vmsview("src")
check("ssh push: Linux push/ = the node's tree", linux(d + "/push"), src, "linux", "vms")
check("ssh push --partial: Linux push2/ = the node's tree", linux(d + "/push2"), src, "linux", "vms")
check("rsync:// push: Linux daemon/push/ = the node's tree", linux(d + "/daemon/push"), src, "linux", "vms")
ls = linux(d + "/lsrc")
for t in ("pull", "pullz", "pull2"):
    check("ssh pull (%s): the node's tree = Linux lsrc/" % t, vmsview(t), ls, "vms", "linux")
check("rsync:// pull: the node's dpull = its own tree", vmsview("dpull"), src, "dpull", "src")
sys.exit(1 if fails else 0)
EOF
