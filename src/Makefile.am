AM_CFLAGS           = -W -Wall -Wextra -std=gnu99
AM_CFLAGS          += -Wno-unused-parameter -Wpointer-arith -Winline
AM_CFLAGS          += -Wshadow -Waggregate-return -Wnested-externs
AM_CFLAGS          += -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes

# _DEFAULT_SOURCE replaces _BSD_SOURCE and _GNU_SOURCE, but we need to keep
# _BSD_SOURCE to be able to build on older GLIBC and other C libs.
AM_CPPFLAGS         = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=199309 -D_BSD_SOURCE

bin_PROGRAMS        = adjtimex
adjtimex_SOURCES    = adjtimex.c

sbin_PROGRAMS       = sntpd
sntpd_SOURCES       = sntpd.c sntpd.h phaselock.c
sntpd_LIBS          = -lrt
SYMLINK             = ntpclient

# Hook in install to add sntpd --> ntpclient symlink
install-exec-hook:
	@for file in $(SYMLINK); do			\
		link=$(DESTDIR)$(sbindir)/$$file;	\
		test -e $$link && continue;		\
		$(LN_S) $(sbin_PROGRAMS) $$link;	\
	done

uninstall-hook:
	@for file in $(SYMLINK); do			\
		$(RM) $(DESTDIR)$(sbindir)/$$file;	\
	done
