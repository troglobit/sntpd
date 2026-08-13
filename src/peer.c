/* NTP server list
 *
 * Copyright (C) 2026  Joachim Wiberg <troglobit@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2,
 * June 1991) as published by the Free Software Foundation.  At the
 * time of writing, that license was published by the FSF with the URL
 * http://www.gnu.org/copyleft/gpl.html, and is incorporated herein by
 * reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <ctype.h>

#include "peer.h"

int min_interval = MIN_INTERVAL;

static struct ntp_server servers[MAX_SERVERS];
static int num_servers;
static int active;
static int backoff;
static int candidate_hits;

/*
 * Split arg into host and port.  Accepted forms:
 *
 *     host              host, default port
 *     host:123          host, port 123
 *     2001:db8::1       literal address, default port
 *     [2001:db8::1]     literal address, default port
 *     [2001:db8::1]:123 literal address, port 123
 *
 * An unbracketed string with more than one colon is a literal IPv6
 * address, so the last colon does not introduce a port.  Modifies arg
 * in place and points *host into it.
 */
static int split_hostport(char *arg, char **host, uint16_t *port)
{
	char *ptr;

	*port = 0;

	if (*arg == '[') {
		ptr = strchr(arg, ']');
		if (!ptr)
			return -1;

		*ptr++ = 0;
		*host = arg + 1;

		if (*ptr == ':')
			*port = atoi(ptr + 1);
		else if (*ptr)
			return -1;

		return 0;
	}

	*host = arg;

	ptr = strchr(arg, ':');
	if (ptr && !strchr(ptr + 1, ':')) {
		*ptr++ = 0;
		*port = atoi(ptr);
	}

	return 0;
}

static int parse_opts(struct ntp_server *srv, char *opts)
{
	char *tok, *ptr = NULL;

	for (tok = strtok_r(opts, ",", &ptr); tok; tok = strtok_r(NULL, ",", &ptr)) {
		if (!strcmp(tok, "iburst"))
			srv->iburst = 1;
		else if (!strcmp(tok, "prefer"))
			srv->prefer = 1;
		else {
			ERR(0, "Unknown server option '%s'", tok);
			return -1;
		}
	}

	return 0;
}

int peer_add(const char *arg)
{
	struct ntp_server srv;
	char *buf, *opts, *host;
	uint16_t port;

	if (num_servers >= MAX_SERVERS) {
		ERR(0, "Too many servers, at most %d, ignoring '%s'", MAX_SERVERS, arg);
		return -1;
	}

	buf = strdup(arg);
	if (!buf) {
		ERR(errno, "Failed allocating memory for '%s'", arg);
		return -1;
	}

	memset(&srv, 0, sizeof(srv));

	opts = strchr(buf, ',');
	if (opts)
		*opts++ = 0;

	if (split_hostport(buf, &host, &port) || (opts && parse_opts(&srv, opts))) {
		ERR(0, "Malformed server '%s'", arg);
		free(buf);
		return -1;
	}

	if (!*host) {
		/*
		 * No argument at all means "use the default", handled by
		 * the caller before ever reaching peer_add().  An argument
		 * that is present but empty is a broken config and must
		 * fail loudly instead of silently substituting a default.
		 */
		ERR(0, "Empty server name");
		free(buf);
		return -1;
	}

	srv.host = strdup(host);
	free(buf);
	if (!srv.host) {
		ERR(errno, "Failed allocating memory for '%s'", arg);
		return -1;
	}

	srv.port = port ?: NTP_PORT;
	servers[num_servers++] = srv;

	return 0;
}

int peer_count(void)
{
	return num_servers;
}

/* Drop the list.  Only used by the test suite. */
void peer_reset(void)
{
	int i;

	for (i = 0; i < num_servers; i++)
		free(servers[i].host);

	memset(servers, 0, sizeof(servers));
	num_servers = 0;
	active = 0;
	backoff = min_interval;
	candidate_hits = 0;
}

