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

cp -a "$d/lsrc" "$d/daemon/lsrc"
# names that differ only in case (D5): one file on VMS
rm -rf "$d/lcase"; mkdir -p "$d/lcase"
printf 'the first name\n' > "$d/lcase/Readme.TXT"
printf 'the second name\n' > "$d/lcase/README.txt"
"$top/tools/build.sh" "$node" > /dev/null || { echo "net: build failed (out/build-$node.log)" >&2; exit 1; }
"$top/tools/vms.sh" "$node" put "$top/tests/vms/mkfiles.c" "$top/tests/vms/md5tree.c" -- tests/vms > /dev/null 2>&1 || true
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
        parts = line.rstrip("\n").split(" ")
        tree, md5, rel = parts[1].lower(), parts[-1].upper(), " ".join(parts[2:-1])
        vms.setdefault(tree, {})[rel.upper()] = md5
def linux(root):
    m = {}
    for dp, dn, fn in os.walk(root):
        for f in fn:
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, root)
            m[rel.upper()] = hashlib.md5(open(p, "rb").read()).hexdigest().upper()
    return m
def vmsview(tree):
    return dict(vms.get(tree, {}))
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
ls = linux(d + "/lsrc")
for t in ("pull", "pullz", "pull2"):
    check("ssh pull (%s): the node's tree = Linux lsrc/" % t, vmsview(t), ls, "vms", "linux")
check("rsync:// pull: the node's dpull = Linux lsrc/", vmsview("dpull"), ls, "vms", "linux")
# Phase 3
check("pulled back: the node's back = its own tree (LF lines)", vmsview("back"), src, "back", "src")
rfm = {}
for line in open(out, errors="replace"):
    if line.startswith("NETRFM back "):
        _, _, name, f = line.split()
        rfm[name.upper()] = f
want = {"NOTES.TXT": "VAR", "BLANK.TXT": "VAR", "A.TXT": "VAR", "DATA.DAT": "STMLF", "FIX.BIN": "STMLF", "BIN.DAT": "STMLF"}
bad = {k: (rfm.get(k), v) for k, v in want.items() if rfm.get(k) != v}
print("CHECK %-58s %s %s" % ("pulled back: record formats (text types VAR)", "ok" if not bad else "FAILED", bad or ""))
if bad: fails += 1
case = vmsview("case")
ok = len(case) == 1
print("CHECK %-58s %s (%d files: %s)" % ("case: Readme.TXT and README.txt, one written", "ok" if ok else "FAILED", len(case), ", ".join(case)))
if not ok: fails += 1
check("rsync:// push refused: nothing in Linux daemon/push/", linux(d + "/daemon/push"), {}, "linux", "none")
sys.exit(1 if fails else 0)
EOF
