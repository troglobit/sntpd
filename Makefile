# Makefile for ntpclient, an RFC-1305 client for UNIX systems   -*-Makefile-*-

# Larry's versioning scheme: YYYY_DOY, i.e., "%Y_%j"
#VERSION      ?= $(shell git tag -l | tail -1)
#VERSION      ?= `date +"%Y_%j"`
VERSION      ?= 2010_326
NAME          = ntpclient
EXECS        ?= $(NAME) adjtimex mini-ntpclient
PKG           = $(NAME)-$(VERSION)
ARCHIVE       = $(PKG).tar.bz2
MANS          = ntpclient.8 adjtimex.1

RM           ?= rm -f
CC           ?= $(CROSS)gcc

prefix       ?= /usr/local
sysconfdir   ?= /etc
datadir       = $(prefix)/share/doc/ntpclient
mandir        = $(prefix)/share/man

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
	@install -d $(DESTDIR)$(prefix)/bin
	@install -d $(DESTDIR)$(datadir)
	@install -d $(DESTDIR)$(mandir)/man8
	@install -d $(DESTDIR)$(mandir)/man1
	@install -m 0755 ntpclient $(DESTDIR)$(prefix)/sbin/ntpclient
	@install -m 0755 mini-ntpclient $(DESTDIR)$(prefix)/sbin/mini-ntpclient
	@install -m 0755 adjtimex $(DESTDIR)$(prefix)/bin/adjtimex
	@install -m 0644 ntpclient.8 $(DESTDIR)$(mandir)/man8/ntpclient.8
	@install -m 0644 adjtimex.1 $(DESTDIR)$(mandir)/man1/adjtimex.1
	@for file in $(DISTFILES); do					\
		install -m 0644 $$file $(DESTDIR)$(datadir)/$$file;	\
	done

uninstall:
	-@$(RM) $(DESTDIR)$(prefix)/sbin/ntpclient
	-@$(RM) $(DESTDIR)$(prefix)/sbin/mini-ntpclient
	-@$(RM) $(DESTDIR)$(prefix)/bin/adjtimex
	-@$(RM) $(DESTDIR)$(mandir)/man8/ntpclient.8
	-@$(RM) $(DESTDIR)$(mandir)/man1/adjtimex.1
	-@$(RM) -r $(DESTDIR)$(datadir)

clean:
	-@$(RM) -f *.o

distclean: clean
	-@$(RM) -f $(EXECS)

dist:
	@echo "Building bzip2 tarball of $(PKG) in parent dir..."
	git archive --format=tar --prefix=$(PKG)/ $(VERSION) | bzip2 >../$(ARCHIVE)
	@(cd ..; md5sum $(ARCHIVE) | tee $(ARCHIVE).md5)

