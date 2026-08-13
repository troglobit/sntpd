#!/bin/sh
# A Kiss-o'-Death is the server telling us to go away, RFC 4330 section
# 8, and the four characters in the reference identifier say how badly.
# DENY and RSTR retire the server for the rest of the run, RATE only
# slows us down, and a client that ignores either is the reason servers
# send them.
#
# Nine cases: the retirement and the failover it forces, the same thing
# with the cross-checks turned off, a DENY answering the retry probe
# rather than the association, a forged one answering nothing at all,
# RATE proving the retirement is specific to the two codes that mean it,
# a stratum 0 packet that is no reply of a server's at all, the code
# reaching the log filtered, a name whose every address goes with it,
# and last the one path where sntpd gives up and leaves the field.
set -eu

# shellcheck source=/dev/null
. "$(dirname "$0")/lib/setup.sh"

enter_namespace "$@"

# As in prefer.sh: named so a case that swaps a fake need not re-arm it.
end_case()
{
	# $pids is deliberately unquoted, it is a space-separated list and
	# each PID must reach kill/wait as its own word.
	# shellcheck disable=SC2086
	kill $pids 2>/dev/null || true
	# shellcheck disable=SC2086
	wait $pids 2>/dev/null || true
	rm -f "$log" "$hosts"
	pids=
	hosts=
	log=
}

pids=
hosts=
log=
trap end_case EXIT

# -m 1 -i 1 so a poll costs a second rather than the RFC's fifteen, and
# -p 0 keeps sntpd's own server mode off port 123, which the fake needs.
run_case()
{
	pattern=$1
	shift

	log=$(mktemp)
	"$SNTPD" -d -n -l debug -p 0 -m 1 -i 1 "$@" >"$log" 2>&1 &
	sntpd=$!
	pids="$pids $sntpd"

	retry "grep -qF '$pattern' $log" 50 0.2 || true
}

# peer_kod()'s two lines, spelled once: each is waited on and then
# counted, and one string per fact keeps the wait and the count from
# drifting apart.
deny='Server 127.0.0.1 sent KoD DENY, will not query it again'
rate='Server 127.0.0.1 sent KoD RATE, backing off'

# One validated round trip per poll, as in prefer.sh: the statistics
# line, day and second and then numbers.  Nothing a KoD sends ever
# reaches it, so it counts polls that got real time back.
polls='grep -cE "^[0-9]+ [0-9]+\.[0-9]{3} +-?[0-9]"'

say "A server that says DENY is dropped for the rest of the run"
# -u as well as -k: RFC 5905 section 7.4 says a kiss o' death carries
# LI 3, so this is the form a real server sends, and the form that is
# rejected as LI==3 by the section 5 checks if the code is not read
# before them.  Case 2 leaves it off, since a stratum 0 reply has to be
# honoured whether or not the server bothered with the leap indicator.
"$FAKE" -p 123 -k DENY -u &
pids="$!"
"$FAKE" -p 1123 &
pids="$pids $!"

run_case "$deny" 127.0.0.1:123 127.0.0.1:1123
retry "[ \"\$($polls $log)\" -ge 2 ]" 50 0.2 || true

if ! grep -qF "$deny" "$log"; then
	cat "$log"
	fail "the DENY was ignored"
fi

assert "the reply itself was thrown away" \
       -n "$(grep -F 'rejected packet: KoD' "$log")"
assert "so it was retired rather than merely unreachable" \
       -n "$(grep -F 'Server 127.0.0.1:123 retired, rotating' "$log")"
# The fake answers every probe, so a second KoD would mean a second
# probe: exactly one is how the log says we never went back.
assert "it was asked exactly once" "$(grep -cF "$deny" "$log")" -eq 1
assert "and the fallback is being polled, so this window is real" \
       "$(eval "$polls" "$log")" -ge 2
end_case

say "Trusting the network does not make a DENY optional"
"$FAKE" -p 123 -k DENY &
pids="$!"
"$FAKE" -p 1123 &
pids="$pids $!"

