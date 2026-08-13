#!/bin/sh
# Shared helpers for the sntpd test suite.

color_reset='\e[0m'
fg_red='\e[1;31m'
fg_green='\e[1;32m'
fg_yellow='\e[1;33m'

log()
{
	printf "\e[2m[%s]\e[0m %b%b%b %s\n" "$(basename "$0" .sh)" "$1" "$2" "$color_reset" "$3"
}

say()  { log "$fg_yellow" • "$*"; }
skip() { log "$fg_yellow" − "$*"; exit 77; }
fail() { log "$fg_red"    ✘ "$*"; exit 99; }

assert()
{
	__msg=$1
	shift

	if [ ! "$@" ]; then
		log "$fg_red" ✘ "$__msg ($*)"
		exit 1
	fi
	log "$fg_green" ✔ "$__msg"
}

# retry CMD [TRIES] [SLEEP] -- succeed as soon as CMD does
retry()
{
	__cmd=$1
	__n=${2:-20}
	__s=${3:-0.2}

	while [ "$__n" -gt 0 ]; do
		if eval "$__cmd"; then
			return 0
		fi
		__n=$((__n - 1))
		sleep "$__s"
	done

	return 1
}

check_dep()
{
	command -v "$1" >/dev/null 2>&1 || skip "Cannot find $1, skipping test."
}

# Re-run the calling test inside a private network namespace, once.
# Tests bind port 123 on their own loopback, so they neither need root
# nor collide with each other.
enter_namespace()
{
	check_dep unshare
	check_dep ip

	[ -n "${IN_NAMESPACE:-}" ] && return 0

	IN_NAMESPACE=1
	export IN_NAMESPACE

	exec "$(dirname "$0")/lib/start.sh" "$0" "$@"
}

SNTPD="${top_builddir:-..}/src/sntpd"
FAKE="${builddir:-.}/fake-ntpd"
export SNTPD FAKE
