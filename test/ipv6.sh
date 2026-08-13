#!/bin/sh
# IPv6 literal addresses, bracketed and bare.
set -eu

# shellcheck source=/dev/null
. "$(dirname "$0")/lib/setup.sh"

enter_namespace "$@"

# sntpd logs the address it resolved; a botched split shows up as a
# resolver failure naming the truncated host.
# Run sntpd briefly and hand back what it logged.  Track the PID
# explicitly: `kill %1` needs job control, which dash disables in
# non-interactive shells, and the process would linger forever.
try()
{
	"$SNTPD" -d -n -l debug -i 15 "$1" 2>&1 &
	sntpd_pid=$!
	sleep 2
	kill "$sntpd_pid" 2>/dev/null || true
	wait "$sntpd_pid" 2>/dev/null || true
}

say "Bare IPv6 literal"
out=$(try '::1')
assert "::1 resolved, not split" -z "$(echo "$out" | grep -i 'resolving')"

say "Bracketed IPv6 literal with port"
out=$(try '[::1]:123')
assert "[::1]:123 resolved, not split" -z "$(echo "$out" | grep -i 'resolving')"

say "Hostname with port still works"
out=$(try '127.0.0.1:123')
assert "127.0.0.1:123 resolved" -z "$(echo "$out" | grep -i 'resolving')"
