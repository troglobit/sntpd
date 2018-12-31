/* NTP client
 *
 * Copyright (C) 1997-2015  Larry Doolittle <larry@doolittle.boa.org>
 * Copyright (C) 2010-2018  Joachim Nilsson <troglobit@gmail.com>
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

#include <config.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>		/* getaddrinfo -> gethostbyname */
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#ifdef PRECISION_SIOCGSTAMP
#include <sys/ioctl.h>
#endif

#include "sntpd.h"

/* XXX fixme - non-automatic build configuration */
#ifdef __linux__
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/timex.h>
#else
extern struct hostent *gethostbyname(const char *name);
extern int h_errno;
char __hstrerror_buf[10];

#define hstrerror(errnum) \
	snprintf(__hstrerror_buf, sizeof(__hstrerror_buf), "Error %d", errnum)
#endif

/* Default to the RFC-4330 specified value */
#ifndef MIN_INTERVAL
#define MIN_INTERVAL 15
#endif

#define JAN_1970        0x83aa7e80	/* 2208988800 1970 - 1900 in seconds */
#define NTP_PORT (123)

/* How to multiply by 4294.967296 quickly (and not quite exactly)
 * without using floating point or greater than 32-bit integers.
 * If you want to fix the last 12 microseconds of error, add in
 * (2911*(x))>>28)
 */
#define NTPFRAC(x) ( 4294*(x) + ( (1981*(x))>>11 ) )

/* The reverse of the above, needed if we want to set our microsecond
 * clock (via clock_settime) based on the incoming time in NTP format.
 * Basically exact.
 */
#define USEC(x) ( ( (x) >> 12 ) - 759 * ( ( ( (x) >> 10 ) + 32768 ) >> 16 ) )

/* Converts NTP delay and dispersion, apparently in seconds scaled
 * by 65536, to microseconds.  RFC-1305 states this time is in seconds,
 * doesn't mention the scaling.
 * Should somehow be the same as 1000000 * x / 65536
 */
#define sec2u(x) ( (x) * 15.2587890625 )

struct ntptime {
	unsigned int coarse;
	unsigned int fine;
};

struct ntp_control {
	uint32_t time_of_send[2];
	int usermode;		/* 0: sntpd, 1: ntpclient */
	int live;
	int set_clock;		/* non-zero presumably needs CAP_SYS_TIME or root */
	int probe_count;
	int cycle_time;
	int goodness;
	int cross_check;

	uint16_t local_udp_port;
	char *server;		/* must be set */
	char serv_addr[4];
};

int debug = 0;
int verbose = 0;		/* Verbose flag, produce useful output to log */
int initial_freq = 0;		/* initial freq value to use */
int daemonize = 0;

const char *prognm = PACKAGE_NAME;
static int sighup = 0;
static int sigterm = 0;

extern char *optarg;		/* according to man 2 getopt */

/* prototypes for some local routines */
static void send_packet(int usd, uint32_t time_sent[2]);
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

	if (debug)
		DBG("Set time to %lu.%.9lu", tv_set.tv_sec, tv_set.tv_nsec);
}

static void ntpc_gettime(uint32_t *time_coarse, uint32_t *time_fine)
{
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	*time_coarse = now.tv_sec + JAN_1970;
	*time_fine = NTPFRAC(now.tv_nsec / 1000);
}

static void send_packet(int usd, uint32_t time_sent[2])
{
	uint32_t data[12];

#define LI 0
#define VN 3
#define MODE 3
#define STRATUM 0
#define POLL 4
#define PREC -6

#ifdef ENABLE_DEBUG
	if (debug)
		DBG("Sending packet ...");
#endif
	if (sizeof(data) != 48) {
		ERR(0, "Packet size error");
		return;
	}

	memset(data, 0, sizeof data);
	data[0] = htonl((LI << 30) | (VN << 27) | (MODE << 24) | (STRATUM << 16) | (POLL << 8) | (PREC & 0xff));
	data[1] = htonl(1 << 16);	/* Root Delay (seconds) */
	data[2] = htonl(1 << 16);	/* Root Dispersion (seconds) */
	ntpc_gettime(time_sent, time_sent + 1);

	data[10] = htonl(time_sent[0]);	/* Transmit Timestamp coarse */
	data[11] = htonl(time_sent[1]);	/* Transmit Timestamp fine   */
	send(usd, data, 48, 0);
}

