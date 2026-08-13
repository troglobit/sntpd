AM_CFLAGS            = -W -Wall -Wextra -std=gnu99
AM_CFLAGS           += -Wno-unused-parameter -Wpointer-arith
AM_CPPFLAGS          = -D_DEFAULT_SOURCE -D_BSD_SOURCE -I$(top_srcdir)/src

check_PROGRAMS       = fake-ntpd
fake_ntpd_SOURCES    = fake-ntpd.c

AM_TESTS_ENVIRONMENT = top_builddir='$(top_builddir)'; export top_builddir;
AM_TESTS_ENVIRONMENT+= builddir='$(builddir)'; export builddir;

TESTS                = selfcheck.sh ipv6.sh

EXTRA_DIST           = $(TESTS) lib/setup.sh lib/start.sh