# -t turns off the RFC 4330 section 5 cross-checks.  Section 8 is not
# one of them: it is what the server asked for, not what we insist on.
run_case 'Trying NTP server 127.0.0.1:1123' -t 127.0.0.1:123 127.0.0.1:1123

assert "the DENY was honoured with -t as well" -n "$(grep -F "$deny" "$log")"
assert "so sntpd moved to the second server" \
       -n "$(grep -E 'Trying NTP server 127\.0\.0\.1:1123' "$log")"
end_case

say "A DENY answering the retry probe retires the server too"
"$FAKE" -p 1123 &
pids="$!"
"$FAKE" -p 123 -k DENY -u &
pids="$pids $!"

# The association is on :1123 and never leaves it; :123 is the server
# sntpd would rather have, and asks after once per poll.  It answers
# DENY, which is about the client and not the socket, so asking again
# from a fresh port would get the same answer.
run_case "$deny" 127.0.0.1:1123 127.0.0.1:123,prefer
retry "[ \"\$($polls $log)\" -ge 2 ]" 50 0.2 || true

assert "the probe's DENY retired the preferred server" \
       -n "$(grep -F "$deny" "$log")"
assert "the probe reply was turned down" \
       -n "$(grep -F 'Preferred server 127.0.0.1:123 unusable: KoD' "$log")"
# The second reply is a poll pass that would have probed again, had the
# server still been a candidate.
assert "the daemon polled on, so this window is real" \
       "$(eval "$polls" "$log")" -ge 2
assert "so the probing stopped at one" \
       "$(grep -cF 'Probing preferred server 127.0.0.1:123' "$log")" -eq 1
assert "nor rotated to" \
       -z "$(grep -E 'Trying NTP server 127\.0\.0\.1:123$' "$log")"
end_case

say "A KoD nobody asked for is not obeyed"
"$FAKE" -p 123 -k DENY -b &
pids="$!"

# Retiring a server for the rest of the run is worth mounting a spoof
# for, and the origin timestamp is the only thing standing in the way,
# so it is checked before the code is read at all.  This fake answers
# DENY with an origin nobody sent, which is what an off-path forgery
# looks like, and the rejection it gets is the ORG one.
run_case 'rejected packet: ORG!=sent' 127.0.0.1:123
retry "[ \"\$(grep -cF 'rejected packet: ORG!=sent' $log)\" -ge 2 ]" 50 0.2 || true

assert "the forged reply was turned down on the timestamp" \
       "$(grep -cF 'rejected packet: ORG!=sent' "$log")" -ge 2
assert "and its DENY went unread, so the server was kept" \
       -z "$(grep -F "$deny" "$log")"
assert "and sntpd stayed on it" -z "$(grep -F 'No usable NTP server left' "$log")"
end_case

say "RATE only slows us down, the server stays in the list"
"$FAKE" -p 123 -k RATE &
pids="$!"

# One server, so if RATE retired it sntpd would run out of servers and
# exit.  It does not: RATE is the one code we answer by waiting longer,
# which is why the count below can climb at all.
run_case "$rate" 127.0.0.1:123
retry "[ \"\$(grep -cF '$rate' $log)\" -ge 2 ]" 50 0.2 || true

assert "the RATE was recognised, more than once" \
       "$(grep -cF "$rate" "$log")" -ge 2
# Any retirement at all, not just a DENY one: the mistake to catch is
# RATE being read as one of the two codes that mean stop.
assert "so the server was kept and kept being polled" \
       -z "$(grep -F 'will not query it again' "$log")"
assert "and sntpd did not give up" \
       -z "$(grep -F 'No usable NTP server left' "$log")"
end_case

say "A stratum 0 packet that is no server reply retires nobody"
# Mode 3 is a client packet, which a server never sends.  Stratum 0 in
# one of those is a server that is broken, not a server refusing us, and
# reading it as a refusal would let one malformed reply cost us the
# server for the rest of the run.  The section 5 checks turn it away
# instead, as they always did.
"$FAKE" -p 123 -k DENY -m 3 &
pids="$!"

