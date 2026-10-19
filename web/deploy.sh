#!/bin/bash
# Uploads build/web/dist (node web/build.mjs) to the web host.
#
#   web/deploy.sh             copies the deployed site to DIR.bak-<UTC time> on the host,
#                             then replaces DIR with build/web/dist
#   web/deploy.sh rollback B  puts the backup B (a DIR.bak-<time> folder) back in DIR
#
# Environment:
#   HDTRANS_WEB_HOST  ssh destination, user@host (required)
#   HDTRANS_WEB_SSH   ssh command with its options (default ssh)
#   HDTRANS_WEB_DIR   site folder on the host (default /srv/hdtrans/web)
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
host=${HDTRANS_WEB_HOST:?set HDTRANS_WEB_HOST to user@host}
ssh=${HDTRANS_WEB_SSH:-ssh}
dir=${HDTRANS_WEB_DIR:-/srv/hdtrans/web}
remote() { $ssh "$host" "$@"; }

case ${1:-deploy} in
  deploy)
    [ -f "$root/build/web/dist/index.html" ] || { echo "build/web/dist is missing: run node web/build.mjs" >&2; exit 1; }
    backup="$dir.bak-$(date -u +%Y%m%dT%H%M%SZ)"
    remote "set -e; if [ -d '$dir' ]; then cp -a '$dir' '$backup'; echo 'backup: $backup'; fi; mkdir -p '$dir'"
    rsync -rlptz --delete --chmod=D755,F644 -e "$ssh" "$root/build/web/dist/" "$host:$dir/"
    echo "rollback: web/deploy.sh rollback $backup"
    ;;
  rollback)
    backup=${2:?usage: $0 rollback BACKUP}
    remote "set -e; [ -d '$backup' ]; rsync -a --delete '$backup/' '$dir/'"
    echo "restored $backup"
    ;;
  *) echo "usage: $0 [deploy | rollback BACKUP]" >&2; exit 2 ;;
esac