static void get_packet_timestamp(int usd, struct ntptime *udp_arrival_ntp)
{
#ifdef PRECISION_SIOCGSTAMP
	struct timeval udp_arrival;

	if (ioctl(usd, SIOCGSTAMP, &udp_arrival) < 0) {
		ERR(errno, "Failed ioctl(SIOCGSTAMP)");
		ntpc_gettime(&udp_arrival_ntp->coarse, &udp_arrival_ntp->fine);
	} else {
		udp_arrival_ntp->coarse = udp_arrival.tv_sec + JAN_1970;
		udp_arrival_ntp->fine = NTPFRAC(udp_arrival.tv_usec);
	}
#else
	(void)usd;		/* not used */
	ntpc_gettime(&udp_arrival_ntp->coarse, &udp_arrival_ntp->fine);
#endif
}

static int check_source(int data_len, struct sockaddr_storage *sa_source, struct ntp_control *ntpc)
{
	struct sockaddr_in6 *ipv6;
	struct sockaddr_in *ipv4;
	uint16_t port;

	(void)data_len;
	(void)ntpc;		/* not used */
	DBG("packet of length %d received", data_len);

	if (sa_source->ss_family == AF_INET) {
		ipv4 = (struct sockaddr_in *)(sa_source);
		port = ntohs(ipv4->sin_port);
	} else if (sa_source->ss_family == AF_INET6) {
		ipv6 = (struct sockaddr_in6 *)(sa_source);
		port = ntohs(ipv6->sin6_port);
	} else {
		DBG("%s: Unsupported address family", __func__);
		return 1;
	}

	/*
	 * we could check that the source is the server we expect, but
	 * Denys Vlasenko recommends against it: multihomed hosts get it
	 * wrong too often.
	 */
	if (NTP_PORT != port) {
		INFO("%s: invalid port: %u", __func__, port);
		return 1;
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

/* Does more than print, so this name is bogus.
 * It also makes time adjustments, both sudden (-s)
 * and phase-locking (-l).
 * sets *error to the number of microseconds uncertainty in answer
 * returns 0 normally, 1 if the message fails sanity checks
 */
static int rfc1305print(uint32_t *data, struct ntptime *arrival, struct ntp_control *ntpc, int *error)
{
	/* straight out of RFC-1305 Appendix A */
	int li, vn, mode, stratum, prec;
	int delay, disp;

#ifdef ENABLE_DEBUG
	int poll, refid;
	struct ntptime reftime;
#endif
	struct ntptime orgtime, rectime, xmttime;
	double el_time, st_time, skew1, skew2;
	int freq;
	const char *drop_reason = NULL;

#define Data(i) ntohl(((uint32_t *)data)[i])
	li      = Data(0) >> 30 & 0x03;
	vn      = Data(0) >> 27 & 0x07;
	mode    = Data(0) >> 24 & 0x07;
	stratum = Data(0) >> 16 & 0xff;
#ifdef ENABLE_DEBUG
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
	if (debug) {
		DBG("LI=%d  VN=%d  Mode=%d  Stratum=%d  Poll=%d  Precision=%d", li, vn, mode, stratum, poll, prec);
		DBG("Delay=%.1f  Dispersion=%.1f  Refid=%u.%u.%u.%u", sec2u(delay), sec2u(disp),
		      refid >> 24 & 0xff, refid >> 16 & 0xff, refid >> 8 & 0xff, refid & 0xff);
		DBG("Reference %u.%.6u", reftime.coarse, USEC(reftime.fine));
		DBG("(sent)    %u.%.6u", ntpc->time_of_send[0], USEC(ntpc->time_of_send[1]));
		DBG("Originate %u.%.6u", orgtime.coarse, USEC(orgtime.fine));
		DBG("Receive   %u.%.6u", rectime.coarse, USEC(rectime.fine));
		DBG("Transmit  %u.%.6u", xmttime.coarse, USEC(xmttime.fine));
		DBG("Our recv  %u.%.6u", arrival->coarse, USEC(arrival->fine));
	}
#endif

	el_time = ntpdiff(&orgtime, arrival);	/* elapsed */
	st_time = ntpdiff(&rectime, &xmttime);	/* stall */
	skew1 = ntpdiff(&orgtime, &rectime);
	skew2 = ntpdiff(&xmttime, arrival);
	freq = get_current_freq();

#ifdef ENABLE_DEBUG
	if (debug) {
		DBG("Total elapsed: %9.2f", el_time);
		DBG("Server stall:  %9.2f", st_time);
		DBG("Slop:          %9.2f", el_time - st_time);
		DBG("Skew:          %9.2f", (skew1 - skew2) / 2);
		DBG("Frequency:     %9d", freq);
		DBG(" Day   Second     Elapsed    Stall     Skew  Dispersion  Freq");
	}
#endif

	/* error checking, see RFC-4330 section 5 */
#define FAIL(x) do { drop_reason=(x); goto fail;} while (0)
	if (ntpc->cross_check) {
		if (li == 3)
			FAIL("LI==3");	/* unsynchronized */
		if (vn < 3)
			FAIL("VN<3");	/* RFC-4330 documents SNTP v4, but we interoperate with NTP v3 */
		if (mode != 4)
			FAIL("MODE!=3");
		if (orgtime.coarse != ntpc->time_of_send[0] || orgtime.fine != ntpc->time_of_send[1])
			FAIL("ORG!=sent");
		if (xmttime.coarse == 0 && xmttime.fine == 0)
			FAIL("XMT==0");
		if (delay > 65536 || delay < -65536)
			FAIL("abs(DELAY)>65536");
		if (disp > 65536 || disp < -65536)
			FAIL("abs(DISP)>65536");
		if (stratum == 0)
			FAIL("STRATUM==0");	/* kiss o' death */
#undef FAIL
	}

	/* XXX should I do this if debug flag is set? */
	if (ntpc->set_clock) {	/* CAP_SYS_TIME or root required, or ntpclient will exit here! */
		set_time(&xmttime);
		if (verbose) {
			LOG("Time synchronized to server %s, stratum %d", ntpc->server, stratum);
		}
	}

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

		if (!debug && new_freq != freq)
			set_freq(new_freq);
	}

	/* Display by default for ntpclient users, sntpd users need to supply -v */
	if (verbose || ntpc->usermode) {
		LOG("%d %.5d.%.3d  %8.1f %8.1f  %8.1f %8.1f %9d",
		      arrival->coarse / 86400, arrival->coarse % 86400,
		      arrival->fine / 4294967, el_time, st_time,
		      (skew1 - skew2) / 2, sec2u(disp), freq);
	}
	*error = el_time - st_time;

	return 0;
 fail:
	if (debug || verbose) {
		ERR(0, "%d %.5d.%.3d rejected packet: %s",
		      arrival->coarse / 86400, arrival->coarse % 86400,
		      arrival->fine / 4294967, drop_reason);
	}

	return 1;
}

static void setup_receive(int usd, sa_family_t sin_family, uint16_t port)
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
		exit(1);
	}
	/* listen(usd,3); this isn't TCP; thanks Alexander! */
}