run_case 'rejected packet: MODE!=3' 127.0.0.1:123
retry "[ \"\$(grep -cF 'rejected packet: MODE!=3' $log)\" -ge 2 ]" 50 0.2 || true

assert "the malformed reply was turned away on its mode" \
       "$(grep -cF 'rejected packet: MODE!=3' "$log")" -ge 2
assert "and its DENY was never read, so the server was kept" \
       -z "$(grep -F 'will not query it again' "$log")"
assert "and sntpd stayed on it" -z "$(grep -F 'No usable NTP server left' "$log")"
end_case

say "A reference identifier is not a free hand at the log"
# Four bytes of the server's choosing, here D, newline, N, Y, which is
# not one of the three codes and so takes the branch that logs the code
# as it came.  A server we have just been refused by does not get to
# write lines of its own into syslog.
"$FAKE" -p 123 -k "$(printf 'D\nNY')" -u &
pids="$!"

run_case 'sent KoD D.NY' 127.0.0.1:123
retry "[ \"\$(grep -cF 'sent KoD D.NY' $log)\" -ge 2 ]" 50 0.2 || true

assert "the unprintable byte was replaced" \
       "$(grep -cF 'Server 127.0.0.1 sent KoD D.NY' "$log")" -ge 2
assert "so nothing of the server's got its own line" \
       -z "$(grep -x 'NY' "$log")"
# The effect, not the wording: a code we do not know is not one of the
# two that mean stop, so nothing may be retired over it.
assert "an unknown code retired nobody" \
       -z "$(grep -F 'retired, rotating' "$log")"
end_case

say "A retired name is retired whole, every address of it"
# A name is as many candidates as it has addresses, and rotation walks
# those before it walks the list, so retiring one has to take all three
# with it.  All of 127/8 is loopback, so the one fake on INADDR_ANY is
# every address of the name, and whichever the resolver hands out first
# answers DENY.
#
# Soft skipped rather than skip(), which would throw away the five cases
# above along with this one.
hosts=$(mktemp)
cat /etc/hosts > "$hosts"
printf '127.0.0.1\tk3.test\n127.0.0.2\tk3.test\n127.0.0.3\tk3.test\n' >> "$hosts"

if unshare --mount -- mount --bind "$hosts" /etc/hosts 2>/dev/null; then
	"$FAKE" -p 123 -k DENY &
	pids="$pids $!"
	"$FAKE" -p 1123 &
	pids="$pids $!"

	log=$(mktemp)
	unshare --mount -- sh -c "mount --bind $hosts /etc/hosts && exec \"\$@\"" \
		-- "$SNTPD" -d -n -l debug -p 0 -m 1 -i 1 k3.test 127.0.0.1:1123 >"$log" 2>&1 &
	pids="$pids $!"
	retry "grep -qE 'Trying NTP server 127\.0\.0\.1:1123' $log" 50 0.2 || true

	assert "the name really was three candidates" \
	       -n "$(grep -E 'Resolved k3\.test to [^,]+, address 1 of 3' "$log")"
	assert "and one DENY was the end of all three" \
	       "$(grep -cE 'Resolved k3\.test to' "$log")" -eq 1
	assert "so the next server took over" \
	       -n "$(grep -E 'Trying NTP server 127\.0\.0\.1:1123' "$log")"
else
	say "Cannot bind mount /etc/hosts, skipping the multi-address case"
fi
end_case

say "The last server saying DENY leaves sntpd with nothing to do"
"$FAKE" -p 123 -k DENY &
pids="$!"

# The one place sntpd terminates on purpose.  Serving a reference clock
# that stopped advancing would be worse than being gone, and the exit
# status is what tells the supervisor to back off and try again later
# rather than treat the job as done.
run_case 'Stopping' 127.0.0.1:123

if ! grep -q 'Stopping' "$log"; then
	cat "$log"
	fail "sntpd kept running with no server left"
fi

rc=0
wait "$sntpd" || rc=$?

assert "sntpd said why it was leaving" \
       -n "$(grep -F 'No usable NTP server left' "$log")"
assert "and left with a failure status" "$rc" -ne 0
end_case
