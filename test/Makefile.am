AM_CFLAGS            = -W -Wall -Wextra -std=gnu99
AM_CFLAGS           += -Wno-unused-parameter -Wpointer-arith
AM_CPPFLAGS          = -D_DEFAULT_SOURCE -D_BSD_SOURCE -I$(top_srcdir)/src

check_PROGRAMS       = fake-ntpd test-parse test-peer
fake_ntpd_SOURCES    = fake-ntpd.c
test_parse_SOURCES   = test-parse.c $(top_srcdir)/src/peer.c $(top_srcdir)/src/logit.c
test_peer_SOURCES    = test-peer.c $(top_srcdir)/src/peer.c $(top_srcdir)/src/logit.c

AM_TESTS_ENVIRONMENT = top_builddir='$(top_builddir)'; export top_builddir;
AM_TESTS_ENVIRONMENT+= builddir='$(builddir)'; export builddir;

TESTS                = test-parse test-peer selfcheck.sh ipv6.sh failover.sh iburst.sh resolve.sh multiaddr.sh \
                       prefer.sh kod.sh

EXTRA_DIST           = $(TESTS) lib/setup.sh lib/start.sh
