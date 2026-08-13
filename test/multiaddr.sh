#!/bin/sh
# A name is as many candidates as it has addresses.  Three here, and
# only the last of them answers, so sntpd has to walk past two dead
# ones to sync -- which one address per server could never do.
set -eu

# shellcheck source=/dev/null
. "$(dirname "$0")/lib/setup.sh"

enter_namespace "$@"

check_dep mount

# The fake server is IPv4 only, so probes to the two IPv6 addresses
# draw an ICMP port unreachable and are charged as lost.  ::1 is on
# loopback already, ::2 has to be added before the resolver offers it.
ip addr add ::2/128 dev lo 2>/dev/null || skip "No IPv6 on loopback, skipping test."

hosts=$(mktemp)
cat /etc/hosts > "$hosts"
printf '::1\tm3.test\n::2\tm3.test\n127.0.0.1\tm3.test\n' >> "$hosts"

# --map-root-user already granted the capability, so a nested mount
# namespace costs no privileges.  Probe it in a throwaway one: the
# mount goes away with the namespace it was made in.
unshare --mount -- mount --bind "$hosts" /etc/hosts 2>/dev/null ||
	skip "Cannot bind mount /etc/hosts, skipping test."

say "One name, three addresses, only the third answers"
"$FAKE" -p 1123 &
alive=$!

log=$(mktemp)
# -m 1 so each dead address costs seconds rather than poll intervals.
unshare --mount -- sh -c "mount --bind $hosts /etc/hosts && exec \"\$@\"" \
	-- "$SNTPD" -d -n -l debug -p 0 -m 1 -i 600 m3.test:1123 >"$log" 2>&1 &
sntpd=$!
# shellcheck disable=SC2064
trap "kill $alive $sntpd 2>/dev/null || true; rm -f $hosts $log" EXIT

if ! retry "grep -qE '^Day +Second' $log" 90 1; then
	cat "$log"
	fail "sntpd never got an answer out of the name's three addresses"
fi

tried=$(grep -oE 'Resolved m3\.test to [^,]+' "$log" | sort -u | grep -c .)
total=$(grep -cE 'Resolved m3\.test to' "$log")

assert "each of the name's addresses was a candidate of its own" "$tried" -eq 3
assert "and it stayed on the one that answered" "$total" -eq 3
