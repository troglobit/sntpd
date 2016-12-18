/* NTP client
 *
 * Copyright (C) 1997, 1999, 2000, 2003, 2006, 2007  Larry Doolittle <larry@doolittle.boa.org>
 * Copyright (C) 2010-2016  Joachim Nilsson <troglobit@gmail.com>
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

# include <stdarg.h>		/* vsyslog(), vfprintf(), use -D_BSD_SOURCE */
#ifdef ENABLE_SYSLOG
# include <syslog.h>
# include <sys/utsname.h>
# include <sys/time.h>
# define SYSLOG_IDENT    "ntpclient"
# define SYSLOG_OPTIONS  (LOG_NOWAIT | LOG_PID)
# define SYSLOG_FACILITY LOG_CRON
# define LOG_OPTION      "L"
#else
#define LOG_ERR     0
#define LOG_WARNING 1
#define LOG_NOTICE  2
#define LOG_DEBUG   3
#define LOG_OPTION
#endif

extern int debug;
extern int logging;
extern int verbose;

extern double min_delay;        /* global tuning parameter */

/* prototypes for functions defined in ntpclient.c */
void logit(int severity, int syserr, const char *format, ...);

/* prototype for function defined in phaselock.c */
int contemplate_data(unsigned int absolute, double skew, double errorbar, int freq);

#endif /* NTPCLIENT_H_ */
