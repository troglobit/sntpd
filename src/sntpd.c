/* Simple NTP client
 *
 * Copyright (C) 1997-2015  Larry Doolittle <larry@doolittle.boa.org>
 * Copyright (C) 2010-2022  Joachim Wiberg <troglobit@gmail.com>
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

/*
 * Possible future improvements:
 *    - Support leap second processing
 *    - Support multiple (interleaved) servers
 *
 * If the compile gives you any flak, check below in the section
 * labelled "XXX fixme - non-automatic build configuration".
 */

#include "config.h"
#include <err.h>
#include <getopt.h>
#include <resolv.h>
#include <signal.h>
#include <netdb.h>		/* getaddrinfo -> gethostbyname */
#include <time.h>
#ifdef PRECISION_SIOCGSTAMP
#include <sys/ioctl.h>
#endif

#include "sntpd.h"
#include "peer.h"

int dry = 0;			/* Dry run, no time corrections */
int initial_freq = 0;		/* initial freq value to use */
int daemonize = 0;
int logging = 1;

const char *prognm = PACKAGE_NAME;
static volatile sig_atomic_t sighup  = 0;
static volatile sig_atomic_t sigterm = 0;

struct ntp_peers peer;
double root_delay;
double root_dispersion;

/* prototypes for some local routines */
static int rfc1305print(uint32_t *data, struct ntptime *arrival, struct ntp_control *ntpc, int *error);

/* OS dependent routine to get the current value of clock frequency */
static int get_current_freq(void)
{
#ifdef __linux__
	struct timex txc;

	txc.modes = 0;
	if (adjtimex(&txc) < 0) {
		ERR(errno, "Failed adjtimex(GET)");
		exit(1);
	}
	return txc.freq;
#else
	return 0;
#endif
}

/* OS dependent routine to set a new value of clock frequency */
static int set_freq(int new_freq)
{
#ifdef __linux__
	struct timex txc;

	txc.modes = ADJ_FREQUENCY;
	txc.freq = new_freq;
	if (adjtimex(&txc) < 0) {
		ERR(errno, "Failed adjtimex(SET)");
		exit(1);
	}
	return txc.freq;
#else
	return 0;
#endif
}

static void set_time(struct ntptime *new)
{
	struct timespec tv_set;

	/* it would be even better to subtract half the slop */
	tv_set.tv_sec = new->coarse - JAN_1970;
	/* divide xmttime.fine by 4294.967296 */
	tv_set.tv_nsec = USEC(new->fine) * 1000;
	if (clock_settime(CLOCK_REALTIME, &tv_set) < 0) {
		ERR(errno, "Failed clock_settime()");
		exit(1);
	}

	DBG("Set time to %lu.%.9lu", tv_set.tv_sec, tv_set.tv_nsec);
}

void ntpc_gettime(struct ntptime *nt)
{
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	nt->coarse = now.tv_sec + JAN_1970;
	nt->fine   = NTPFRAC(now.tv_nsec / 1000);
}

static int send_packet(int usd, struct ntptime *time_sent)
{
	uint32_t data[12];

#define LI 0
#define VN 3
#define MODE 3
#define STRATUM 0
#define POLL 4
#define PREC -6

#ifdef ENABLE_DEBUG
	DBG("Sending packet ...");
#endif
	if (sizeof(data) != 48) {
		ERR(0, "Packet size error");
		return -1;
	}

	memset(data, 0, sizeof data);
	data[0] = htonl((LI << 30) | (VN << 27) | (MODE << 24) | (STRATUM << 16) | (POLL << 8) | (PREC & 0xff));
	data[1] = htonl(1 << 16);	/* Root Delay (seconds) */
	data[2] = htonl(1 << 16);	/* Root Dispersion (seconds) */
	ntpc_gettime(time_sent);

	data[10] = htonl(time_sent->coarse);	/* Transmit Timestamp coarse */
	data[11] = htonl(time_sent->fine);	/* Transmit Timestamp fine   */

	return send(usd, data, 48, 0);
}

void get_packet_timestamp(int usd, struct ntptime *udp_arrival_ntp)
{
#ifdef PRECISION_SIOCGSTAMP
	struct timeval udp_arrival;

	if (ioctl(usd, SIOCGSTAMP, &udp_arrival) < 0) {
		ERR(errno, "Failed ioctl(SIOCGSTAMP)");
		ntpc_gettime(udp_arrival_ntp);
	} else {
		udp_arrival_ntp->coarse = udp_arrival.tv_sec + JAN_1970;
		udp_arrival_ntp->fine = NTPFRAC(udp_arrival.tv_usec);
	}
#else
	(void)usd;		/* not used */
	ntpc_gettime(udp_arrival_ntp);
#endif
}

static int check_source(int data_len, struct sockaddr_storage *ss, struct ntp_control *ntpc)
{
	struct sockaddr_in6 *ipv6;
	struct sockaddr_in *ipv4;
	uint16_t port;

	(void)data_len;
	(void)ntpc;		/* not used */
#ifdef ENABLE_DEBUG
	DBG("packet of length %d received", data_len);
#endif

	if (ss->ss_family == AF_INET) {
		ipv4 = (struct sockaddr_in *)ss;
		port = ntohs(ipv4->sin_port);
	} else if (ss->ss_family == AF_INET6) {
		ipv6 = (struct sockaddr_in6 *)ss;
		port = ntohs(ipv6->sin6_port);
	} else {
		ERR(0, "%s: Unsupported address family %d", __func__, ss->ss_family);
		return 1;
	}

	/*
	 * we could check that the source is the server we expect, but
	 * Denys Vlasenko recommends against it: multihomed hosts get it
	 * wrong too often.
	 */
	if (NTP_PORT != port) {
		if (port != peer_active()->port) {
			INFO("%s: invalid port: %u", __func__, port);
			return 1;
		}
	}

	return 0;
}

static double ntpdiff(struct ntptime *start, struct ntptime *stop)
{
	int a;
	unsigned int b;

	a = stop->coarse - start->coarse;
	if (stop->fine >= start->fine) {
		b = stop->fine - start->fine;
	} else {
		b = start->fine - stop->fine;
		b = ~b;
		a -= 1;
	}

	return a * 1.e6 + b * (1.e6 / 4294967296.0);
}