struct ntp_server *peer_get(int idx)
{
	if (idx < 0 || idx >= num_servers)
		return NULL;

	return &servers[idx];
}

struct ntp_server *peer_active(void)
{
	return peer_get(active);
}

/*
 * Bring the backoff up to the floor.  Covers both the initial zero and
 * a floor raised by -m after the fact.
 */
static void backoff_floor(void)
{
	if (backoff < min_interval)
		backoff = min_interval;
}

/*
 * Wait twice as long, from the floor rather than from under it: a run
 * starts at zero, so doubling what is there would be no wait at all.
 */
static void backoff_double(void)
{
	backoff_floor();
	if (backoff < INT_MAX / 2)
		backoff *= 2;
}

void peer_rx(struct ntp_server *srv)
{
	srv->reach = (uint8_t)(srv->reach << 1) | 1;
	srv->lost  = 0;
	srv->burst = 0;
	backoff = min_interval;
}

/*
 * Record how many addresses the last resolve returned, so rotation
 * knows when this server's addresses are exhausted.  A shrinking count
 * pulls the cursor back into range, wrapping the way the resolver's
 * own pick does so the two cannot drift apart.
 */
void peer_naddr(struct ntp_server *srv, int n)
{
	srv->naddr = n;
	srv->addr_idx = n > 0 ? srv->addr_idx % n : 0;
}

void peer_timeout(struct ntp_server *srv)
{
	srv->reach = (uint8_t)(srv->reach << 1);
	srv->lost++;

	/*
	 * Arm on the transition into trouble, not on every miss, so a
	 * burst runs once per outage instead of restarting forever.
	 */
	if (srv->iburst && srv->lost == 1)
		srv->burst = BCOUNT;
}

/* One probe went out.  Counts it off the armed burst, if any is running. */
void peer_probed(struct ntp_server *srv)
{
	if (srv->burst > 0)
		srv->burst--;
}

/*
 * A fresh association has reach 0 because RFC 5905 says so, which
 * cannot be told apart from eight consecutive misses.  Count the
 * misses instead and leave reach to the logs, where the bit pattern
 * says which of the last eight polls went missing.
 */
int peer_unreachable(struct ntp_server *srv)
{
	return srv->lost >= BCOUNT;
}

/* Retired by KoD, never to be queried again this run. */
int peer_retired(struct ntp_server *srv)
{
	return srv->dead;
}

/*
 * RFC 4330 section 8.  A stratum 0 reply is a Kiss-o'-Death and the
 * reference identifier says why.  DENY and RSTR are permanent for this
 * run; RATE is the server asking us to slow down, which the existing
 * backoff already expresses.  Any other code is logged and otherwise
 * left alone: the reply is still dropped, so an unknown code costs the
 * server a miss and eventually rotation.
 */
void peer_kod(struct ntp_server *srv, const char *code)
{
	char str[5];
	int i;

	/*
	 * Four bytes chosen by a server that has just told us to go away,
	 * on their way into the log.  Anything unprintable becomes a dot,
	 * or that server gets to write newlines and terminal escapes into
	 * our syslog.
	 */
	for (i = 0; i < 4; i++)
		str[i] = isprint((unsigned char)code[i]) ? code[i] : '.';
	str[4] = 0;

	if (!strncmp(code, "DENY", 4) || !strncmp(code, "RSTR", 4)) {
		ERR(0, "Server %s sent KoD %s, will not query it again", srv->host, str);
		srv->dead = 1;
		return;
	}

	if (!strncmp(code, "RATE", 4)) {
		LOG("Server %s sent KoD RATE, backing off", srv->host);
		backoff_double();
		return;
	}

	LOG("Server %s sent KoD %s", srv->host, str);
}

/*
 * Move to the next usable candidate, skipping any server retired by
 * KoD.  A hostname with several A records is several candidates, so
 * the rest of the current server's addresses come before the next
 * server.  Every full pass over the servers without a reachable one
 * doubles the backoff, so a network that is down does not turn into a
 * packet storm; doubling per server rather than per candidate keeps a
 * pool hostname from quadrupling it on every lap.  Returns the new
 * active index, or -1 when every server is dead.
 */
