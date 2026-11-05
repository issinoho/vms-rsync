#!/usr/bin/env bash
# push.sh <node> - upload the prepared tree (staging/openrsync) to <node>'s work
# directory as [.OPENRSYNC], with [.OPENRSYNC.VMS] for our files. Only files whose
# content changed since the last push are uploaded (PUSH_ALL=1 for all).
set -euo pipefail
export LC_ALL=C
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: push.sh <node>}
. "$top/upstream.conf"
stage=$top/staging/$UPSTREAM_NAME
[ -f "$stage/config.h" ] || { echo "push: run tools/prepare.sh first" >&2; exit 1; }
read -r _ _ HOST PORT USER _ SFTPDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
manifest=$top/cache/pushed-$node.sha
[ "${PUSH_ALL:-}" = 1 ] && rm -f "$manifest"
touch "$manifest"
now=$top/cache/push-$node.sha
(cd "$top/staging" && find "$UPSTREAM_NAME" -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' \) -print0 |
    sort -z | xargs -0 sha256sum
 cd "$top/staging" && find "$UPSTREAM_NAME/vms" "$UPSTREAM_NAME/regress" -type f -print0 2>/dev/null |
    sort -z | xargs -0 sha256sum) > "$now"
changed=$(comm -23 <(awk '{print $2" "$1}' "$now" | sort) <(awk '{print $2" "$1}' "$manifest" | sort) | awk '{print $1}')
echo "push: $(echo "$changed" | grep -c . || true) changed of $(wc -l < "$now") files -> $node:[.$(echo "$UPSTREAM_NAME" | tr a-z A-Z)]"
batch=$top/cache/push-$node.batch
{
    echo "cd $SFTPDIR"
    (cd "$top/staging" && find "$UPSTREAM_NAME" "$UPSTREAM_NAME/vms" -maxdepth 0 -type d; \
     cd "$top/staging" && find "$UPSTREAM_NAME/regress" -type d 2>/dev/null) | sed 's/^/-mkdir /'
    for f in $changed; do echo "put $top/staging/$f $f"; done
} > "$batch"
sftp -P "$PORT" -i "${VMS_SSH_KEY:-$HOME/.ssh/vms_ed25519}" -o BatchMode=yes -b "$batch" "$USER@$HOST" \
    2>&1 >/dev/null | grep -vE '^ *Welcome to|^ *$|^remote mkdir .*Failure' >&2 || true
cp "$now" "$manifest"
echo "push: done"
