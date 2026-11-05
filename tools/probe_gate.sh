#!/usr/bin/env bash
# probe_gate.sh <dir> - the ForceCommand of the probe sshd (tools/probe23.sh): it
# runs only `cat` and the one rsync server command probe 3 sends, and logs every
# command to <dir>/gate.log.
d=${1:?}
echo "$(date '+%H:%M:%S') [$SSH_ORIGINAL_COMMAND]" >> "$d/gate.log"
case "$SSH_ORIGINAL_COMMAND" in
cat) exec cat ;;
"rsync --server --sender -r . $d/remote/") exec rsync --server --sender -r . "$d/remote/" ;;
*) echo "probe gate: refused" >&2; exit 1 ;;
esac