/* Remember a probe so its reply can be matched and its loss noticed. */
static void probe_sent(struct ntp_control *ntpc, struct ntptime *ts, time_t now)
{
	int i;

	for (i = 0; i < BCOUNT; i++) {
		if (!ntpc->expire[i]) {
			ntpc->sent[i]   = *ts;
			ntpc->expire[i] = now + RESPONSE_TIMEOUT;
			return;
		}
	}
}

/* Count every probe past its deadline as lost.  Returns how many. */
static int probe_expire(struct ntp_control *ntpc, struct ntp_server *srv, time_t now)
{
	int i, num = 0;

	for (i = 0; i < BCOUNT; i++) {
		if (ntpc->expire[i] && ntpc->expire[i] <= now) {
			ntpc->expire[i] = 0;
			peer_timeout(srv);
			num++;
		}
	}

	return num;
}

/* Earliest outstanding deadline, 0 when nothing is in flight. */
static time_t probe_deadline(struct ntp_control *ntpc)
{
	time_t next = 0;
	int i;

	for (i = 0; i < BCOUNT; i++) {
		if (ntpc->expire[i] && (!next || ntpc->expire[i] < next))
			next = ntpc->expire[i];
	}

	return next;
}

/* Retire a probe once its reply is in. */
static int probe_match(struct ntp_control *ntpc, struct ntptime *org)
{
	int i;

	for (i = 0; i < BCOUNT; i++) {
		if (ntpc->expire[i] &&
		    ntpc->sent[i].coarse == org->coarse &&
		    ntpc->sent[i].fine   == org->fine) {
			ntpc->expire[i] = 0;
			return 0;
		}
	}

	return -1;
}

/*
 * RFC 4330 section 5 checks, in the order the original had them.
 * Returns NULL when the reply is usable, otherwise the name of the
 * check it failed, for the log.  Whether the origin timestamp matched
 * is the caller's to work out, since only the caller knows what it
 * sent.
 */
static const char *packet_check(uint32_t *data, int unmatched)
{
	int li, vn, mode;
	int delay, disp;
	uint32_t xmt_coarse, xmt_fine;

#define Data(i) ntohl(((uint32_t *)data)[i])
	li         = Data(0) >> 30 & 0x03;
	vn         = Data(0) >> 27 & 0x07;
	mode       = Data(0) >> 24 & 0x07;
	delay      = Data(1);
	disp       = Data(2);
	xmt_coarse = Data(10);
	xmt_fine   = Data(11);
#undef Data

	if (li == 3)
		return "LI==3";		/* unsynchronized */
	if (vn < 3)
		return "VN<3";		/* RFC-4330 documents SNTP v4, but we interoperate with NTP v3 */
	if (mode != 4)
		return "MODE!=3";
	if (unmatched)
		return "ORG!=sent";
	if (xmt_coarse == 0 && xmt_fine == 0)
		return "XMT==0";
	if (delay > 65536 || delay < -65536)
		return "abs(DELAY)>65536";
	if (disp > 65536 || disp < -65536)
		return "abs(DISP)>65536";

	return NULL;
}

/*
 * A stratum 0 reply is a Kiss-o'-Death, RFC 4330 section 8, and it is
 * for srv: the association and the candidate probe each hand in the
 * server they asked, or a DENY meant for the one we would rather have
 * would retire the one we are actually using.  Returns non-zero when
 * the reply was one, since it never carries a time either way.
 *
 * Handled outside packet_check() because it is not one of the section 5
 * checks: those are recommendations -t lets the operator waive, and
 * this is the server's own request, which is not the operator's to
 * waive.  Still held to the origin timestamp, waived or not, because
 * retiring a server for the rest of the run on an unsolicited packet is
 * a denial of service for anyone who can guess our source port.
 */
static int packet_kod(uint32_t *data, int unmatched, struct ntp_server *srv)
{
	char code[5] = { 0 };
	int mode, stratum;

	mode    = ntohl(data[0]) >> 24 & 0x07;
	stratum = ntohl(data[0]) >> 16 & 0xff;

	/*
	 * Mode as well as stratum, because a stratum 0 packet that is not a
	 * server reply is a server that is broken rather than one refusing
	 * us, and retiring it for the rest of the run would be our mistake
	 * and not its request.
	 *
	 * The leap indicator is the one field that cannot be consulted
	 * here: a conformant kiss o' death sets it to 3, RFC 5905 section
	 * 7.4, which is also why this has to run before packet_check()
	 * rather than after.  After, every real one would be turned away as
	 * LI==3 with nobody the wiser.
	 */
	if (mode != 4 || stratum || unmatched)
		return 0;

	/* Reference identifier, four characters on the wire. */
	memcpy(code, &data[3], sizeof(code) - 1);
	peer_kod(srv, code);

	return 1;
}

/*
 * Everything that decides whether a reply is usable, in the order it has
 * to be decided.  Returns NULL when the reply is good, otherwise what it
 * failed, for the log.
 *
 * Both reply paths come through here, because a reply is worth exactly
 * as much to the candidate probe as it is to the association: a server
 * that is up but unsynchronised, or answering a question nobody asked,
 * would otherwise win a switch it cannot sustain, and every one of its
 * packets be rejected the moment it became the association.  One
 * function rather than two call sites that agree to ask the same
 * questions in the same order, since the order is the whole subtlety:
 * a kiss o' death comes before the section 5 checks and survives -t
 * waiving them.
 */
static const char *packet_verify(uint32_t *data, int unmatched, int cross_check,
				 struct ntp_server *srv)
{
	if (packet_kod(data, unmatched, srv))
		return "KoD";

	if (!cross_check)
		return NULL;

	return packet_check(data, unmatched);
}

/* Does more than print, so this name is bogus.
 * It also makes time adjustments, both sudden (-s)
 * and phase-locking (-l).
 * sets *error to the number of microseconds uncertainty in answer
 * returns 0 normally, 1 if the message fails sanity checks
 */
