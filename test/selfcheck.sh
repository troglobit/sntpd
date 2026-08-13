#!/bin/sh
# Verify the harness itself: namespace, loopback, fake server.
set -eu

# shellcheck source=/dev/null
. "$(dirname "$0")/lib/setup.sh"

enter_namespace "$@"

say "Starting fake NTP server on 127.0.0.1:123"
"$FAKE" -p 123 &
fake_pid=$!
# shellcheck disable=SC2064
trap "kill $fake_pid 2>/dev/null || true" EXIT

assert "Fake server is running" -d "/proc/$fake_pid"

say "Running sntpd against it"
# "wait" after "kill" can itself fail under set -e (e.g. if sntpd already
# exited), so it gets its own "|| true"; the trailing "; true" only covers
# the compound list's own exit status, not wait's.
out=$("$SNTPD" -d -n -l debug -i 15 127.0.0.1 2>&1 & sntpd_pid=$!; sleep 3; kill "$sntpd_pid" 2>/dev/null; wait "$sntpd_pid" 2>/dev/null || true; true)

# "Connected to NTP server." only means connect(2) on the UDP socket
# succeeded, which happens whether or not anything replies.  Assert on
# rfc1305print()'s stats line instead: it is only reached after a
# reply has passed every RFC 4330 cross-check (src/sntpd.c:371-378).
assert "sntpd completed a validated NTP round trip" \
       -n "$(echo "$out" | grep -F 'Day   Second      Elapsed')"
assert "sntpd rejected no packets" \
       -z "$(echo "$out" | grep -F 'rejected packet')"
