AM_CFLAGS           = -W -Wall -Wextra -std=gnu99
AM_CFLAGS          += -Wno-unused-parameter -Wpointer-arith -Wcast-align
AM_CFLAGS          += -Wcast-qual -Wshadow -Waggregate-return -Winline
AM_CFLAGS          += -Wnested-externs -Wwrite-strings -Waggregate-return
# _DEFAULT_SOURCE replaces _BSD_SOURCE and _GNU_SOURCE, but we need to keep
# _BSD_SOURCE to be able to build on older GLIBC and other C libs.
AM_CPPFLAGS         = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=199309 -D_BSD_SOURCE
AM_CFLAGS          += -Wstrict-prototypes -Wmissing-prototypes -pedantic

bin_PROGRAMS        = adjtimex
sbin_PROGRAMS       = ntpclient

adjtimex_SOURCES    = adjtimex.c

if MINI
ntpclient_SOURCES   = mini-ntpclient.c
else
ntpclient_SOURCES   = ntpclient.c ntpclient.h phaselock.c
ntpclient_LIBS      = -lrt
endif