static int rfc1305print(uint32_t *data, struct ntptime *arrival, struct ntp_control *ntpc, int *error)
{
	static int first = 1;

	/* straight out of RFC-1305 Appendix A */
	int stratum, prec;
	int delay, disp;

#ifdef ENABLE_DEBUG
	int li, vn, mode, poll, refid;
	struct ntptime reftime;
#endif
	struct ntptimes pkt_root_delay, pkt_root_dispersion;
	struct ntptime orgtime, rectime, xmttime;
	double el_time, st_time, skew1, skew2, dtemp;
	int freq, unmatched;
	const char *drop_reason = NULL;

#define Data(i) ntohl(((uint32_t *)data)[i])
	stratum = Data(0) >> 16 & 0xff;
#ifdef ENABLE_DEBUG
	/* Only the log wants these now, packet_check() reads its own. */
	li      = Data(0) >> 30 & 0x03;
	vn      = Data(0) >> 27 & 0x07;
	mode    = Data(0) >> 24 & 0x07;
	poll    = Data(0) >>  8 & 0xff;
#endif
	prec    = Data(0) & 0xff;
	if (prec & 0x80)
		prec |= 0xffffff00;
	delay   = Data(1);
	disp    = Data(2);

#ifdef ENABLE_DEBUG
	refid          = Data(3);
	reftime.coarse = Data(4);
	reftime.fine   = Data(5);
#endif

	orgtime.coarse = Data(6);
	orgtime.fine   = Data(7);
	rectime.coarse = Data(8);
	rectime.fine   = Data(9);
	xmttime.coarse = Data(10);
	xmttime.fine   = Data(11);
#undef Data

#ifdef ENABLE_DEBUG
	DBG("LI=%d  VN=%d  Mode=%d  Stratum=%d  Poll=%d  Precision=%d", li, vn, mode, stratum, poll, prec);
	DBG("Delay=%.1f  Dispersion=%.1f  Refid=%u.%u.%u.%u", sec2u(delay), sec2u(disp),
	      refid >> 24 & 0xff, refid >> 16 & 0xff, refid >> 8 & 0xff, refid & 0xff);
	DBG("Reference %u.%.6u", reftime.coarse, USEC(reftime.fine));
	DBG("Originate %u.%.6u", orgtime.coarse, USEC(orgtime.fine));   /* T1 */
	DBG("Receive   %u.%.6u", rectime.coarse, USEC(rectime.fine));   /* T2 */
	DBG("Transmit  %u.%.6u", xmttime.coarse, USEC(xmttime.fine));   /* T3 */
	DBG("Our recv  %u.%.6u", arrival->coarse, USEC(arrival->fine)); /* T4 */
#endif

	el_time = ntpdiff(&orgtime, arrival);	/* elapsed: (T4 - T1)*/
	st_time = ntpdiff(&rectime, &xmttime);	/* stall: (T3 - T2) */
	skew1 = ntpdiff(&orgtime, &rectime);
	skew2 = ntpdiff(&xmttime, arrival);
	freq = get_current_freq();

#ifdef ENABLE_DEBUG
	DBG("Total elapsed: %9.2f", el_time);
	DBG("Server stall:  %9.2f", st_time);
	DBG("Slop:          %9.2f", el_time - st_time);
	DBG("Skew:          %9.2f", (skew1 - skew2) / 2);
	DBG("Frequency:     %9d", freq);
#endif

	/*
	 * Retire the probe this reply answers.  Done even with the
	 * cross-checks disabled, or an answered probe would sit in the
	 * ring until its deadline and be counted as lost.
	 */
	unmatched = probe_match(ntpc, &orgtime);

	/* error checking, see RFC-4330 sections 5 and 8 */
	drop_reason = packet_verify(data, unmatched, ntpc->cross_check, peer_active());
	if (drop_reason)
		goto fail;

	if (!dry && ntpc->set_clock) {
		/* CAP_SYS_TIME or root required, or sntpd will exit here! */
		set_time(&xmttime);
		LOG("Time synchronized to server %s, stratum %d", peer_active()->host, stratum);
	}

	/* Update last time set ... */
	pkt_root_delay.coarse = ((uint32_t)delay >> 16) & 0xFFFF;
	pkt_root_delay.fine   = ((uint32_t)delay >>  0) & 0xFFFF;
	pkt_root_dispersion.coarse = ((uint32_t)disp >> 16) & 0xFFFF;
	pkt_root_dispersion.fine   = ((uint32_t)disp >>  0) & 0xFFFF;

	peer.last_update_ts = xmttime;
	peer.last_rootdelay = wire2d32(&pkt_root_delay);
	peer.last_rootdisp  = wire2d32(&pkt_root_dispersion);

	/* delay = (T4 - T1) - (T3 - T2) */
	peer.last_delay = (wire2d64(arrival) - wire2d64(&orgtime)) - (wire2d64(&xmttime) - wire2d64(&rectime));
	if (peer.last_delay < G_precision_sec)
                peer.last_delay = G_precision_sec;

	root_delay = peer.last_rootdelay + peer.last_delay;
	dtemp = G_precision_sec + MIN_DISP; /* XXX: Fixme, see BusyBox ntpd.c */
	root_dispersion = peer.last_rootdisp + dtemp;
//	LOG("Calculated root_delay %f, root_dispersion %f", root_delay, root_dispersion);

	/*
	 * Not the ideal order for printing, but we want to be sure
	 * to do all the time-sensitive thinking (and time setting)
	 * before we start the output, especially fflush() (which
	 * could be slow).  Of course, if debug is turned on, speed
	 * has gone down the drain anyway.
	 */
	if (ntpc->live) {
		int new_freq;

		new_freq = contemplate_data(arrival->coarse,
					    (skew1 - skew2) / 2,
					    el_time + sec2u(disp),
					    freq);

		if (!dry && new_freq != freq)
			set_freq(new_freq);
	}

	/*
	 * Display by default for ntpclient users, sntpd must run with -l info:
	 * Day   Second      Elapsed    Stall      Skew  Dispersion  Freq
	 * 43896 75886.786    6653.0      2.4     798.9  14663.7     92907
	 */
	if (first) {
		INFO("Day   Second      Elapsed    Stall      Skew  Dispersion   Freq");
		first = 0;
	}
	INFO("%d %.5d.%.3d  %8.1f %8.1f  %8.1f %8.1f %9d",
	     arrival->coarse / 86400, arrival->coarse % 86400,
	     arrival->fine / 4294967, el_time, st_time,
	     (skew1 - skew2) / 2, sec2u(disp), freq);

	*error = el_time - st_time;

	return 0;
 fail:
	ERR(0, "%d %.5d.%.3d rejected packet: %s",
	    arrival->coarse / 86400, arrival->coarse % 86400,
	    arrival->fine / 4294967, drop_reason);

	/*
	 * Our probe was answered, the answer was just no good, and its
	 * slot is already retired so nothing else will notice.  Charge
	 * it as a miss, or a server that is up but unsynchronized is
	 * one sntpd would sit on forever.  A reply that failed the ORG
	 * test answers no probe of ours, so a replayed or spoofed
	 * packet cannot drive rotation.
	 */
	if (!unmatched)
		peer_timeout(peer_active());

	return 1;
}

