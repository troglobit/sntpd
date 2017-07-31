# Larry's versioning scheme: YYYY_DOY, i.e. `date +"%Y_%j"`
AC_PREREQ(2.61)
AC_INIT(ntpclient, 2017_PRE, https://github.com/troglobit/ntpclient/issues,, https://github.com/troglobit/ntpclient)
AM_INIT_AUTOMAKE([1.11 foreign dist-xz])
AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([src/ntpclient.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile src/Makefile doc/Makefile man/Makefile])

# Define necessary build flags
AC_GNU_SOURCE
AC_USE_SYSTEM_EXTENSIONS

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_GCC_TRADITIONAL
AC_PROG_INSTALL

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

AS_IF([test "x$ac_enable_siocgstamp" = "xyes"],
	AC_DEFINE([PRECISION_SIOCGSTAMP], [], [Enable precision SIOCGSTAMP, for all modern kernels]))

# Generate all files
AC_OUTPUT
