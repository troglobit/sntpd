#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>		/* vsyslog(), vfprintf(), use -D_BSD_SOURCE */
#include <string.h>
#define SYSLOG_NAMES
#include <syslog.h>
#include <sys/param.h>		/* MIN()/MAX() */

#include "sntpd.h"

struct {
	const char *name;
	int val;
} prionm[] =
{
	{ "none",    INTERNAL_NOPRI },		/* INTERNAL */
	{ "crit",    LOG_CRIT       },
	{ "alert",   LOG_ALERT      },
	{ "panic",   LOG_EMERG      },
	{ "error",   LOG_ERR        },
	{ "warning", LOG_WARNING    },
	{ "notice",  LOG_NOTICE     },
	{ "info",    LOG_INFO       },
	{ "debug",   LOG_DEBUG      },
	{ NULL, -1 }
};

static int log_enable = 0;
static int log_level = LOG_NOTICE;

void log_init(int use_syslog, int level)
{
	log_enable = use_syslog;
	log_level  = level;

	if (log_enable <= 0)
		return;

	openlog(prognm, LOG_OPTS, LOG_FACILITY);
	setlogmask(LOG_UPTO(log_level));
}

void log_exit(void)
{
	if (log_enable <= 0)
		return;

	closelog();
}

int log_str2lvl(char *arg)
{
	int i;

	for (i = 0; prionm[i].name; i++) {
		size_t len = MIN(strlen(prionm[i].name), strlen(arg));

		if (!strncasecmp(prionm[i].name, arg, len))
			return prionm[i].val;
	}

	return atoi(arg);
}

void logit(int severity, int syserr, const char *format, ...)
{
	va_list ap;
	char buf[200];

	if (log_level < severity)
		return;

	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	if (log_enable > 0) {
		if (syserr)
			syslog(severity, "%s: %s", buf, strerror(syserr));
		else
			syslog(severity, "%s", buf);

		return;
	}

	if (severity == LOG_WARNING)
		fputs("Warning - ", stderr);
	else if (severity == LOG_ERR)
		fputs("ERROR - ", stderr);

	if (syserr)
		fprintf(stderr, "%s: %s\n", buf, strerror(errno));
	else
		fprintf(stderr, "%s\n", buf);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
