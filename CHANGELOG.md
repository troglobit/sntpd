Change Log
==========

All notable changes to the project are documented in this file.


[2010_365] - 2010-12-31
-----------------------

Released by [Larry Doolittle].  Changes since `ntpclient_2007_365.tar.gz`.

### Changes
* Include `netdb.h` and always define `_BSD_SOURCE` to get `herror()`
* Minor formatting to align with Nilsson's fork
* add `-fno-strict-aliasing` as needed by traditional network coding style

### Fixes
* Fixed type of `sa_xmit_len`, thanks [Mike Frysinger]
* Dropped underscores in spelling of `adjtimex(2)`, might make uClibc happier


[2010_300] - 2010-10-27
-----------------------

Released by [Joachim Nilsson].

### Changes
* Prefix all log messages with "ntpclient:"
* Merge the `adnyw.patch`
* Refactor logging to stdout/syslog using one `logit()` function
* Support for daemonizing ntpclient by default if run as root, assumes
  syslog output.

### Fixes
* Fix `adjtimex()` compile error on uClibc, from OpenEmbedded


[2007_365] - 2007-12-31
-----------------------

Released by [Larry Doolittle].

### Changes
* Adjustable `min_delay` parameter, used to be hard-coded 800 microseconds
* Remove useless `listen()` call, thanks to Alexander Indenbaum
* Tidy up 32-bit integer declarations, prodding from Brian McAllister
* Added `rate2.awk`, contributed by Lou Sortman
* Provide easy way to override 15 second `MIN_INTERVAL` at compile time
* Relax `MIN_INTERVAL` enforcement for one-shot use, thanks to Mihai Buha

### Fixes
* Fix length passed to `recvfrom()`, thanks to Alexander Indenbaum


[2006_318] - 2006-11-14
-----------------------

Released by [Larry Doolittle].

### Changes
* Default build is now `-std=c99`, but `c89` sill works too
* Switch default compile from `gettimeofday()` to POSIX `clock_gettime()`
* More sanity checking on the NTP reply packet, reference [RFC 4330]
* Fractional second printing in debug output changed to traditional decimal
* New `-f` switch to set initial frequency
* Works to specify both `-s` and `-l`, will jump-set first and then phase lock
* New man page, contributed by Walter Harms
* Most subroutines are now flagged static to the compiler
* Structural changes to the code, such as the new `ntpclient.h` file
* Dropped (mostly) obsolete patches from Linksys and Andy Warner

### Fixes
* Bug fix for `select()` error handling, thanks to Walter Harms


[2003_194] - 2003-07-13
-----------------------

Released by [Larry Doolittle].

* New `-g` option (has had limited testing)
* Changed max frequency adjustment from 91 ppm to 150 ppm
* Fixed "inconsistent" bug in `phaselock.c` triggered by large freq errors
* New files: `HOWTO`, `adjtimex.c`, `adjtimex.1`, `rate.awk`, `log2date.pl`
* Minor source code cleanups
* Source is now as 64-bit clean as practical; tested on Alpha
* Optional patches provided by Andy Warner, see `andyw.patch`
* Optional patches provided by Linksys, see linksys.patch
* Removed unreasonable 15020 day offset in date column (xntpd[1] has this
  offset, which turns days-since-1900-epoch into Modified Julian Day)


[UNRELEASED]: https://github.com/troglobit/ntpclient/compare/2010_365...HEAD
[2010_365]: https://github.com/troglobit/ntpclient/compare/2010_300...2010_365
[2010_300]: https://github.com/troglobit/ntpclient/compare/2007_365...2010_300
[2007_365]: https://github.com/troglobit/ntpclient/compare/2006_318...2007_365
[2006_318]: https://github.com/troglobit/ntpclient/compare/2003_194...2006_318
[2003_194]: https://github.com/troglobit/ntpclient/compare/2000_345...2003_194
[1]: http://www.eecis.udel.edu/~mills/ntp/
[Mike Frysinger]: vapier@gentoo.org
[Joachim Nilsson]: https://github.com/troglobit/ntpclient/
[Larry Doolittle]: http://doolittle.icarus.com/ntpclient/

