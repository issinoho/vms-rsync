#!/usr/bin/env bash
# net_gate.sh <dir> - the ForceCommand of the Phase 2 test sshd (tools/net_servers.sh):
# it runs only `rsync --server ...` whose paths are inside <dir>, with no shell
# metacharacters, and logs every command to <dir>/gate.log.
d=${1:?}
cmd=$SSH_ORIGINAL_COMMAND
echo "$(date '+%H:%M:%S') [$cmd]" >> "$d/gate.log"
refuse() { echo "net gate: refused: $1" >&2; echo "$(date '+%H:%M:%S')   refused: $1" >> "$d/gate.log"; exit 1; }
case $cmd in
"rsync --server "*) ;;
"exit-status 0") exit 0 ;;          # for net.com's check of VSI ssh's exit status
"exit-status 1") exit 1 ;;
*) refuse "not an rsync server command" ;;
esac
case $cmd in *[\;\&\|\`\$\<\>\\\"\'\(\)\{\}]*) refuse "shell metacharacters" ;; esac
read -r -a a <<< "$cmd"
for x in "${a[@]:2}"; do
    case $x in
    -*|.) ;;
    *) case $x in "$d"/*) ;; *) refuse "path outside the test directory: $x" ;; esac
       case $x in *..*) refuse "'..' in a path" ;; esac ;;
    esac
done
exec /usr/bin/rsync "${a[@]:1}"
