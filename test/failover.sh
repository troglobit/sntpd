#!/bin/sh
# A silent server is abandoned for the next one in the list.
set -eu

# shellcheck source=/dev/null
. "$(dirname "$0")/lib/setup.sh"

enter_namespace "$@"

say "Server on :123 is silent, server on :1123 answers"
"$FAKE" -p 123 -q &
dead=$!
"$FAKE" -p 1123 &
alive=$!

log=$(mktemp)
# -m 1 so eight misses cost seconds rather than two minutes.  The 15s
# default spacing is arithmetic that test-peer.c already asserts; what
# this test proves is the wiring, that a lost probe reaches the state
# machine and that rotation rebuilds the socket against the next entry.
# -p 0 disables sntpd's own server mode, which would otherwise contest
# port 123 with the fake server and, if it ever won, answer its own
# probes.
"$SNTPD" -d -n -l debug -m 1 -i 600 -p 0 127.0.0.1:123 127.0.0.1:1123 >"$log" 2>&1 &
sntpd=$!
# shellcheck disable=SC2064
trap "kill $dead $alive $sntpd 2>/dev/null || true; rm -f $log" EXIT

if ! retry "grep -qi 'unreachable' $log" 60 1; then
	cat "$log"
	fail "sntpd never noticed the silent server"
fi

assert "sntpd gave up on the silent server" -n "$(grep -i 'unreachable' "$log")"
assert "sntpd moved to the second server" \
       -n "$(grep -E 'Trying NTP server 127\.0\.0\.1:1123' "$log")"
assert "the low floor was warned about" -n "$(grep -i 'RFC 4330' "$log")"
