#!/bin/sh
# An entry sntpd cannot even build a socket for is a candidate like any
# other: it runs out of chances and the list moves on.  Both ways of
# failing used to loop above the rotation check, so the first entry
# blocked the rest -- a name that would not resolve, and an address with
# no route to it.
set -eu

# shellcheck source=/dev/null
. "$(dirname "$0")/lib/setup.sh"

enter_namespace "$@"

say "First entry cannot resolve, second has no route, third answers"
"$FAKE" -p 1123 &
alive=$!

# .invalid is reserved by RFC 2606 and never resolves, so nothing here
# waits on a real name server.
log=$(mktemp)
"$SNTPD" -d -n -l debug -p 0 -m 1 -i 600 no.such.host.invalid 192.0.2.1:1123 \
	 127.0.0.1:1123 >"$log" 2>&1 &
sntpd=$!
# shellcheck disable=SC2064
trap "kill $alive $sntpd 2>/dev/null || true; rm -f $log" EXIT

if ! retry "grep -qE 'Trying NTP server 127\.0\.0\.1:1123' $log" 60 1; then
	cat "$log"
	fail "a first entry that cannot be reached blocked the list"
fi

# The gate already required that it got here, so assert what the gate
# did not.  Two rotations and no more: a ring that advanced on anything
# other than failure would keep going, and one that charged only the
# resolve failure would never have left 192.0.2.1.
assert "it rotated twice and stayed" \
       "$(grep -cE 'Trying NTP server' "$log")" -eq 2

assert "the unroutable entry ran out of chances too" \
       -n "$(grep -E 'Server 192\.0\.2\.1:1123 unreachable' "$log")"

# Ordering, not just arrival: the first entry has to be condemned by
# name and the move has to come after that, otherwise the second entry
# could be reached by some other path entirely.
condemned=$(grep -nE 'Server no\.such\.host\.invalid:123 unreachable' "$log" | head -1 | cut -d: -f1)
trying=$(grep -nE 'Trying NTP server 127\.0\.0\.1:1123' "$log" | head -1 | cut -d: -f1)

assert "the unresolvable entry was condemned, not merely stepped over" -n "$condemned"
assert "the move came after the condemnation" "$condemned" -lt "$trying"
