AM_CFLAGS           = -W -Wall -Wextra -std=gnu99
AM_CFLAGS          += -Wno-unused-parameter -Wpointer-arith -Wcast-align
AM_CFLAGS          += -Wcast-qual -Wshadow -Waggregate-return -Winline
AM_CFLAGS          += -Wnested-externs -Wwrite-strings -Waggregate-return
AM_CFLAGS          += -Wstrict-prototypes -Wmissing-prototypes -pedantic
AM_CPPFLAGS         = -D_POSIX_C_SOURCE=199309 -D_BSD_SOURCE

bin_PROGRAMS        = adjtimex
sbin_PROGRAMS       = ntpclient

adjtimex_SOURCES    = adjtimex.c

ntpclient_SOURCES   = ntpclient.c phaselock.c
ntpclient_CPPFLAGS  = -DENABLE_SYSLOG
#ntpclient_CPPFLAGS += -DENABLE_DEBUG
#ntpclient_CPPFLAGS += -DUSE_OBSOLETE_GETTIMEOFDAY
#ntpclient_CPPFLAGS += -DENABLE_REPLAY
ntpclient_LIBS      = -lrt
