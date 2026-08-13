#!/bin/sh
# Re-execute a test inside a private network namespace.
#
# Unprivileged: --user --map-root-user gives CAP_NET_BIND_SERVICE for the
# namespace, enough to bind port 123 on the namespace's own loopback.
set -eu

unshare=$(command -v unshare) || exit 77

# The binary can exist while unprivileged user namespaces are still
# blocked (e.g. AppArmor on Ubuntu 23.10+, or a locked-down CI runner),
# so probe the actual syscall rather than trust the binary's presence.
"$unshare" --user --map-root-user --net true 2>/dev/null || exit 77

exec "$unshare" --user --map-root-user --net -- \
     sh -c 'ip link set lo up 2>/dev/null || true; exec "$@"' -- "$@"
