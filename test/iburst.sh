#!/bin/sh
# iburst shortens failover: eight packets two seconds apart instead of
# one poll interval apart.
set -eu

# shellcheck source=/dev/null
. "$(dirname "$0")/lib/setup.sh"

enter_namespace "$@"

# Run sntpd against SNTPD_ARGS, retrying PATTERN in its log up to TRIES
# times SLEEP seconds apart.  The caller starts any fakes first and
# seeds $pids with their PIDs; this appends sntpd's and arms the trap.
# Leaves the result at $log for the caller's asserts; end_case() tears
# everything down.
run_case()
{
	pattern=$1
	tries=$2
	sleep_s=$3
	shift 3

	log=$(mktemp)
	"$SNTPD" -d -n -l debug -i 600 -p 0 "$@" >"$log" 2>&1 &
	pids="$pids $!"
	# shellcheck disable=SC2064
	trap "kill $pids 2>/dev/null || true; wait $pids 2>/dev/null || true; rm -f $log" EXIT

	retry "grep -qi '$pattern' $log" "$tries" "$sleep_s" || true
}

end_case()
{
	# $pids is deliberately unquoted: it is a space-separated list of
	# PIDs and each one must reach kill/wait as its own word.
	# shellcheck disable=SC2086
	kill $pids 2>/dev/null || true
	# shellcheck disable=SC2086
	wait $pids 2>/dev/null || true
	rm -f "$log"
	trap - EXIT
	pids=
}

say "Silent server with iburst, live fallback, default poll interval"
"$FAKE" -p 123 -q &
pids="$!"
"$FAKE" -p 1123 &
pids="$pids $!"

# No -m here.  With the 600s default interval and no iburst this would
# take eight poll intervals; iburst is the whole reason it does not.
run_case unreachable 45 1 127.0.0.1:123,iburst 127.0.0.1:1123

if ! grep -qi 'unreachable' "$log"; then
	cat "$log"
	fail "iburst did not shorten failover"
fi

assert "iburst detected the dead server quickly" -n "$(grep -i 'unreachable' "$log")"
assert "no RFC 4330 warning, the floor was untouched" -z "$(grep -i 'RFC 4330' "$log")"
end_case

say "Control: same scenario without iburst does not rotate in a much shorter window"
"$FAKE" -p 123 -q &
pids="$!"
"$FAKE" -p 1123 &
pids="$pids $!"

# Without iburst, misses land about every MIN_INTERVAL (15s) once the
# first one arms it, so by 18s lost is 2 or 3 against the 8 needed to
# rotate -- long enough to prove the negative, far short of the ~113s
# it would actually take to rotate.
run_case unreachable 18 1 127.0.0.1:123 127.0.0.1:1123

assert "without iburst, the same window is not enough to rotate" \
       -z "$(grep -i 'unreachable' "$log")"
end_case

say "Control: the RFC 4330 warning still fires when the floor really is lowered"
"$FAKE" -p 1123 &
pids="$!"

run_case 'RFC 4330' 20 0.2 -m 1 127.0.0.1:1123

assert "the low floor warning still fires on its own trigger" \
       -n "$(grep -i 'RFC 4330' "$log")"
end_case
