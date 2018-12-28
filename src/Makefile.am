AM_CFLAGS           = -W -Wall -Wextra -std=gnu99
AM_CFLAGS          += -Wno-unused-parameter -Wpointer-arith -Winline
AM_CFLAGS          += -Wshadow -Waggregate-return -Wnested-externs
AM_CFLAGS          += -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes

# _DEFAULT_SOURCE replaces _BSD_SOURCE and _GNU_SOURCE, but we need to keep
# _BSD_SOURCE to be able to build on older GLIBC and other C libs.
AM_CPPFLAGS         = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=199309 -D_BSD_SOURCE

bin_PROGRAMS        = adjtimex
adjtimex_SOURCES    = adjtimex.c

if MINI
sbin_PROGRAMS       = ntpclient
ntpclient_SOURCES   = mini-ntpclient.c
else
sbin_PROGRAMS       = sntpd
sntpd_SOURCES       = sntpd.c sntpd.h phaselock.c
sntpd_LIBS          = -lrt
endif
