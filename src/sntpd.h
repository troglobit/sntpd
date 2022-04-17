/* NTP client
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

#ifndef NTPCLIENT_H_
#define NTPCLIENT_H_

#include "config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <unistd.h>

#define LOG_OPTS        (LOG_NOWAIT | LOG_PID)
#define LOG_FACILITY    LOG_CRON

#ifdef ENABLE_REPLAY
# define REPLAY_OPTION   "r"
#else
# define REPLAY_OPTION   ""
#endif

#define ERR(code, fmt, args...)  logit(LOG_ERR,    code, fmt, ##args)
#define LOG(fmt,  args...)       logit(LOG_NOTICE,    0, fmt, ##args)
#define INFO(fmt, args...)       logit(LOG_INFO,      0, fmt, ##args)
#define DBG(fmt,  args...)       logit(LOG_DEBUG,     0, fmt, ##args)

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

#ifndef MIN_DISP
#define MIN_DISP 0.01
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

/* precision is defined as the larger of the resolution and time to
 * read the clock, in log2 units.  For instance, the precision of a
 * mains-frequency clock incrementing at 60 Hz is 16 ms, even when the
 * system clock hardware representation is to the nanosecond.
 *
 * Delays, jitters of various kinds are clamped down to precision.
 *
 * If precision_sec is too large, discipline_jitter gets clamped to it
 * and if offset is smaller than discipline_jitter * POLLADJ_GATE, poll
 * interval grows even though we really can benefit from staying at
 * smaller one, collecting non-lagged datapoits and correcting offset.
 * (Lagged datapoits exist when poll_exp is large but we still have
 * systematic offset error - the time distance between datapoints
 * is significant and older datapoints have smaller offsets.
 * This makes our offset estimation a bit smaller than reality)
 * Due to this effect, setting G_precision_sec close to
 * STEP_THRESHOLD isn't such a good idea - offsets may grow
 * too big and we will step. I observed it with -6.
 *
 * OTOH, setting precision_sec far too small would result in futile
 * attempts to synchronize to an unachievable precision.
 *
 * -6 is 1/64 sec, -7 is 1/128 sec and so on.
 * -8 is 1/256 ~= 0.003906 (worked well for me --vda)
 * -9 is 1/512 ~= 0.001953 (let's try this for some time)
 */
#define G_precision_exp  -9
/*
 * G_precision_exp is used only for construction outgoing packets.
 * It's ok to set G_precision_sec to a slightly different value
 * (One which is "nicer looking" in logs).
 * Exact value would be (1.0 / (1 << (- G_precision_exp))):
 */
#define G_precision_sec  0.002

struct ntptime {
	uint32_t coarse;
	uint32_t fine;
};

struct ntptimes {
	uint16_t coarse;
	uint16_t fine;
};

struct ntp_control {
	struct ntptime time_of_send;
	int usermode;		/* 0: sntpd, 1: ntpclient */
	int live;
	int set_clock;		/* non-zero presumably needs CAP_SYS_TIME or root */
	int probe_count;
	int cycle_time;
	int goodness;
	int cross_check;

	uint16_t local_udp_port;
	uint16_t udp_port;	/* remote port on 'server' */
	uint16_t server_port;
	char *server;		/* must be set in client mode */
	char serv_addr[4];
};

struct ntp {
	uint8_t flags;			/*  0: */
	uint8_t stratum;		/*  1: */
	uint8_t interval;		/*  2: */
	int8_t precision;		/*  3: */

	struct ntptimes delay;		/*  4: */
	struct ntptimes dispersion;	/*  8: */

	char identifier[4];		/* 12: */

	struct ntptime refclk_ts;	/* 16: */
	struct ntptime origin_ts;	/* 24: */
	struct ntptime recv_ts;		/* 32: */
	struct ntptime xmit_ts;		/* 40: */
};

struct ntp_peers {
	struct ntptime last_update_ts;

	double last_rootdelay;
	double last_rootdisp;
	double last_delay;
};

extern struct ntp_peers peer;
extern const char *prognm;
extern double min_delay;        /* global tuning parameter */
extern double root_delay;
extern double root_dispersion;

/* logit.c */
void log_init(int use_syslog, int level);
void log_exit(void);
int  log_str2lvl(char *arg);

void logit(int severity, int syserr, const char *format, ...) __attribute__ ((format (printf, 3, 4)));

/* phaselock.c */
int contemplate_data(unsigned int absolute, double skew, double errorbar, int freq);

/* sntpd.c */
int  setup_receive(int sd, sa_family_t sin_family, uint16_t port);
void get_packet_timestamp(int usd, struct ntptime *nt);
void ntpc_gettime(struct ntptime *nt);

/* server.c */
int server_init(uint16_t port);
int server_recv(int sd);

/* Workaround for missing SIOCGSTAMP after Linux 5.1 64-bit timestamp fixes. */
#ifndef SIOCGSTAMP
# ifdef SIOCGSTAMP_OLD
#  define SIOCGSTAMP SIOCGSTAMP_OLD
# else
#  define SIOCGSTAMP 0x8906
# endif
#endif

/* Misc. helpers */
static inline double wire2d64(struct ntptime *nt)
{
	return (double)nt->coarse + ((double)nt->fine / UINT_MAX);
}

static inline double wire2d32(struct ntptimes *nt)
{
	return (double)nt->coarse + ((double)nt->fine / USHRT_MAX);
}

static inline struct ntptimes u2sec(double x)
{
	struct ntptimes nts;

	nts.coarse = (uint16_t)x;
	nts.fine   = (uint16_t)((x - nts.coarse) * USHRT_MAX);
//	DBG("double %f => 32-bit int %d.%d", x, nts.coarse, nts.fine);

	nts.coarse = htons(nts.coarse);
	nts.fine   = htons(nts.fine);

	return nts;
}

#endif /* NTPCLIENT_H_ */
