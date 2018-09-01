# Use `make verison` to get Larry's version number (YYY_DDD) for today
AC_PREREQ(2.61)
AC_INIT(ntpclient, 2018_244, https://github.com/troglobit/ntpclient/issues,, https://github.com/troglobit/ntpclient)
AM_INIT_AUTOMAKE([1.11 foreign dist-xz])
AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([src/ntpclient.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile src/Makefile docs/Makefile man/Makefile ntpclient.service])

# Define necessary build flags
AC_GNU_SOURCE
AC_USE_SYSTEM_EXTENSIONS

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_GCC_TRADITIONAL
AC_PROG_INSTALL

# Required to check for libsystemd-dev
PKG_PROG_PKG_CONFIG

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h netinet/in.h sys/ioctl.h sys/socket.h syslog.h],
                  [], [],
		  [
		  #ifdef HAVE_SYS_SOCKET_H
		  #include <sys/socket.h>
		  #endif
])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T

AC_ARG_ENABLE(siocgstamp,
        [AS_HELP_STRING([--disable-siocgstamp], [Disable precision SIOCGSTAMP, for Linux pre 3.0])],
        [ac_enable_siocgstamp="$enableval"],
        [ac_enable_siocgstamp="yes"])

AC_ARG_ENABLE(syslog,
        [AS_HELP_STRING([--disable-syslog], [Disable syslog support, log to stderr instead])],
        [ac_enable_syslog="$enableval"],
        [ac_enable_syslog="yes"])

AC_ARG_ENABLE(obsolete,
        [AS_HELP_STRING([--enable-obsolete], [Use obsolete gettimeofday(), really old systems])],
        [ac_enable_obsolete="$enableval"],
        [ac_enable_obsolete="no"])

AC_ARG_ENABLE(debug,
        [AS_HELP_STRING([--enable-debug], [Enable debug of NTP protocol])],
        [ac_enable_debug="$enableval"],
        [ac_enable_debug="no"])

AC_ARG_ENABLE(replay,
        [AS_HELP_STRING([--enable-replay], [Enable analysis code path, developer mode])],
        [ac_enable_replay="$enableval"],
        [ac_enable_replay="no"])

AC_ARG_ENABLE(mini,
        [AS_HELP_STRING([--enable-mini], [Really small ntpclient, from OpenWRT project])],
        [ac_enable_mini="$enableval"],
        [ac_enable_mini="no"])

AC_ARG_WITH([systemd],
     [AS_HELP_STRING([--with-systemd=DIR], [Directory for systemd service files])],,
     [with_systemd=auto])

AS_IF([test "x$ac_enable_siocgstamp" = "xyes"],
	AC_DEFINE([PRECISION_SIOCGSTAMP], [], [Enable precision SIOCGSTAMP, for all modern kernels]))

AS_IF([test "x$ac_enable_obsolete" = "xyes"],
	AC_DEFINE([USE_OBSOLETE_GETTIMEOFDAY], [], [For RHEL 3.0, Ubuntu 4.10 and earlier]))

AS_IF([test "x$ac_enable_debug" = "xyes"],
	AC_DEFINE([ENABLE_DEBUG], [], [Debug NTP protocol data]))

AS_IF([test "x$ac_enable_syslog" = "xyes"],
	AC_DEFINE([ENABLE_SYSLOG], [], [Debug NTP protocol data]))

AS_IF([test "x$ac_enable_replay" = "xyes"],
	AC_DEFINE([ENABLE_REPLAY], [], [Support for replay analysis from stdin]))

AS_IF([test "x$with_systemd" = "xyes" -o "x$with_systemd" = "xauto"], [
     def_systemd=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)
     AS_IF([test "x$def_systemd" = "x"],
         [AS_IF([test "x$with_systemd" = "xyes"],
	     [AC_MSG_ERROR([systemd support requested but pkg-config unable to query systemd package])])
	     with_systemd=no], [with_systemd="$def_systemd"])]
)
AS_IF([test "x$with_systemd" != "xno"],
     [AC_SUBST([systemddir], [$with_systemd])])
AM_CONDITIONAL(SYSTEMD, [test "x$with_systemd"   != "xno"])
AM_CONDITIONAL(MINI,    [test "x$ac_enable_mini" != "xno"])

# Expand $sbindir early, into $SBINDIR, for systemd unit file
# NOTE: This does *not* take prefix/exec_prefix override at "make
#       install" into account, unfortunately.
test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
DOCDIR=`eval echo $docdir`
DOCDIR=`eval echo $DOCDIR`
AC_SUBST(DOCDIR)
SBINDIR=`eval echo $sbindir`
SBINDIR=`eval echo $SBINDIR`
AC_SUBST(SBINDIR)

# Generate all files
AC_OUTPUT
