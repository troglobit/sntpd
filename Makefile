# Makefile for ntpclient, an RFC-1305 client for UNIX systems   -*-Makefile-*-

# Larry's versioning scheme: YYYY_DOY, i.e., "%Y_%j"
#VERSION      ?= $(shell git tag -l | tail -1)
VERSION      ?= `date +"%Y_%j"`
NAME          = ntpclient
EXECS        ?= $(NAME) adjtimex mini-ntpclient
PKG           = $(NAME)-$(VERSION)
ARCHIVE       = $(PKG).tar.bz2
MANS          = $(addsuffix .8, $(EXECS))

RM           ?= rm -f
CC           ?= $(CROSS)gcc

prefix       ?= /usr/local
sysconfdir   ?= /etc
datadir       = $(prefix)/share/doc/ntpclient
mandir        = $(prefix)/share/man/man8

OBJS	      = ntpclient.o phaselock.o
CFLAGS        = -DVERSION_STRING=\"$(VERSION)\" $(CFG_INC) $(EXTRA_CFLAGS)
CFLAGS       += -O2 -std=c99 -D_BSD_SOURCE
CFLAGS       += -W -Wall -Wpointer-arith -Wcast-align -Wcast-qual -Wshadow
CFLAGS       += -Waggregate-return -Wnested-externs -Winline -Wwrite-strings
CFLAGS       += -Wstrict-prototypes -Wno-strict-aliasing
CFLAGS       += -DENABLE_SYSLOG
#CFLAGS       += -DPRECISION_SIOCGSTAMP
#CFLAGS       += -DENABLE_DEBUG
#CFLAGS       += -DUSE_OBSOLETE_GETTIMEOFDAY
#CFLAGS       += -DENABLE_REPLAY
LDLIBS       += -lrt
DISTFILES     = README HOWTO

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

mini-ntpclient: mini-ntpclient.o
	@printf "  LINK    $@\n"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Need to build with -DENABLE_DEBUG for this to work.
test: ntpclient
	./ntpclient -d -r <test.dat

install: $(EXECS)
	@install -d $(DESTDIR)$(prefix)/sbin
	@install -d $(DESTDIR)$(datadir)
	@install -d $(DESTDIR)$(mandir)
	@for file in $(EXECS); do					\
		install -m 0755 $$file $(DESTDIR)$(prefix)/sbin/$$file;	\
	done
	@for file in $(MANS); do					\
		install -m 0644 $$file $(DESTDIR)$(mandir)/$$file;	\
	done
	@for file in $(DISTFILES); do					\
		install -m 0644 $$file $(DESTDIR)$(datadir)/$$file;	\
	done

uninstall:
	-@for file in $(EXECS); do			\
		$(RM) $(DESTDIR)$(prefix)/sbin/$$file;	\
	done
	-@for file in $(MANS); do			\
		$(RM) $(DESTDIR)$(mandir)/$$file;	\
	done
	-@$(RM) -r $(DESTDIR)$(datadir)

clean:
	-@$(RM) -f *.o

distclean: clean
	-@$(RM) -f $(EXECS)

