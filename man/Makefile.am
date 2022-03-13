EXTRA_DIST     = adjtimex.1

dist_man8_MANS = sntpd.8

if WITH_ADJTIMEX
dist_man1_MANS = adjtimex.1
endif

if WITH_NTPCLIENT
SYMLINK        = ntpclient.8

# Hook in install to add sntpd.8 --> ntpclient.8 symlink
install-data-hook:
	for file in $(SYMLINK); do			\
		link=$(DESTDIR)$(man8dir)/$$file;	\
		test -e $$link && continue;		\
		$(LN_S) $(dist_man8_MANS) $$link;	\
	done

uninstall-hook:
	for file in $(SYMLINK); do			\
		$(RM) $(DESTDIR)$(man8dir)/$$file;	\
	done
endif