int setup_receive(int usd, sa_family_t sin_family, uint16_t port)
{
	struct sockaddr_in6 sin6;
	struct sockaddr_in sin;
	struct sockaddr *sa;
	socklen_t len;

	if (sin_family == AF_INET) {
		/* IPV4 */
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
		sin.sin_port = htons(port);

		sa = (struct sockaddr *)&sin;
		len = sizeof(sin);
	} else {
		/* IPV6 */
		memset(&sin6, 0, sizeof(struct sockaddr_in6));
		sin6.sin6_family = AF_INET6;
		sin6.sin6_port = htons(port);
		sin6.sin6_addr = in6addr_any;

		sa = (struct sockaddr *)&sin6;
		len = sizeof(sin6);
	}

	if (bind(usd, sa, len) == -1) {
		ERR(errno, "Failed binding to UDP port %u", port);
		return -1;
	}

	return 0;
}

static int setup_transmit(int usd, struct sockaddr_storage *ss, uint16_t port,
			  struct ntp_control *ntpc)
{
	struct sockaddr_in6 *ipv6;
	struct sockaddr_in *ipv4;
	socklen_t len = 0;

	/* Prefer IPv4 over IPv6, for now */
	if (ss->ss_family == AF_INET) {
		ipv4 = (struct sockaddr_in *)ss;
		ipv4->sin_port = htons(port);
		len = sizeof(struct sockaddr_in);
	} else if (ss->ss_family == AF_INET6) {
		ipv6 = (struct sockaddr_in6 *)ss;
		ipv6->sin6_port = htons(port);
		len = sizeof(struct sockaddr_in6);
	} else {
		ERR(0, "%s: Unsupported address family %d", __func__, ss->ss_family);
		return -1;
	}

	if (connect(usd, (struct sockaddr *)ss, len) == -1) {
		ERR(errno, "Failed connecting to NTP server");
		return -1;
	}

	return 0;
}

/*
 * Resolve host and hand back the idx-th usable address, wrapping if
 * idx runs past the end.  Returns how many usable addresses there
 * were, or -1 on failure, so the caller can tell how many candidates
 * this name actually represents.
 *
 * The order is not stable between calls: RFC 6724 sorting depends on
 * the local addresses of the moment, and a round-robin name server
 * shuffles the answer anyway.  So idx is a slot, not an identity, and
 * a lap visits as many addresses as the name has rather than each
 * address exactly once.  Good enough to find a working one, which is
 * all a lap is for.
 *
 * quiet keeps the candidate probe out of the network-state reporting.
 * It asks once per poll interval, so a name that stays broken would be
 * a notice per interval forever, and its answers must not decide when
 * the association says the network came back.
 */
static int getaddrbyname(char *host, struct sockaddr_storage *ss, int idx, int quiet)
{
	struct addrinfo *result;
	static int netdown = 0;
	struct addrinfo hints;
	struct addrinfo *rp;
	int err, num, want;

	if (!host || !ss) {
		errno = EINVAL;
		return -1;
	}

	res_init();

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	/*
	 * Without a socket type getaddrinfo() returns every address once
	 * per type, which would count one address as three candidates.
	 * NTP is UDP, so ask for what we are actually going to open.
	 */
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	memset(ss, 0, sizeof(struct sockaddr_storage));
	err = getaddrinfo(host, NULL, &hints, &result);
	if (err) {
		if (quiet) {
			errno = ENETDOWN;
			return -1;
		}

		switch (err) {
		case EAI_NONAME:
			LOG("Failed resolving %s, will try again later ...", host);
			break;
		case EAI_AGAIN:
			LOG("Temporary failure resolving %s, will try again later ...", host);
			break;
		default:
			LOG("Error %d resolving %s: %s", err, host, gai_strerror(err));
			break;
		}
		netdown = errno = ENETDOWN;
		return -1;
	}

	num = 0;
	for (rp = result; rp; rp = rp->ai_next) {
		if (rp->ai_family == AF_INET || rp->ai_family == AF_INET6)
			num++;
	}

	if (num == 0) {
		freeaddrinfo(result);
		errno = EAGAIN;
		return -1;
	}

	want = idx % num;
	for (rp = result; rp; rp = rp->ai_next) {
		if (rp->ai_family != AF_INET && rp->ai_family != AF_INET6)
			continue;
		if (want-- == 0) {
			memcpy(ss, rp->ai_addr, rp->ai_addrlen);
			break;
		}
	}

	freeaddrinfo(result);

	if (netdown && !quiet) {
		LOG("Network up, resolved address to hostname %s", host);
		netdown = 0;
	}

	return num;
}

/* Numeric form of ss, for the log.  Never fails, worst case "?". */
static char *addr2str(struct sockaddr_storage *ss, char *buf, size_t len)
{
	const void *src;

	if (ss->ss_family == AF_INET)
		src = &((struct sockaddr_in *)ss)->sin_addr;
	else
		src = &((struct sockaddr_in6 *)ss)->sin6_addr;

	if (!inet_ntop(ss->ss_family, src, buf, len))
		snprintf(buf, len, "?");

	return buf;
}

