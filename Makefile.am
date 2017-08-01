DISTCHECK_CONFIGURE_FLAGS = --with-systemd=$$dc_install_base/$(systemd)
SUBDIRS         = src doc man
doc_DATA        = README.md COPYING ChangeLog.md
EXTRA_DIST      = README.md ChangeLog.md

if SYSTEMD
systemd_DATA     = ntpclient.service
endif
