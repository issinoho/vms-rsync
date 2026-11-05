#!/usr/bin/env bash
# fetch.sh - download the openrsync archive pinned in upstream.conf into cache/ and
# verify its SHA-256. GitHub publishes no signature (D0); see upstream.conf.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
. "$top/upstream.conf"
mkdir -p "$top/cache"
tarball=$top/cache/$UPSTREAM_NAME-$UPSTREAM_COMMIT.tar.gz
[ -f "$tarball" ] || { echo "fetch: downloading $UPSTREAM_URL"
                       curl -fsSL -o "$tarball.tmp" "$UPSTREAM_URL"; mv "$tarball.tmp" "$tarball"; }
echo "$UPSTREAM_SHA256  $tarball" | sha256sum -c --quiet - ||
    { echo "fetch: SHA-256 mismatch for $tarball (see upstream.conf)" >&2; exit 1; }
echo "fetch: SHA-256 OK"
echo "fetch: $tarball"
