#!/bin/sh
# prefer names the one server sntpd comes back to.  The ring only ever
# advances on failure, so without this a single blip on a local
# stratum-1 strands the daemon on the pool for the rest of the run.
#
# Three cases: the blip and the return, then a pair that shows the
# keyword is what arms any of it -- the same two servers probe the
# preferred one or nothing at all, depending on a single word.
set -eu

# shellcheck source=/dev/null
. "$(dirname "$0")/lib/setup.sh"

enter_namespace "$@"

# As in iburst.sh, but the trap is named rather than expanded, so a case
# that replaces a fake halfway through does not have to re-arm it.
end_case()
{
	# $pids is deliberately unquoted: it is a space-separated list of
	# PIDs and each one must reach kill/wait as its own word.
	# shellcheck disable=SC2086
	kill $pids 2>/dev/null || true
	# shellcheck disable=SC2086
	wait $pids 2>/dev/null || true
	rm -f "$log"
	pids=
}

pids=
log=/dev/null
trap end_case EXIT

# The caller starts its fakes and seeds $pids, this appends sntpd's and
# leaves the log at $log for the asserts.
#
# -m 1 -i 1 is why this test costs seconds: the retry probe goes out
# once per poll cycle, so the poll cycle is what a switch-back costs.
# -p 0 keeps sntpd's own server mode off port 123, which the fake needs.
run_case()
{
	pattern=$1
	tries=$2
	sleep_s=$3
	shift 3

	log=$(mktemp)
	"$SNTPD" -d -n -l debug -p 0 -m 1 -i 1 "$@" >"$log" 2>&1 &
	pids="$pids $!"

	retry "grep -qE '$pattern' $log" "$tries" "$sleep_s" || true
}

# One validated round trip per poll: rfc1305print()'s statistics line,
# which is day, second and then numbers.  The digit after the timestamp
# is what keeps the "rejected packet" error, printed with the same two
# leading fields, from counting as a poll.
polls='grep -cE "^[0-9]+ [0-9]+\.[0-9]{3} +-?[0-9]"'

say "Preferred server goes silent, sntpd falls back, then it comes back"
"$FAKE" -p 123 -q &		# preferred: answers nothing yet
pref=$!
pids="$pref"
"$FAKE" -p 1123 &		# fallback: always answers
pids="$pids $!"

run_case 'Trying NTP server 127\.0\.0\.1:1123' 200 0.2 \
	 127.0.0.1:123,prefer 127.0.0.1:1123

if ! grep -qE 'Trying NTP server 127\.0\.0\.1:1123' "$log"; then
	cat "$log"
	fail "sntpd never left the silent preferred server"
fi

assert "the preferred server ran out of chances like any other" \
       -n "$(grep -F 'Server 127.0.0.1:123 unreachable' "$log")"

say "Bringing the preferred server back"
kill $pref 2>/dev/null || true
wait $pref 2>/dev/null || true
"$FAKE" -p 123 &
pids="$pids $!"

if ! retry "grep -qF 'Switching back to preferred server 127.0.0.1:123' $log" 200 0.2; then
	cat "$log"
	fail "sntpd never switched back to the preferred server"
fi

# The gate got us here, so assert what it did not.  A switch-back is not
# a rotation: the ring is still where failover left it, and one more
# "Trying" line would mean the return trip went the long way round.
assert "it came back without the ring moving again" \
       "$(grep -cE 'Trying NTP server' "$log")" -eq 1

# Exactly two replies, no fewer: only a reply that passed every check is
# logged here, anything else resets the run, and once the switch is made
# the preferred server is the association and is never probed again.  So
# the count cannot drift either side of the anti-flap threshold.
assert "it took two usable replies in a row, no fewer" \
       "$(grep -cF 'Preferred server 127.0.0.1:123 answered' "$log")" -eq 2

