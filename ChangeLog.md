Change Log
==========

All notable changes to the project are documented in this file.
Changes to Larry's upstream version are interleaved.


[2018_244][] - 2018-09-01
-------------------------

### Changes
- Add HOWTO.md to distribution and install into docs

### Fixes
- Issue #3: Open network socket using the address family returned by the
  hostname, or address, lookup.  Fix by Laura Fang


[2018_176][] - 2018-06-25
-------------------------

The Stuttgart release.  Curated by [Joachim Nilsson][].

### Changes
- NTP server argument is now optional, default: `pool.ntp.org`
- Restore `.tar.gz` archives for `make dist` and release
- Make `.deb` file part of release
- Cleanup and simplify `README`
- Renmed `doc/` to `docs/`
- Moved `TODO` and `CONTRIBUTING.md` to `docs/`

### Fixes
- Issue #2: Missing fallback to IPv4 on systems with IPv6 disbled


[2017_246][] - 2017-09-03
-------------------------

Curated by [Joachim Nilsson][].

### Changes
- Log successful resolve of NTP host when network has been down
- Add support for building `.deb` package

### Fixes
- Fix build warnings
- Define `_DEFAULT_SOURCE`, needed with newer GLIBC
- Do not overload `in6addr_any`, exists in `netinet/in.h`


[2017_217][] - 2017-08-05
-------------------------

Curated by [Joachim Nilsson][].

### Changes
- Add support for IPv6, thanks to Dieter Ries
- Behavior change: `-L` now required with `-n`, otherwise `stdout` is
  used.  This to conform with de facto UNIX daemon behavior.
- Convert to GNU Configure & Build System, with proper `--enable-opts`
  for all optional features, like precision and mini NTP client
- Relocate files to `doc/` and `src/` subdirectories
- Reindent to Linux kernel coding style and add [CONTRIBIBUTING.md][]
  to help newcomers with basic project info
- Cleanup README and HOWTO, move to Markdown format for comfortable
  presentation on GitHub.
- Update and refactor `ntpclient.8` and `adjtimex.1` into mdoc format
- Move change log from README to separate CHANGELOG file in a format
  proposed by http://keepachangelog.com/
- Merge in changes from [2015_365][] by Larry
- Add missing `COPYING` file, GNU license should be included
- Add systemd unit file for running `ntpclient` as a daemon
- Add initial Debian packaging to be able to create `.deb` files

### Fixes
- Massive cleanup and build fixes to to `mini-ntpclient.c` from DD-WRT
- Fix compiler warnings for unused variables when `ENABLE_DEBUG` is unset
- Fix build error when building without `ENABLE_SYSLOG` support
- Fixes for non-GNU libc systems, replace `%m` and `__progname`
- Retry DNS lookup of NTP server, useful at boot and if net goes down
- Restart/reopen network socket at least once a day to prevent lockup


[2015_365][] - 2015-12-31
-------------------------

Curated by [Larry Doolittle][].  Changes since `ntpclient_2010_365.tar.gz`,
<http://doolittle.icarus.com/ntpclient/ntpclient_2015_365.tar.gz>

### Changes
- Tweak coding style to work better with modern compilers (e.g.,
  gcc-gcc-4.9.2 and clang-3.5.0)
- Make c99 the default
- List more warning flags in disabled part of Makefile
- Minor whitespace, parenthesis, and spelling changes in source files
- Move `_POSIX_C_SOURCE` and `_BSD_SOURCE` to Makefile

### Fixes
- Dixed and re-enabled `-DPRECISION_SIOCGSTAMP`, seems to work properly
  with modern (e.g., 3.x or 4.x) Linux kernel
- use `LDLIBES` instad of `LDFLAGS` for `-lrt` in Makefile (thanks, ChristophGr)


[2010_365][] - 2010-12-31
-------------------------

Curated by [Larry Doolittle][].  Changes since `ntpclient_2007_365.tar.gz`.
<http://doolittle.icarus.com/ntpclient/ntpclient_2010_365.tar.gz>

### Changes
- Include `netdb.h` and always define `_BSD_SOURCE` to get `herror()`
- Minor formatting to align with Nilsson's fork
- add `-fno-strict-aliasing` as needed by traditional network coding style

### Fixes
- Fixed type of `sa_xmit_len`, thanks [Mike Frysinger][]
- Dropped underscores in spelling of `adjtimex(2)`, might make uClibc happier


[2010_326][] - 2010-11-22
-------------------------

### Changes
- Allow hostname, instead of IP, to be supplied without `-h`

### Fixes
- Fix one-shot sync using `-s`, broke in previous release.


[2010_323][] - 2010-11-19
-------------------------

Curated by [Joachim Nilsson][].

