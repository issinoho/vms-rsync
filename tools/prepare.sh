#!/usr/bin/env bash
# prepare.sh - build a VMS-ready openrsync tree in staging/openrsync/
#
#   1. fetch and verify the pinned archive (upstream.conf)
#   2. extract it, apply patches/series, lay overlay/ over the top
#   3. config.h: upstream's ./configure, here on Linux, with the answers measured
#      on the nodes (overlay/vms/configure.local, from tools/features.sh), then
#      vms/vms_config.h appended (the *at shim and VMS definitions)
#   4. vms/sources.txt: the Makefile's OBJS, for [.VMS]BUILD.COM
#
# Nothing in staging/ is ever edited by hand: fix things in patches/ or overlay/.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
. "$top/upstream.conf"
stage=$top/staging/$UPSTREAM_NAME
step() { echo "prepare: $*"; }
die() { echo "prepare: error: $*" >&2; exit 1; }

"$top/tools/fetch.sh" > /dev/null
step "extracting $UPSTREAM_NAME $UPSTREAM_COMMIT"
rm -rf "$stage" "$top/staging/$UPSTREAM_NAME-$UPSTREAM_COMMIT"; mkdir -p "$top/staging"
tar -xzf "$top/cache/$UPSTREAM_NAME-$UPSTREAM_COMMIT.tar.gz" -C "$top/staging"
mv "$top/staging/$UPSTREAM_NAME-$UPSTREAM_COMMIT" "$stage"

if [ -f "$top/patches/series" ]; then
    while read -r p; do
        case $p in ''|'#'*) continue ;; esac
        step "patch $p"
        patch -d "$stage" -p1 -s --no-backup-if-mismatch -F0 < "$top/patches/$p" ||
            die "patch $p does not apply cleanly"
    done < "$top/patches/series"
fi

(cd "$top/overlay" && find . -type f) | while read -r f; do
    [ -e "$stage/$f" ] && die "overlay/$f would replace an upstream file; use a patch"
    true
done
cp -a "$top/overlay/." "$stage/"

step "config.h (configure with vms/configure.local)"
[ -f "$stage/vms/configure.local" ] || die "no overlay/vms/configure.local: run tools/features.sh"
(cd "$stage" && cp vms/configure.local configure.local && ./configure > vms/configure.log 2>&1) ||
    die "configure failed (staging/$UPSTREAM_NAME/vms/configure.log)"
# configure tests some features itself on Linux; only the answers in
# configure.local are VMS's, so check every HAVE_ came from there.
missing=$(grep -o '^#define HAVE_[A-Z0-9_]* [01]' "$stage/config.h" | awk '{print $2}' |
          while read -r h; do grep -q "^$h=" "$stage/configure.local" || echo "$h"; done)
[ -z "$missing" ] || die "config.h has answers not measured on VMS: $missing"
# our definitions after configure's, inside its include guard
sed -i 's|^#endif /\*!OCONFIGURE_CONFIG_H\*/|#include "vms/vms_config.h"\n&|' "$stage/config.h"
grep -q 'vms/vms_config.h' "$stage/config.h" || die "could not append vms_config.h to config.h"

step "source list"
# OBJS is one backslash-continued list; ALLOBJS adds main.o, appended below.
awk '/^OBJS/{on=1} on{print; if ($0 !~ /\\$/) exit}' "$stage/Makefile" | grep -o '[a-z_0-9]*\.o' |
    sed 's/\.o$//' > "$stage/vms/sources.txt"
echo main >> "$stage/vms/sources.txt"
step "$(wc -l < "$stage/vms/sources.txt") sources"
step "version and PCSI kit inputs"
: "${KIT_PRODUCER:=ISSINOHO}"
kitversion="$UPSTREAM_NAME-$(echo "$UPSTREAM_DATE" | tr -d -)-vms$VMS_PATCH_LEVEL"
printf '#define VMS_KIT_VERSION "%s"\n#define VMS_PCSI_VERSION "%s"\n' "$kitversion" "$PCSI_VERSION" \
    > "$stage/vms/vms_version.h"
kit=$stage/vms/kit
subst() {
    sed -e "s/@PRODUCER@/$KIT_PRODUCER/g" -e "s/@BASE@/$1/g" -e "s/@PCSIVERSION@/$PCSI_VERSION/g" \
        -e "s/@KITVERSION@/$kitversion/g" -e "s/@UPSTREAMDATE@/$UPSTREAM_DATE/g" -e "s/@ARCH@/$2/g"
}
for base in I64VMS X86VMS; do
    subst $base "" < "$kit/rsync.pcsi\$desc_template" > "$kit/RSYNC-$base.PCSI\$DESC"
    subst $base "" < "$kit/rsync.pcsi\$text_template" > "$kit/RSYNC-$base.PCSI\$TEXT"
done
rm -f "$kit/rsync.pcsi\$desc_template" "$kit/rsync.pcsi\$text_template"
mv "$kit/rsync\$startup.com" "$kit/RSYNC\$STARTUP.COM"
mv "$kit/rsync\$setup.com" "$kit/RSYNC\$SETUP.COM"
mv "$kit/make_kit.com" "$kit/MAKE_KIT.COM"
subst "" "IA64 and x86-64" < "$kit/readme.vms" > "$kit/README.VMS"; rm -f "$kit/readme.vms"
mkdir -p "$kit/doc"
cp "$stage/LICENSE.md" "$kit/doc/LICENSE."
# the manual, from openrsync's mdoc page, as plain text
groff -mandoc -Tascii -P-c -P-b -P-u "$stage/openrsync.1" 2>/dev/null > "$kit/doc/RSYNC.TXT"
[ -s "$kit/doc/RSYNC.TXT" ] || die "could not render openrsync.1"
printf 'KIT_PRODUCER=%s\nPCSI_VERSION=%s\nKIT_VERSION=%s\n' "$KIT_PRODUCER" "$PCSI_VERSION" \
    "$kitversion" > "$kit/kit.env"
step "staged $stage"