# Ordering, not just arrival: probing must start only once sntpd has
# left the preferred server, or it is probing its own association.  The
# defaults are lopsided on purpose, so a missing line fails the compare
# from whichever side it goes missing on.
rotated=$(grep -nE 'Trying NTP server 127\.0\.0\.1:1123' "$log" | head -1 | cut -d: -f1)
probed=$(grep -nF 'Probing preferred server 127.0.0.1:123' "$log" | head -1 | cut -d: -f1)

assert "probing started only after the fallback took over" \
       "${rotated:-999999}" -lt "${probed:-0}"

# The silent stretch has to have been charged, or two replies in a row
# is not what the switch-back actually waited for.
assert "an unanswered probe was given up on" \
       -n "$(grep -F 'Preferred server 127.0.0.1:123 did not answer' "$log")"
end_case

say "A preferred server answering unsynchronised does not win the switch"
"$FAKE" -p 1123 &
pids="$!"
"$FAKE" -p 123 -u &		# answers every probe, with LI == 3
pids="$pids $!"

# The motivating hardware in its worst state: a GPS receiver powered up
# without a fix answers, but its time is no good, and RFC 4330 says so
# in the leap indicator.  Accepting the probe on arrival alone would
# switch to it, reject every packet it then sent as the association,
# rotate away and come straight back, once per outage cycle forever.
run_case 'Preferred server 127\.0\.0\.1:123 unusable: LI==3' 50 0.2 \
	 127.0.0.1:1123 127.0.0.1:123,prefer
retry "[ \"\$($polls $log)\" -ge 3 ]" 50 0.2 || true

assert "the daemon polled on, so this window is real" \
       "$(eval "$polls" "$log")" -ge 3
assert "the probe was answered and the answer turned down, twice over" \
       "$(grep -cF 'Preferred server 127.0.0.1:123 unusable: LI==3' "$log")" -ge 2
assert "so no run of replies ever built up" \
       -z "$(grep -F 'Preferred server 127.0.0.1:123 answered' "$log")"
assert "and it never won the switch" \
       -z "$(grep -F 'Switching back' "$log")"
end_case

say "Dormancy: a list with no prefer keyword probes nothing"
"$FAKE" -p 1123 &
pids="$!"
"$FAKE" -p 123 &
pids="$pids $!"

# Both answer, so nothing fails over and the ring never moves.  The
# point is what does not happen: with no prefer there is no candidate,
# so no retry probe and no second socket, ever.  Three round trips is
# three poll intervals, against the two the case below needs to switch
# on the same pair of servers, and counting them beats sleeping for
# them: the window is then a fact the log can be held to.
run_case '^Day +Second' 50 0.2 127.0.0.1:1123 127.0.0.1:123
retry "[ \"\$($polls $log)\" -ge 3 ]" 50 0.2 || true

assert "the flat list polled on, so the loop really did run" \
       "$(eval "$polls" "$log")" -ge 3
assert "no retry probe was ever sent" \
       -z "$(grep -F 'Probing preferred server' "$log")"
assert "and nothing was switched back to" \
       -z "$(grep -F 'Switching back' "$log")"
end_case

say "Control: the same pair, one word added, switches within two polls"
"$FAKE" -p 1123 &
pids="$!"
"$FAKE" -p 123 &
pids="$pids $!"

# No failure anywhere here.  sntpd starts on the first entry, as it
# always does, and prefer is the only reason it does not stay.
run_case 'Switching back to preferred server 127\.0\.0\.1:123' 30 0.5 \
	 127.0.0.1:1123 127.0.0.1:123,prefer

assert "the preferred server was probed from the start" \
       -n "$(grep -F 'Probing preferred server 127.0.0.1:123' "$log")"
assert "and sntpd moved to it without anything having failed" \
       -n "$(grep -F 'Switching back to preferred server 127.0.0.1:123' "$log")"
assert "the ring never moved, nothing was unreachable" \
       -z "$(grep -i 'unreachable' "$log")"
end_case
