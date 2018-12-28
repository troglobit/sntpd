dist_man1_MANS = adjtimex.1

if MINI
dist_man8_MANS = ntpclient.8
else
dist_man8_MANS = sntpd.8
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