static int setup_socket(struct ntp_control *ntpc)
{
	struct ntp_server *srv = peer_active();
	struct sockaddr_storage ss;
	char addr[INET6_ADDRSTRLEN];
	int num, sd;

	if (!srv || !srv->host) {
		ERR(0, "No NTP server to connect to");
		exit(1);
	}

	num = getaddrbyname(srv->host, &ss, srv->addr_idx, 0);
	if (num < 0) {
		if (EINVAL == errno) {
			ERR(0, "Unable to look up %s address", srv->host);
			exit(1);
		}

		/*
		 * A name that will not resolve is a candidate that will
		 * not answer, so charge it a miss the way a lost probe
		 * is charged one.  It then runs out of chances and
		 * rotation moves on, instead of one bad name blocking
		 * the whole list.
		 */
		peer_timeout(srv);
		errno = ENETDOWN;
		return -1;
	}

	peer_naddr(srv, num);

	/* open socket based on the server address family */
	if (ss.ss_family != AF_INET && ss.ss_family != AF_INET6) {
		ERR(0, "%s: Unsupported address family %d", __func__, ss.ss_family);
		exit(1);
	}

	/*
	 * Remember it before setup_transmit() stamps the port into it,
	 * so srv->addr is the address as resolved rather than one with
	 * our own idea of the port in it.
	 */
	srv->addr = ss;

	/*
	 * Rotating through a name's addresses is otherwise invisible:
	 * every lap logs the same host:port.  Say which address it is.
	 */
	DBG("Resolved %s to %s, address %d of %d", srv->host,
	    addr2str(&ss, addr, sizeof(addr)), srv->addr_idx + 1, num);

	sd = socket(ss.ss_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sd == -1)
		return -1;

	if (setup_receive(sd, ss.ss_family, ntpc->local_udp_port) ||
	    setup_transmit(sd, &ss, srv->port, ntpc)) {
		close(sd);
		/* An address we cannot even reach is a candidate that will
		 * not answer, same as a name we cannot resolve. */
		peer_timeout(srv);
		errno = ENETDOWN;
		return -1;
	}

	/* Only the association, which setup_transmit() no longer knows it
	 * is being used for; the candidate probe below is not one. */
	INFO("Connected to NTP server.");

	/*
	 * Every day: reopen socket and perform a new DNS lookup.
	 */
	alarm(60 * 60 * 24);

	return sd;
}

/*
 * Probe a server we are not synced to.  It gets its own short-lived
 * socket so the active association keeps the connected one, and with
 * it ICMP error reporting.
 *
 * The probe goes to the address the candidate's own cursor points at,
 * which is the one setup_socket() would use if the probe wins, and is
 * address zero for any server rotation has stepped away from.  Nothing
 * here touches that cursor or the resolved address count: those belong
 * to the active association, and rotation is the only thing that may
 * move them.
 */
static int probe_candidate(struct ntp_server *srv, struct ntptime *sent,
			   struct ntp_control *ntpc)
{
	struct sockaddr_storage ss;
	char addr[INET6_ADDRSTRLEN];
	int sd;

	if (getaddrbyname(srv->host, &ss, srv->addr_idx, 1) < 0) {
		DBG("Cannot resolve preferred server %s", srv->host);
		return -1;
	}

	sd = socket(ss.ss_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sd == -1)
		return -1;

	/*
	 * The origin timestamp goes back to the caller: only one probe is
	 * ever outstanding, so that one timestamp is the whole ring the
	 * association needs BCOUNT slots for, and it is what lets the
	 * reply be held to the ORG check.
	 */
	if (setup_transmit(sd, &ss, srv->port, ntpc) || send_packet(sd, sent) == -1) {
		close(sd);
		return -1;
	}

	DBG("Probing preferred server %s:%u at %s", srv->host, srv->port,
	    addr2str(&ss, addr, sizeof(addr)));

	return sd;
}

/*
 * Signal handler.  Take note of the fact that the signal arrived
 * so that the main loop can take care of it.
 */
static void handler(int sig)
{
	switch (sig) {
	case SIGHUP:
	case SIGALRM:
		/* Trigger NTP sync */
		sighup = 1;
		break;

	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
	case SIGUSR1:
	case SIGUSR2:
		sigterm = 1;
		break;
	}
}

static void setup_signals(void)
{
	struct sigaction sa;

	sa.sa_handler = handler;
	sa.sa_flags = 0;	/* Interrupt system calls */
	sigemptyset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);
}

/*
 * Returns non-zero when it gave up rather than was asked to stop, so a
 * supervisor can tell "nothing left to sync to" from a clean exit and
 * back off and retry instead of considering the job done.
 */