static void setup_transmit(int usd, struct sockaddr_storage *ssp , uint16_t port, struct ntp_control *ntpc)
{
	struct sockaddr_in6 *ipv6;
	struct sockaddr_in *ipv4;
	socklen_t len = 0;

	/* Prefer IPv4 over IPv6, for now */
	if (ssp->ss_family == AF_INET) {
		ipv4 = (struct sockaddr_in *)ssp;
		ipv4->sin_port = htons(port);
		len = sizeof(struct sockaddr_in);
	} else if (ssp->ss_family == AF_INET6) {
		ipv6 = (struct sockaddr_in6 *)ssp;
		ipv6->sin6_port = htons(port);
		len = sizeof(struct sockaddr_in6);
	} else {
		ERR(0, "%s: Unsupported address family", __func__);
		exit(1);
	}

	while (connect(usd, (struct sockaddr *)ssp, len) == -1) {
		if (ntpc->live) {
			/* Wait here a while, networking is probably not up yet. */
			sleep(1);
			continue;
		}

		ERR(errno, "Failed connecting to NTP server");
		exit(1);
	}
}

static int getaddrbyname(char *host, struct sockaddr_storage *ss)
{
	int err;
	static int netdown = 0;
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *rp;

	if (!host || !ss) {
		errno = EINVAL;
		return 1;
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = 0;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	memset(ss, 0, sizeof(struct sockaddr_storage));
	err = getaddrinfo(host, NULL, &hints, &result);
	if (err) {
		ERR(0, "Failed resolving address to hostname %s: %s", host, gai_strerror(err));
		netdown = errno = ENETDOWN;
		return 1;
	}

	/* The first result will be used. IPV4 has higher priority */
	err = 1;
	for (rp = result; rp; rp = rp->ai_next) {
		if (rp->ai_family == AF_INET) {
			memcpy(ss, (struct sockaddr_in *)(rp->ai_addr), sizeof(struct sockaddr_in));
			err = 0;
			break;
		}
		if (rp->ai_family == AF_INET6) {
			memcpy(ss, (struct sockaddr_in6 *)(rp->ai_addr), sizeof(struct sockaddr_in6));
			err = 0;
			break;
		}
	}
	freeaddrinfo(result);

	if (err) {
		errno = EAGAIN;
		return 1;
	}

	if (netdown) {
		LOG("Network up, resolved address to hostname %s", host);
		netdown = 0;
	}

	return 0;
}

static int setup_socket(struct ntp_control *ntpc)
{
	int sd;
	struct sockaddr_storage ss;

	while (getaddrbyname(ntpc->server, &ss)) {
		if (EINVAL != errno && ntpc->live) {
			/* Wait here a while, networking is probably not up yet. */
			sleep(1);
			continue;
		}

		if (verbose)
			ERR(0, "Unable lookup %s", ntpc->server);

		exit(1);
	}

	/* open socket based on the server address family */
	if (ss.ss_family == AF_INET || ss.ss_family == AF_INET6) {
		sd = socket(ss.ss_family, SOCK_DGRAM, IPPROTO_UDP);
	} else {
		ERR(0, "%s: Unsupported address family", __func__);
		exit(1);
	}

	if (sd == -1) {
	    return -1;
	}

	setup_receive(sd, ss.ss_family, ntpc->local_udp_port);
	setup_transmit(sd, &ss, NTP_PORT, ntpc);

	/*
	 * Every day: reopen socket and perform a new DNS lookup.
	 */
	alarm(60 * 60 * 24);

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

static void primary_loop(int usd, struct ntp_control *ntpc)
{
	fd_set fds;
	struct sockaddr_storage sa_xmit;
	int i, pack_len, probes_sent, error;
	socklen_t sa_xmit_len;
	struct timeval to;
	struct ntptime udp_arrival_ntp;
	static uint32_t incoming_word[325];

#define incoming ((char *) incoming_word)
#define sizeof_incoming (sizeof incoming_word)

#ifdef ENABLE_DEBUG
	if (debug)
		DBG("Listening...");
#endif
	probes_sent = 0;
	sa_xmit_len = sizeof(sa_xmit);
	to.tv_sec = 0;
	to.tv_usec = 0;

	while (1) {
		if (sigterm) {
			ntpc->live = 0;
			break;
		}
		if (sighup) {
			sighup = 0;
			to.tv_sec = 0;
			to.tv_usec = 0;
			close(usd);
			usd = setup_socket(ntpc);
			if (-1 == usd) {
				ERR(errno, "Failed reopening NTP socket");
				return;
			}
			DBG("Got SIGHUP, triggering resync with NTP server.");
		}

		FD_ZERO(&fds);
		FD_SET(usd, &fds);
		i = select(usd + 1, &fds, NULL, NULL, &to);	/* Wait on read or error */
		if ((i != 1) || (!FD_ISSET(usd, &fds))) {
			if (i < 0) {
				if (errno != EINTR)
					ERR(errno, "Failed select()");
				continue;
			}
			if (to.tv_sec == 0) {
				if (probes_sent >= ntpc->probe_count && ntpc->probe_count != 0)
					break;

				send_packet(usd, ntpc->time_of_send);
				++probes_sent;
				to.tv_sec = ntpc->cycle_time;
				to.tv_usec = 0;
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
			if (rfc1305print(incoming_word, &udp_arrival_ntp, ntpc, &error) != 0)
				continue;
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
			if (debug)
				DBG("Contemplate %u %.1f %.1f %d", absolute, skew, errorbar, freq);
#endif
			if (last_fake_time == 0)
				simulated_freq = freq;
			fake_delta_time += (absolute - last_fake_time) * ((double)(freq - simulated_freq)) / 65536;
#ifdef ENABLE_DEBUG
			if (debug)
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

static void run(struct ntp_control *ntpc)
{
	int usd;

	if (initial_freq) {
#ifdef ENABLE_DEBUG
		DBG("Initial frequency %d", initial_freq);
#endif
		set_freq(initial_freq);
	}

	if (ntpc->set_clock && !ntpc->live && !ntpc->goodness && !ntpc->probe_count)
		ntpc->probe_count = 1;

	/* If user gives a probe count, then assume non-live run and verbose reporting. */
	if (ntpc->probe_count > 0) {
		ntpc->live = 0;
		verbose = 1;
	}

	/* respect only applicable MUST of RFC-4330 */
	if (ntpc->probe_count != 1 && ntpc->cycle_time < MIN_INTERVAL)
		ntpc->cycle_time = MIN_INTERVAL;

#ifdef ENABLE_DEBUG
	if (debug) {
		DBG("Configuration:");
		DBG("  -c probe_count %d", ntpc->probe_count);
		DBG("  -d (debug)     %d", debug);
		DBG("  -g goodness    %d", ntpc->goodness);
		DBG("  -h hostname    %s", ntpc->server);
		DBG("  -i interval    %d", ntpc->cycle_time);
		DBG("  -l live        %d", ntpc->live);
		DBG("  -p local_port  %d", ntpc->local_udp_port);
		DBG("  -q min_delay   %f", min_delay);
		DBG("  -s set_clock   %d", ntpc->set_clock);
		DBG("  -t cross_check %d", ntpc->cross_check);
	}
#endif

	/* Startup sequence */
	if (daemonize) {
		if (-1 == daemon(0, 0)) {
			ERR(errno, "Failed daemonizing, aborting");
			exit(1);
		}

		/*
		 * Force output to syslog, we have no other way of
		 * communicating with the user after being daemonized
		 */
		log_enable = 1;
		if (verbose)
			LOG("Starting ntpclient v" PACKAGE_VERSION);
	}

	usd = setup_socket(ntpc);
	if (usd == -1) {
		ERR(errno, "Failed creating UDP socket() to SNTP server");
		exit(1);
	}

	setup_signals();

	if (daemonize && verbose)
		LOG("Using time sync server: %s", ntpc->server);

	primary_loop(usd, ntpc);

	if (daemonize && verbose)
		LOG("Stopping ntpclient v" PACKAGE_VERSION);
	close(usd);
}

static int ntpclient_usage(int code)
{
	fprintf(stderr,
		"Usage:\n"
		"  ntpclient [-c count] [-d] [-f frequency] [-g goodness] -h hostname\n"
		"            [-i interval] [-l] [-p port] [-q min_delay] [-r] [-s] [-t]\n"
		"\n"
		"Options:\n"
		"  -c count      stop after count time measurements (default 0 means go forever)\n"
		"  -d            print diagnostics (feature can be disabled at compile time)\n"
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
			debug++;
			break;

		case 'f':
			initial_freq = atoi(optarg);
			break;

		case 'g':
			ntpc.goodness = atoi(optarg);
			break;

		case 'h':
			ntpc.server = optarg;
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

	if (!ntpc.server)
		return ntpclient_usage(1);

	log_init();

	run(&ntpc);

	log_exit();

	return 0;
}

static int usage(int code)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s [-dhln" REPLAY_OPTION "tvV] [-i SEC] [-p PORT] [-q MSEC] [SERVER]\n"
		"\n"
		"Options:\n"
		"  -d       Debug, or diagnostics mode.  Possible to enable more at compile\n"
		"  -h       Show summary of command line options and exit\n"
		"  -i SEC   Check time every interval seconds.  Default: 600\n"
		"  -l       Use syslog instead of stdout for log messages, default unless -n\n"
		"  -n       Don't fork.  Prevents %s from daemonizing by default\n"
		"           Use -L with this to use syslog as well, for Finit + systemd\n"
		"  -p PORT  NTP client UDP port.  Default: 0 (\"any available\")\n"
		"  -q MSEC  Minimum packet delay for transaction, default: 800 msec\n"
#ifdef ENABLE_REPLAY
		"  -r       Replay analysis code based on stdin\n"
#endif
		"  -t       Trust network and server, no RFC-4330 recommended validation\n"
		"  -v       Verbose, show time sync events, hostname lookup errors, etc.\n"
		"  -V       Show program version\n"
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
	int c;

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
	ntpc.cross_check = 0;

	/* Default to daemon mode for sntpd */
	daemonize        = 1;

	while (1) {
		char opts[] = "dhi:lnp:q:" REPLAY_OPTION "tvV?";

		c = getopt(argc, argv, opts);
		if (c == EOF)
			break;

		switch (c) {
		case 'd':
			debug++;
			break;

		case 'h':
			return usage(0);

		case 'i':
			ntpc.cycle_time = atoi(optarg);
			break;

		case 'l':
			log_enable++;
			break;

		case 'n':
			daemonize = 0;
			log_enable--;
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

		case 't':
			ntpc.cross_check = 0;
			break;

		case 'v':
			++verbose;
			break;

		case 'V':
			puts("v" PACKAGE_VERSION);
			return 0;

		case '?':
		default:
			return usage(0);
		}
	}

	log_init();

	if (optind < argc && ntpc.server == NULL)
		ntpc.server = argv[optind];

	if (ntpc.server == NULL) {
		ntpc.server = (char *)"pool.ntp.org";
		DBG("Using server %s", ntpc.server);
	}

	run(&ntpc);

	log_exit();

	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
