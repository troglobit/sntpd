#include <errno.h>
#include <stdio.h>
#include <stdarg.h>		/* vsyslog(), vfprintf(), use -D_BSD_SOURCE */
#include <string.h>

#include "sntpd.h"

int log_enable = 0;
int log_level = LOG_NOTICE;

void log_init(void)
{
	if (debug > 0)
		log_level = LOG_DEBUG;

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
