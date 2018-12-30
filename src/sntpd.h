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

#ifndef NTPCLIENT_H_
#define NTPCLIENT_H_

#include <config.h>
#include <syslog.h>
#include <sys/utsname.h>
#include <sys/time.h>

#define LOG_OPTS        (LOG_NOWAIT | LOG_PID)
#define LOG_FACILITY    LOG_CRON

#ifdef ENABLE_REPLAY
# define REPLAY_OPTION   "r"
#else
# define REPLAY_OPTION   ""
#endif

extern int debug;
extern int log_enable;
extern int verbose;

extern const char *prognm;

extern double min_delay;        /* global tuning parameter */

/* logit.c */
void log_init(void);
void log_exit(void);
void logit(int severity, int syserr, const char *format, ...);

/* phaselock.c */
int contemplate_data(unsigned int absolute, double skew, double errorbar, int freq);

#endif /* NTPCLIENT_H_ */