int peer_rotate(void)
{
	struct ntp_server *srv = &servers[active];
	int i, next;

	backoff_floor();

	/*
	 * Any move drops a part-finished run of replies from the
	 * candidate.  It has to be dropped here because this is the only
	 * place that can: the ring may well be moving onto the preferred
	 * entry itself, and from then on there is no candidate, so no
	 * probe, and nothing else that could ever clear the count.  A
	 * reply banked before that would sit there and win the next
	 * switch-back on its own.
	 */
	candidate_hits = 0;

	if (!srv->dead && srv->addr_idx + 1 < srv->naddr) {
		srv->addr_idx++;
		return active;
	}

	srv->addr_idx = 0;

	for (i = 1; i <= num_servers; i++) {
		next = (active + i) % num_servers;
		if (servers[next].dead)
			continue;

		if (next <= active)
			backoff_double();

		active = next;
		return active;
	}

	return -1;
}

/*
 * Begin a fresh attempt at whichever server is active.  The miss
 * counter belongs to the attempt rather than to the server:
 * peer_rotate() leaves it alone so peer_spacing() can still tell an
 * entry that has been failing from one that has not, but a server we
 * come back to must not be condemned before its first probe.  The
 * doubled backoff is what throttles a network that is down.
 *
 * burst is cleared for the same reason: a server rotated back to would
 * otherwise inherit a stale count from the outage that took it out
 * last time, and be sent a burst before it has missed anything this
 * time round.
 *
 * addr_idx is the one thing left alone.  Rotating to the next address
 * of the same server advances the cursor and then comes straight here,
 * so clearing it would pin the daemon to the first address forever.
 */
struct ntp_server *peer_activate(void)
{
	struct ntp_server *srv = peer_active();

	if (srv) {
		srv->lost  = 0;
		srv->reach = 0;
		srv->burst = 0;
	}

	return srv;
}

/*
 * The server we would rather be using.  Only an explicit prefer marks
 * one; the list is otherwise a flat ring with no ranking, so without
 * the keyword there is nothing to return to and none of the
 * switch-back machinery ever runs.  Returns -1 when there is nothing
 * to go back to, which includes already being there.
 */
static int candidate(void)
{
	int i;

	for (i = 0; i < num_servers; i++) {
		if (!servers[i].prefer)
			continue;

		if (i == active || servers[i].dead)
			return -1;

		return i;
	}

	return -1;
}

struct ntp_server *peer_candidate(void)
{
	return peer_get(candidate());
}

void peer_candidate_rx(void)
{
	candidate_hits++;
}

void peer_candidate_timeout(void)
{
	candidate_hits = 0;
}

/*
 * Two consecutive replies before we move.  One is not enough: every
 * switch resets the phase lock loop, so a server that answers only
 * intermittently would keep sntpd permanently unsynchronised.
 */
int peer_switchback(void)
{
	int i = candidate();

	if (i < 0 || candidate_hits < 2)
		return 0;

	active = i;
	candidate_hits = 0;

	return 1;
}

/*
 * Seconds between probes.  RFC 4330 section 10 wants at least
 * min_interval between them and exponential backoff on failure, so a
 * server that is answering is polled at the configured interval and
 * one that has missed anything drops to the backoff.
 *
 * Safe to call as often as you like: the only state it touches is the
 * backoff floor, and raising something to a floor twice changes
 * nothing.
 */
int peer_spacing(struct ntp_server *srv, int cycle_time)
{
	/*
	 * RFC 4330's floor applies between bursts, not inside one: a
	 * burst is a single poll that happens to be eight packets.  The
	 * packets are pipelined, so this spacing is independent of how
	 * long each one is given to answer.
	 */
	if (srv && srv->burst > 0)
		return BTIME;

	if (!srv || srv->lost == 0)
		return cycle_time;

	backoff_floor();

	return backoff < cycle_time ? backoff : cycle_time;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