### Changes
- Support for resync with server on `SIGHUP`
- Create `.md5` file as well during <kbd>make dist</kbd>


[2010_300][] - 2010-10-27
-------------------------

Curated by [Joachim Nilsson][].

### Changes
- Prefix all log messages with "ntpclient:"
- Merge the `adnyw.patch`
- Refactor logging to stdout/syslog using one `logit()` function
- Support for daemonizing ntpclient by default if run as root, assumes
  syslog output.
- Add mini-ntpclient, a *very* small [ntpclient from DD-WRT][dd-wrt].

### Fixes
- Fix `adjtimex()` compile error on uClibc, from OpenEmbedded


[2007_365][] - 2007-12-31
-------------------------

Curated by [Larry Doolittle][].

### Changes
- Adjustable `min_delay` parameter, used to be hard-coded 800 microseconds
- Remove useless `listen()` call, thanks to Alexander Indenbaum
- Tidy up 32-bit integer declarations, prodding from Brian McAllister
- Added `rate2.awk`, contributed by Lou Sortman
- Provide easy way to override 15 second `MIN_INTERVAL` at compile time
- Relax `MIN_INTERVAL` enforcement for one-shot use, thanks to Mihai Buha

### Fixes
- Fix length passed to `recvfrom()`, thanks to Alexander Indenbaum


[2006_318][] - 2006-11-14 [YANKED]
----------------------------------

Curated by [Larry Doolittle][].

### Changes
- Default build is now `-std=c99`, but `c89` sill works too
- Switch default compile from `gettimeofday()` to POSIX `clock_gettime()`
- More sanity checking on the NTP reply packet, reference [RFC 4330]
- Fractional second printing in debug output changed to traditional decimal
- New `-f` switch to set initial frequency
- Works to specify both `-s` and `-l`, will jump-set first and then phase lock
- New man page, contributed by Walter Harms
- Most subroutines are now flagged static to the compiler
- Structural changes to the code, such as the new `ntpclient.h` file
- Dropped (mostly) obsolete patches from Linksys and Andy Warner

### Fixes
- Bug fix for `select()` error handling, thanks to Walter Harms


[2003_194][] - 2003-07-13 [YANKED]
----------------------------------

Curated by [Larry Doolittle][].

- New `-g` option (has had limited testing)
- Changed max frequency adjustment from 91 ppm to 150 ppm
- Fixed "inconsistent" bug in `phaselock.c` triggered by large freq errors
- New files: `HOWTO`, `adjtimex.c`, `adjtimex.1`, `rate.awk`, `log2date.pl`
- Minor source code cleanups
- Source is now as 64-bit clean as practical; tested on Alpha
- Optional patches provided by Andy Warner, see `andyw.patch`
- Optional patches provided by Linksys, see linksys.patch
- Removed unreasonable 15020 day offset in date column ([xntpd] has this
  offset, which turns days-since-1900-epoch into Modified Julian Day)


[UNRELEASED]: https://github.com/troglobit/ntpclient/compare/2018_244...HEAD
[2018_244]:   https://github.com/troglobit/ntpclient/compare/2018_176...2018_244
[2018_176]:   https://github.com/troglobit/ntpclient/compare/2017_246...2018_176
[2017_246]:   https://github.com/troglobit/ntpclient/compare/2017_217...2017_246
[2017_217]:   https://github.com/troglobit/ntpclient/compare/2010_365...2017_217
[2015_365]:   https://github.com/troglobit/ntpclient/compare/2010_365...2015_365
[2010_365]:   https://github.com/troglobit/ntpclient/compare/2010_326...2010_365
[2010_326]:   https://github.com/troglobit/ntpclient/compare/2010_323...2010_326
[2010_323]:   https://github.com/troglobit/ntpclient/compare/2010_300...2010_323
[2010_300]:   https://github.com/troglobit/ntpclient/compare/2007_365...2010_300
[2007_365]:   https://github.com/troglobit/ntpclient/compare/2006_318...2007_365
[2006_318]:   https://github.com/troglobit/ntpclient/compare/2003_194...2006_318
[2003_194]:   https://github.com/troglobit/ntpclient/compare/2000_345...2003_194
[xntpd]: http://www.eecis.udel.edu/~mills/ntp/
[dd-wrt]: http://svn.dd-wrt.com/browser/src/router/ntpclient/
[RFC 4330]: http://tools.ietf.org/html/rfc4330
[Mike Frysinger]: vapier@gentoo.org
[CONTRIBIBUTING.md]: https://github.com/troglobit/ntpclient/CONTRIBIBUTING.md
[Joachim Nilsson]: https://github.com/troglobit/ntpclient/
[Larry Doolittle]: http://doolittle.icarus.com/ntpclient/
