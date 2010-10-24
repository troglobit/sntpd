# Makefile for ntpclient, an RFC-1305 client for UNIX systems   -*-Makefile-*-

#VERSION      ?= $(shell git tag -l | tail -1)
VERSION      ?= 2010_10-rc1
NAME          = ntpclient
EXECS         = $(NAME) adjtimex
PKG           = $(NAME)-$(VERSION)
ARCHIVE       = $(PKG).tar.bz2

OBJS	      = ntpclient.o phaselock.o
CFLAGS        = -DVERSION_STRING=\"$(VERSION)\" $(CFG_INC) $(EXTRA_CFLAGS)
CFLAGS       += -O2 -std=c99 -D_GNU_SOURCE
CFLAGS       += -W -Wall -Wpointer-arith -Wcast-align -Wcast-qual -Wshadow
CFLAGS       += -Waggregate-return -Wnested-externs -Winline -Wwrite-strings
CFLAGS       += -Wstrict-prototypes
#CFLAGS       += -DPRECISION_SIOCGSTAMP
#CFLAGS       += -DENABLE_DEBUG
#CFLAGS       += -DUSE_OBSOLETE_GETTIMEOFDAY
CFLAGS       += -DENABLE_REPLAY
LDLIBS       += -lrt

# A long time ago, far, far away, under Solaris, you needed to
#    CFLAGS += -xO2 -Xc
#    LDLIBS += -lnsl -lsocket
# To cross-compile
#    CC = arm-linux-gcc
# To check for lint
# -Wundef not recognized by gcc-2.7.2.3

# Pattern rules
.c.o:
	@printf "  CC      $@\n"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# Build rules
all: $(EXECS)

ntpclient: $(OBJS)
	@printf "  LINK    $@\n"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

ntpclient.o phaselock.o: ntpclient.h

adjtimex: adjtimex.o
	@printf "  LINK    $@\n"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: ntpclient
	./ntpclient -d -r <test.dat

clean:
	-@$(RM) -f *.o

distclean: clean
	-@$(RM) -f $(EXECS)