static int loop(struct ntp_control *ntpc)
{
	fd_set fds;
	struct sockaddr_storage sa_xmit;
	int i, pack_len, probes_sent, error;
	socklen_t sa_xmit_len;
	struct timeval to;
	struct ntptime udp_arrival_ntp;
	static uint32_t incoming_word[325];
	struct ntp_server *srv;
	struct ntptime ts;
	time_t now, t_send = 0, t_last = 0, deadline;
	struct ntp_server *cand = NULL;	/* what an outstanding probe asks   */
	struct ntptime rsent;		/* and the timestamp it asked with  */
	time_t t_retry = 0;		/* when to probe the candidate      */
	time_t t_candidate = 0;		/* when that probe is given up on   */
	int nfds;
	int rc = 0;
	int retired;
	int usd = -1;
	int sd = -1;
	int rsd = -1;			/* transient candidate probe socket */

#define incoming ((char *) incoming_word)
#define sizeof_incoming (sizeof incoming_word)

	if (ntpc->server_port)
		sd = server_init(ntpc->server_port);

#ifdef ENABLE_DEBUG
	DBG("Listening...");
#endif
	probes_sent = 0;
	sa_xmit_len = sizeof(sa_xmit);

	while (1) {
		if (sigterm) {
			ntpc->live = 0;
			break;
		}

		now = time(NULL);
		srv = peer_active();

		/*
		 * A probe past its deadline is a probe lost.  Losing one
		 * tightens the schedule, so recompute from the last send.
		 */
		if (probe_expire(ntpc, srv, now))
			t_send = t_last + peer_spacing(srv, ntpc->cycle_time);

		/*
		 * Out of chances, or told to go away for good.  This has to
		 * come before the socket block: a name that will not resolve
		 * fails in there and loops straight back round, so a check
		 * placed after it would never run.  Dropping the socket here
		 * lets the block below build one for the new candidate in
		 * this same pass.
		 */
		retired = peer_retired(srv);
		if (retired || peer_unreachable(srv)) {
			LOG("Server %s:%u %s, rotating", srv->host, srv->port,
			    retired ? "retired" : "unreachable");
			if (peer_rotate() == -1) {
				ERR(0, "No usable NTP server left");
				rc = 1;
				goto done;
			}

			srv = peer_activate();
			LOG("Trying NTP server %s:%u", srv->host, srv->port);

			contemplate_reset();
			if (usd != -1)
				close(usd);
			usd = -1;

			/*
			 * An outstanding probe asks whether to come back to
			 * a server.  If the ring just landed on that server
			 * the question is answered, and counting the reply
			 * would bank a hit against the association itself.
			 */
			if (rsd != -1 && cand == srv) {
				close(rsd);
				rsd = -1;
				cand = NULL;
			}
		}

		/*
		 * Give up on a candidate probe past its deadline, and on the
		 * run of replies it was part of.  Above the socket block:
		 * that block can loop straight back round for a whole
		 * outage, and an expired probe must not be held open, and
		 * its run left standing, for the length of one.
		 */
		if (rsd > -1 && now >= t_candidate) {
			DBG("Preferred server %s:%u did not answer", cand->host, cand->port);
			peer_candidate_timeout();
			close(rsd);
			rsd = -1;
			cand = NULL;
		}

		if (sighup || usd == -1) {
			int init;

			sighup = 0;

			/*
			 * Probes sent on the old socket can no longer be
			 * answered, so drop them instead of blaming the
			 * address we are about to start using.  Then probe
			 * at once, SIGHUP means "resync now".
			 */
			memset(ntpc->expire, 0, sizeof(ntpc->expire));
			t_send = 0;

			if (usd == -1)
				init = 1;
			else {
				init = 0;	/* SIGHUP with a live socket */
				close(usd);
			}

			usd = setup_socket(ntpc);
			if (usd == -1) {
				/* Wait here a while, networking is probably not up yet. */
				if (errno == ENETDOWN) {
					sleep(1);
					continue;
				}
				ERR(errno, init ? "Failed creating UDP socket()"
				    : "Failed reopening NTP socket");
				goto done;
			}

			if (!init)
				DBG("Got SIGHUP, triggering resync with NTP server.");
			init = 0;

			/* Resolving can take a while, so do not schedule off a stale now. */
			now = time(NULL);
		}

		if (now >= t_send) {
			if (probes_sent >= ntpc->probe_count && ntpc->probe_count != 0)
				break;

			if (send_packet(usd, &ts) == -1) {
				ERR(errno, "Failed sending probe");
				peer_timeout(srv);	/* a probe that never left is lost */
			} else {
				probe_sent(ntpc, &ts, now);
				peer_probed(srv);
				probes_sent++;
			}

			t_last = now;
			t_send = now + peer_spacing(srv, ntpc->cycle_time);
		}

		/*
		 * Ask the server we would rather be using whether it is
		 * back, once per poll cycle.  This is where all of it
		 * starts: only an explicit prefer names such a server, so on
		 * a plain list peer_candidate() returns NULL, rsd stays -1,
		 * and every other piece of the machinery is gated on that.
		 * After the active probe, because resolving the candidate
		 * can block and the association comes first.
		 */
		if (rsd == -1 && now >= t_retry) {
			cand = peer_candidate();
			if (cand) {
				rsd = probe_candidate(cand, &rsent, ntpc);
				now = time(NULL); /* resolving can take a while */
				if (rsd == -1) {
					peer_candidate_timeout();
					cand = NULL;
				} else
					t_candidate = now + RESPONSE_TIMEOUT;
			}

			/* Off the refreshed clock: a resolve that blocked
			 * for longer than the cycle would otherwise retry on
			 * every iteration instead of once per cycle. */
			t_retry = now + ntpc->cycle_time;
		}

		deadline = probe_deadline(ntpc);
		if (!deadline || t_send < deadline)
			deadline = t_send;

		/*
		 * The candidate probe is the third clock, and it joins the
		 * other two here.  Without it the socket would sit open, and
		 * the run of replies stay uncharged, until the next poll --
		 * ten minutes later by default.  Its deadline was still in
		 * the future at the top of the pass, so the wait only comes
		 * out as zero if the socket rebuild in between blocked past
		 * it, and then select() returns at once and the next pass
		 * gives the probe up.
		 */
		if (rsd > -1 && t_candidate < deadline)
			deadline = t_candidate;

		to.tv_sec  = deadline > now ? deadline - now : 0;
		to.tv_usec = 0;

		FD_ZERO(&fds);
		FD_SET(usd, &fds);
		if (sd > -1)
			FD_SET(sd, &fds);
		if (rsd > -1)
			FD_SET(rsd, &fds);

		nfds = usd > sd ? usd : sd;
		if (rsd > nfds)
			nfds = rsd;
		i = select(nfds + 1, &fds, NULL, NULL, &to);	/* Wait on read or error */
		if (i <= 0) {
			if (i < 0 && errno != EINTR)
				ERR(errno, "Failed select()");
			continue;
		}

		if (sd > -1 && FD_ISSET(sd, &fds)) {
			server_recv(sd);
			continue;
		}

		/*
		 * Serviced before the association, and held to the same
		 * standard: the question is not whether something answered
		 * but whether this answer would survive becoming the
		 * association.  A server that is up and unsynchronised
		 * answers cheerfully, and a GPS receiver without a fix is
		 * exactly that, so accepting it on length alone would switch
		 * to it, reject every packet it then sent, rotate away, and
		 * come straight back.
		 *
		 * A reply that fails simply does not count, and ends the run
		 * of replies the way a timeout does.  No miss is charged:
		 * misses drive rotation, and rotation is about the server we
		 * are synced to, not the one we are asking after.
		 */
		if (rsd > -1 && FD_ISSET(rsd, &fds)) {
			const char *why;

			pack_len = recv(rsd, incoming, sizeof_incoming, 0);
			if (pack_len < 48 || (unsigned)pack_len >= sizeof_incoming)
				why = "not an NTP reply";
			else
				why = packet_verify(incoming_word,
						    ntohl(incoming_word[6]) != rsent.coarse ||
						    ntohl(incoming_word[7]) != rsent.fine,
						    ntpc->cross_check, cand);

			if (why) {
				DBG("Preferred server %s:%u unusable: %s",
				    cand->host, cand->port, why);
				peer_candidate_timeout();
			} else {
				DBG("Preferred server %s:%u answered",
				    cand->host, cand->port);
				peer_candidate_rx();
			}

			close(rsd);
			rsd = -1;
			cand = NULL;

			if (peer_switchback()) {
				/* peer_activate() for the same reason
				 * rotation calls it: unactivated, the server
				 * arrives still carrying the misses it was
				 * dropped for. */
				srv = peer_activate();
				LOG("Switching back to preferred server %s:%u",
				    srv->host, srv->port);

				contemplate_reset();
				close(usd);
				usd = -1;
			}
			continue;
		}

		error = ntpc->goodness;
		pack_len = recvfrom(usd, incoming, sizeof_incoming, 0, (struct sockaddr *)&sa_xmit, &sa_xmit_len);
		if (pack_len < 0) {
			ERR(errno, "Failed recvfrom()");
		} else if (pack_len > 0 && (unsigned)pack_len < sizeof_incoming) {
			get_packet_timestamp(usd, &udp_arrival_ntp);
			if (check_source(pack_len, &sa_xmit, ntpc))
				continue;
			if (rfc1305print(incoming_word, &udp_arrival_ntp, ntpc, &error) != 0) {
				/*
				 * A rejected answer is charged as a miss, so
				 * the schedule has to tighten the same way it
				 * does for a lost probe -- its slot is gone
				 * from the ring and will not expire.  While
				 * nothing is lost peer_spacing() still returns
				 * the cycle time, so a spoofed packet, which
				 * is charged to nobody, cannot speed us up.
				 */
				t_send = t_last + peer_spacing(srv, ntpc->cycle_time);
				continue;
			}
			peer_rx(srv);
		} else {
			ERR(0, "Ooops.  pack_len=%d", pack_len);
		}

		/*
		 * best rollover option: specify -g, -s, and -l.
		 * simpler rollover option: specify -s and -l, which
		 * triggers a magic -c 1
		 */
		if ((error < ntpc->goodness && ntpc->goodness != 0) ||
		    (probes_sent >= ntpc->probe_count && ntpc->probe_count != 0)) {
			ntpc->set_clock = 0;
			if (!ntpc->live)
				break;
		}
	}
#undef incoming
#undef sizeof_incoming
done:
	if (usd != -1)
		close(usd);
	if (sd != -1)
		close(sd);
	if (rsd != -1)
		close(rsd);

	return rc;
}

