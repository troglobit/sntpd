sntpd
=====
[![License Badge][]][License] [![GitHub Status][]][GitHub] [![Coverity Status][]][Coverity Scan]

Table of Contents
-----------------

* [Introduction](#introduction)
* [Usage](#usage)
* [Troubleshooting](#troubleshooting)
* [Bugs](#bugs)
* [Compliance](#compliance)
* [Building](#building)
* [Building from GIT](#building-from-git)
* [Origin & References](#origin--references)


Introduction
------------

sntpd is a small SNTP server and client for UNIX systems, implementing
[RFC 1305][] and [RFC 4330][].  Its functionality is only a small subset
of [ntpd][], [chrony][], [OpenNTPd][], and [xntpd][].  Since it is much
smaller it is also more relevant for embedded systems in need of only a
background process to keep the system time in sync (client mode).

sntpd is a fork of ntpclient by Larry Doolittle.  As such it implements
a compatibility mode when called with the name `ntpclient`.  The name
has been changed to indicate the expanded feature set, e.g. a background
daemon mode, IPv6, syslog, as well as changes in command line options.

Use the GitHub [issue tracker][] to report bugs.  If you want to
contribute fixes or new features, see the file [CONTRIBUTING.md][].

> [!NOTE]
> sntpd has _limited_ support for acting as an SNTP server.
> The server mode is enabled by default, disable with `-p 0`.

Usage
-----

All arguments are optional, sntpd defaults to use `pool.ntp.org`.

```
Usage:
  sntpd [options] [SERVER ...]

  -d       Dry run, no time correction, useful for debugging
  -h       Show summary of command line options and exit
  -i SEC   Check time every interval seconds.  Default: 600
  -l LEVEL Set log level: none, err, warn, notice (default), info, debug
  -m SEC   Minimum poll interval.  Default: 15, the floor RFC 4330
           requires.  Lower only on a network with its own time source
  -n       Don't fork.  Implies logging to stdout instead of syslog
           Add '-s' to use syslog anyway, e.g. Finit + systemd
  -p PORT  SNTP server mode port, default: 123, use 0 to disable
  -q USEC  Minimum packet delay for transaction, default: 800 usec
  -s       Use syslog instead of stdout, default unless -n
  -t       Trust network and server, disable RFC4330 validation
  -v       Show program version

  SERVER   NTP server, host[:port][,iburst][,prefer].  Repeatable,
           at most 8.  Bracket IPv6 literals: [2001:db8::1]:123
           Used as a failover list.  Default: pool.ntp.org

  Server options:
  iburst   Burst 8 packets 2s apart while unreachable, for a
           faster first sync and faster failover.  Default: off
  prefer   Return to this server once it answers again
```

Several servers may be given.  sntpd uses one at a time and treats the
list as a failover order, moving on when a server stops answering.  It
does not run the selection and combining algorithms of RFC 5905, so
extra servers buy resilience rather than accuracy.

    sntpd 192.168.1.1,prefer,iburst pool.ntp.org

Here the local server is tried first and returned to whenever it comes
back, with `iburst` making both the first sync and the failover take
seconds instead of minutes.  `pool.ntp.org` covers the gap.


Compatibility
-------------

The sntpd project comes with a compatiblity mode triggered when called
as `ntpclient`.  This mode supports the original command line options.

Mortal users can use the `ntpclient` tool for monitoring, but not clock
setting (with the `-s` or `-l` switches).  The `-l` switch is designed
to be robust in any network environment, but has seen the most extensive
testing in a low latency (less than 2 ms) Ethernet environment.  Users
in other environments should study sntpd's behavior, and be prepared to
adjust internal tuning parameters.  A long description of how and why to
use sntpd and ntpclient is in the [HowTo.md][] file.  sntpd always sends
packets to the server's UDP port 123.

One commonly needed tuning parameter for lock mode is `min_delay`, the
shortest possible round-trip transaction time.  This can be set with the
command line `-q` switch.  The historical default of 800 microseconds
was good for local Ethernet hardware a few years ago.  If it is set too
high, you will get a lot of "inconsistent" lines in the log file when
time locking (`-l` switch).  The only true future-proof value is 0, but
that will cause the local time to wander more than it should.  Setting
it to 200 is recommended on an end client.

The `test.dat` file that is part of the source distribution has 200
lines of sample output.  Its first few lines, with the output column
headers that are shown when the `-d` option is chosen, are:

```
  day    second   elapsed    stall      skew  dispersion  freq
36765 00180.386    1398.0     40.3  953773.9       793.5  -1240000
36765 00780.382    1358.0     41.3  954329.0       915.5  -1240000
36765 01380.381    1439.0     56.0  954871.3       915.5  -1240000
```

* day, second: time of measurement, UTC, relative to NTP epoch (Jan 1, 1900)
* elapsed:     total time from query to response (microseconds)
* stall:       time the server reports that it sat on the request (microseconds)
* skew:        difference between local time and server time (microseconds)
* dispersion:  reported by server, see [RFC 1305][] (microseconds)
* freq:        local clock frequency adjustment (Linux only, ppm*65536)

ntclient performs a series of sanity checks on UDP packets received, as
recommended by [RFC 4330][].  If it fails one of these tests, the line
described above is replaced by `36765 01380.381 rejected packet` or, if
`--enable-debug` was selected at `configure`, one of:

```
36765 01380.381  rejected packet: LI==3
36765 01380.381  rejected packet: VN<3
36765 01380.381  rejected packet: MODE!=3
36765 01380.381  rejected packet: ORG!=sent
36765 01380.381  rejected packet: XMT==0
36765 01380.381  rejected packet: abs(DELAY)>65536
36765 01380.381  rejected packet: abs(DISP)>65536
36765 01380.381  rejected packet: KoD
```

To see the actual values of the rejected packet, start the ntpclient
tool or sntpd with the `-d` option; this will give a human-readable
printout of every packet received, including the rejected ones.  The
`-t` switch skips all of these except the last: a Kiss-o'-Death, [RFC
4330][] section 8, is the server's own request rather than one of our
checks, so `-t` still honours it and still retires that server for the
rest of the run.

The file `test.dat` is suitable for piping into <kbd>ntpclient -r</kbd>.
There are more than 200000 samples (lines) archived for study.  They are
generally spaced 10 minutes apart, representing over three years of data
logging (from a variety of machines, and not continuous, unfortunately).
If you are interested, [contact Larry][].

Also included is a version of the `adjtimex(1)` tool.  See its man page
and the [HowTo.md][] file for more information.

Another tool is `envelope`, which is a perl script that was used for the
lock studies.  It's kind of a hack and not worth documenting here.


Troubleshooting
---------------

Some really old Linux systems (e.g., Red Hat EL-3.0 and Ubuntu 4.10)
have a totally broken POSIX `clock_settime()` implementation.  If you
get the following with <kbd>sntpd -s</kbd>:

```
clock_settime: Invalid argument
```

then `configure --enable-obsolete`.  Linux systems that are even older
will not even compile without that switch set.


Bugs
----

* Doesn't understand the LI (Leap second Indicator) field of an NTP packet
* Doesn't interact with `adjtimex(2)` status value
* No simultaneous querying or combining of multiple servers, only the
  failover list described under Usage above
* Requires Linux `select()` semantics, where timeout value is modified


Compliance
----------

Adherence to [RFC 4330][] chapter 10, Best practices:

1. Enforced, unless someone tinkers with the source code
2. Enforced: a full lap through the failover list without an answer
   doubles the backoff, floored at `-m` (15s by default)
3. Not in scope for the upstream source
4. Defaults to pool.ntp.org, but is configurable
5. Not in scope for the upstream source
6. Supported
7. Supported, connection to server reopened once a day
8. Honoured: `RATE` widens a server's retry backoff, `DENY`/`RSTR`
   retire it for the run.  A forged Kiss-o'-Death, the DoS risk raised
   here originally, cannot retire a server without echoing the
   timestamp we sent it; see `docs/TODO.md` for why that guard is not
   yet as strong as it should be


Building
--------

Please use released, and versioned, tarballs of this project.  All
releases are available from here:

☞ https://github.com/troglobit/sntpd/releases

sntpd uses the [GNU configure & build system][buildsystem]:

```sh
    ./configure
    make
```

The GNU build system use `/usr/local` as the default install prefix.  In
many cases this is useful, but many users expect `/usr` or `/opt`.  To
install into `/usr/sbin/sntpd` and `/usr/bin/adjtimex`:

```sh
    ./configure --prefix=/usr
    make
    sudo make install-strip
```

The last command installs, there is also a possiblity to uninstall all
files using:

```sh
    sudo make uninstall
```

For changing the system clock frequency, only the Linux `adjtimex(2)`
interface is implemented at this time.  Non-Linux systems can only use
the ntpclient tool to measure time differences and set the system clock,
by way of the POSIX 1003.1-2001 standard, the routines `clock_gettime()`
and `clock_settime()`.  Also, see section [Bugs](#bugs), below.

There are a few compile-time configurations possible.  E.g., for older
Linux kernels, before the tickless erea (pre 3.0), you want to:

```sh
    ./configure --disable-siocgstamp
```

However, first try without changing the default.  That gives you a full-
featured `sntpd` and `ntpclient` tool that use a modern POSIX time API
and works reasonably well with any Linux kernel.

Solaris and other UNIX users may need to adjust the `CFLAGS` slightly.
For other options, see <kbd>./configure --help</kbd>

To run the test suite:

```sh
    make check -j
```

Each integration test gets a network namespace of its own, so it can bind
UDP port 123 without root and without the tests tripping over each other.
That needs unprivileged user namespaces, which Debian and Ubuntu disable:

```sh
    sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0
```

Without it the tests report SKIP rather than FAIL and `make check` still
succeeds, so look for `# SKIP: 0` in the summary before trusting a green
run.  Details of any failure land in `test/test-suite.log`.

To build Debian packages, into the parent directory:

```sh
    sudo apt install devscripts debhelper lintian systemd-dev
    ./configure
    make package
```


Building from GIT
-----------------

If you want to contribute, or try out the latest unreleased features,
here is a few things to know about [GNU build system][buildsystem]:

- `configure.ac` and a per-directory `Makefile.am` are key files
- `configure` and `Makefile.in` are generated from `autogen.sh`,
  they are not stored in GIT but automatically generated for the
  release tarballs
- `Makefile` is generated by `configure` script

To build from GIT you first need to clone the repository and run the
`autogen.sh` script.  This requires `automake` and `autoconf` to be
installed on your system.

```sh
    git clone https://github.com/troglobit/sntpd.git
    cd sntpd/
    ./autogen.sh
    ./configure && make
```

**Remember:** GIT sources are a moving target and not recommended for
              production systems, unless you know what you are doing!


Origin & References
-------------------

[Larry Doolittle][] created ntpclient and made it freely available under
the terms of the GNU General Public [License][], version 2.  He remains
the official upstream for `ntpclient`.

This sntpd fork at GitHub is maintained by [Joachim Wiberg][] and adds
features like syslog, background daemon, IPv6, systemd support, a
limited SNTP server mode, and a list of servers to fail over between.

[ntpd]:            http://www.ntp.org
[xntpd]:           http://www.eecis.udel.edu/~mills/ntp/
[chrony]:          http://chrony.tuxfamily.org/
[OpenNTPd]:        http://www.openntpd.org
[RFC 1305]:        http://tools.ietf.org/html/rfc1305
[RFC 4330]:        http://tools.ietf.org/html/rfc4330
[Larry Doolittle]: http://doolittle.icarus.com/ntpclient/
[contact Larry]:   mailto:larry@doolittle.boa.org
[buildsystem]:     https://airs.com/ian/configure/
[License]:         http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
[License Badge]:   https://img.shields.io/badge/License-GPL%20v2-blue.svg
[GitHub]:          https://github.com/troglobit/sntpd/actions/workflows/build.yml/
[GitHub Status]:   https://github.com/troglobit/sntpd/actions/workflows/build.yml/badge.svg
[CONTRIBUTING.md]: docs/CONTRIBUTING.md
[issue tracker]:   https://github.com/troglobit/sntpd/issues
[HowTo.md]:        docs/HOWTO.md
[Joachim Wiberg]:  http://troglobit.com
[Coverity Scan]:   https://scan.coverity.com/projects/20498
[Coverity Status]: https://scan.coverity.com/projects/20498/badge.svg