#ifdef ENABLE_REPLAY
static int do_replay(void)
{
	char line[100];
	int n, day, freq, absolute;
	float sec, el_time, st_time, disp;
	double skew, errorbar;
	int simulated_freq = 0;
	unsigned int last_fake_time = 0;
	double fake_delta_time = 0.0;

	while (fgets(line, sizeof line, stdin)) {
		n = sscanf(line, "%d %f %f %f %lf %f %d", &day, &sec, &el_time, &st_time, &skew, &disp, &freq);
		if (n == 7) {
			DBG("%s", line);

			absolute = day * 86400 + (int)sec;
			errorbar = el_time + disp;
#ifdef ENABLE_DEBUG
			DBG("Contemplate %u %.1f %.1f %d", absolute, skew, errorbar, freq);
#endif
			if (last_fake_time == 0)
				simulated_freq = freq;
			fake_delta_time += (absolute - last_fake_time) * ((double)(freq - simulated_freq)) / 65536;
#ifdef ENABLE_DEBUG
			DBG("Fake %f %d", fake_delta_time, simulated_freq);
#endif
			skew += fake_delta_time;
			freq = simulated_freq;
			last_fake_time = absolute;
			simulated_freq = contemplate_data(absolute, skew, errorbar, freq);
		} else {
			ERR(0, "Replay input error");
			return 2;
		}
	}

	return 0;
}
#endif

static int run(struct ntp_control *ntpc, int log_level)
{
	int rc;

	if (daemonize) {
		/*
		 * Force output to syslog, we have no other way of
		 * communicating with the user after being daemonized
		 */
		logging = 1;
	}
	log_init(logging, log_level);

	if (initial_freq) {
		DBG("Initial frequency %d", initial_freq);
		set_freq(initial_freq);
	}

	if (ntpc->set_clock && !ntpc->live && !ntpc->goodness && !ntpc->probe_count)
		ntpc->probe_count = 1;

	/* If user gives a probe count, then assume non-live run */
	if (ntpc->probe_count > 0)
		ntpc->live = 0;

	/* respect only applicable MUST of RFC-4330 */
	if (ntpc->probe_count != 1 && ntpc->cycle_time < min_interval)
		ntpc->cycle_time = min_interval;

#ifdef ENABLE_DEBUG
	DBG("Configuration:");
	DBG("  probe_count %d", ntpc->probe_count);
	DBG("  Dry run     %d", dry);
	DBG("  goodness    %d", ntpc->goodness);
	DBG("  hostname    %s", peer_active()->host);
	DBG("  interval    %d", ntpc->cycle_time);
	DBG("  live        %d", ntpc->live);
	DBG("  local_port  %d", ntpc->local_udp_port);
	DBG("  min_delay   %f", min_delay);
	DBG("  set_clock   %d", ntpc->set_clock);
	DBG("  cross_check %d", ntpc->cross_check);
#endif

	/* Startup sequence */
	if (daemonize) {
		if (-1 == daemon(0, 0)) {
			ERR(errno, "Failed daemonizing, aborting");
			exit(1);
		}
	}

	if (!ntpc->usermode)
		LOG("Starting " PACKAGE_NAME " v" PACKAGE_VERSION);
	setup_signals();

	INFO("Using time sync server: %s", peer_active()->host);

	rc = loop(ntpc);

	if (!ntpc->usermode)
		LOG("Stopping " PACKAGE_NAME " v" PACKAGE_VERSION);

	log_exit();

	return rc;
}

static int ntpclient_usage(int code)
{
	FILE *fp = stdout;

	if (code)
		fp = stderr;

	fprintf(fp,
		"Usage:\n"
		"  ntpclient [-c count] [-d] [-f frequency] [-g goodness] -h hostname\n"
		"            [-i interval] [-l] [-p port] [-q min_delay] [-r] [-s] [-t]\n"
		"\n"
		"Options:\n"
		"  -c count      stop after count time measurements (default 0 means go forever)\n"
		"  -d            Dry run, connect to server, do calculations, no time correction\n"
		"  -f frequency  Initialize frequency offset.  Linux only, requires CAP_SYS_TIME\n"
		"  -g goodness   causes ntpclient to stop after getting a result more accurate\n"
		"                than goodness (microseconds, default 0 means go forever)\n"
		"  -h hostname   (mandatory) NTP server, against which to measure system time\n"
		"  -i interval   check time every interval seconds (default 600)\n"
		"  -l            attempt to lock local clock to server using adjtimex(2)\n"
		"  -p port       local NTP client UDP port (default 0 means \"any available\")\n"
		"  -q min_delay  minimum packet delay for transaction (default 800 microseconds)\n"
#ifdef ENABLE_REPLAY
		"  -r            replay analysis code based on stdin\n"
#endif
		"  -s            simple clock set (implies -c 1)\n"
		"  -t            trust network and server, no RFC-4330 recommended cross-checks\n"
		"\n"
#ifdef PACKAGE_BUGREPORT
		"Bug report address: " PACKAGE_BUGREPORT "\n"
#endif
		"Project homepage: " PACKAGE_URL "\n");

	return code;
}

/* Backwards compat. mode */
static int ntpclient(int argc, char *argv[])
{
	struct ntp_control ntpc;
	int c;

	memset(&ntpc, 0, sizeof(ntpc));
	ntpc.probe_count = 0;	/* default of 0 means loop forever */
	ntpc.cycle_time  = 600;	/* seconds */
	ntpc.goodness    = 0;
	ntpc.set_clock   = 0;
	ntpc.usermode    = 1;
	ntpc.live        = 0;
	ntpc.cross_check = 1;
	daemonize        = 0;
	logging          = 0;

	while (1) {
		char opts[] = "c:df:g:h:i:lp:q:" REPLAY_OPTION "st?";

		c = getopt(argc, argv, opts);
		if (c == EOF)
			break;

		switch (c) {
		case 'c':
			ntpc.probe_count = atoi(optarg);
			break;

		case 'd':
			dry++;
			break;

		case 'f':
			initial_freq = atoi(optarg);
			break;

		case 'g':
			ntpc.goodness = atoi(optarg);
			break;

		case 'h':
			peer_add(optarg);
			break;

		case 'i':
			ntpc.cycle_time = atoi(optarg);
			break;

		case 'l':
			ntpc.live++;
			break;

		case 'p':
			ntpc.local_udp_port = atoi(optarg);
			break;

		case 'q':
			min_delay = atof(optarg);
			break;

#ifdef ENABLE_REPLAY
		case 'r':
			return do_replay();
#endif

		case 's':
			ntpc.set_clock++;
			break;

		case 't':
			ntpc.cross_check = 0;
			break;

		case '?':
			return ntpclient_usage(0);

		default:
			return ntpclient_usage(1);
		}
	}

	if (peer_count() == 0)
		return ntpclient_usage(1);

	return run(&ntpc, dry ? LOG_DEBUG : LOG_INFO);
}

static int usage(int code)
{
	FILE *fp = stdout;

	if (code)
		fp = stderr;

	fprintf(fp,
		"Usage:\n"
		"  %s [-dhn" REPLAY_OPTION "stV] [-i SEC] [-l LEVEL] [-p PORT] [-q USEC] [SERVER]\n"
		"\n"
		"Options:\n"
		"  -d       Dry run, no time correction, useful for debugging\n"
		"  -h       Show summary of command line options and exit\n"
		"  -i SEC   Check time every interval seconds.  Default: 600\n"
		"  -l LEVEL Set log level: none, err, warn, notice (default), info, debug\n"
		"  -n       Don't fork.  Prevents %s from daemonizing by default\n"
		"           Use with '-s' to use syslog as well, for Finit + systemd\n"
		"  -p PORT  SNTP server mode port, default: 123, use 0 to disable\n"
		"  -q USEC  Minimum packet delay for transaction, default: 800 usec\n"
#ifdef ENABLE_REPLAY
		"  -r       Replay analysis code based on stdin\n"
#endif
		"  -s       Use syslog instead of stdout, default unless -n\n"
		"  -t       Trust network and server, disable RFC4330 validation\n"
		"  -v       Show program version\n"
		"\n"
		"Arguments:\n"
		"  SERVER   Optional NTP server to sync with, default: pool.ntp.org\n"
		"\n"
#ifdef PACKAGE_BUGREPORT
		"Bug report address: " PACKAGE_BUGREPORT "\n"
#endif
		"Project homepage: " PACKAGE_URL "\n", prognm, prognm);

	return code;
}

static const char *progname(const char *arg0)
{
	const char *nm;

	nm = strrchr(arg0, '/');
	if (nm)
		nm++;
	else
		nm = arg0;

	return nm;
}

int main(int argc, char *argv[])
{
	struct ntp_control ntpc;
	int log_level = LOG_NOTICE;

	/* sntpd is a multicall binary, how are we called? */
	prognm = progname(argv[0]);

	/* Compat ntpclient */
	if (!strcmp(prognm, "ntpclient"))
		return ntpclient(argc, argv);

	/* Default sntpd */
	memset(&ntpc, 0, sizeof(ntpc));
	ntpc.probe_count = 0;	/* default of 0 means loop forever */
	ntpc.cycle_time  = 600;	/* seconds */
	ntpc.goodness    = 0;
	ntpc.set_clock   = 0;
	ntpc.usermode    = 0;
	ntpc.live        = 1;
	ntpc.cross_check = 1;
	ntpc.server_port = NTP_PORT; /* Server mode enabled by default */

	/* Default to daemon mode for sntpd */
	daemonize        = 1;

	while (1) {
		char opts[] = "dhi:l:m:np:q:" REPLAY_OPTION "stv?";
		int c;

		c = getopt(argc, argv, opts);
		if (c == EOF)
			break;

		switch (c) {
		case 'd':
			dry = 1;
			break;

		case 'h':
			return usage(0);

		case 'i':
			ntpc.cycle_time = atoi(optarg);
			break;

		case 'l':
			log_level = log_str2lvl(optarg);
			if (log_level == -1)
				return usage(1);
			break;

		case 'm':
			min_interval = atoi(optarg);
			if (min_interval < 1)
				min_interval = 1;
			if (min_interval < MIN_INTERVAL)
				logit(LOG_WARNING, 0, "Minimum poll interval %d sec is below the"
				      " %d sec floor RFC 4330 section 10 requires.  Only do this"
				      " on a network with its own time source.",
				      min_interval, MIN_INTERVAL);
			break;

		case 'n':
			daemonize = 0;
			logging--;
			break;

		case 'p':
			ntpc.server_port = atoi(optarg);
			break;

		case 'q':
			min_delay = atof(optarg);
			break;

#ifdef ENABLE_REPLAY
		case 'r':
			return do_replay();
#endif

		case 's':
			logging++;
			break;

		case 't':
			ntpc.cross_check = 0;
			break;

		case 'v':
			puts("v" PACKAGE_VERSION);
			return 0;

		case '?':
		default:
			return usage(0);
		}
	}

	for (; optind < argc; optind++) {
		if (peer_add(argv[optind]))
			return 1;
	}

	if (peer_count() == 0 && peer_add("pool.ntp.org"))
		return 1;

	return run(&ntpc, log_level);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
