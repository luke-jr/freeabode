AC_PREREQ([2.69])
AC_INIT([freeabode], [0.0.1], [luke+freeabode@dashjr.org])
AC_CONFIG_AUX_DIR([ac-aux])
AC_CONFIG_MACRO_DIR([ac-aux])
AC_CONFIG_SRCDIR([libfreeabode/libfreeabode.pc.in])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_HEADERS([config.h])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC_C99
if test x$ac_cv_prog_cc_c99 = xno; then
	AC_MSG_ERROR([C99 support is required])
fi
AC_PROG_LN_S
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
LT_INIT([disable-static])

# http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
AC_SUBST([LIBFREEABODE_SO_VERSION], [0:0:0])

AC_CHECK_HEADERS([fcntl.h stddef.h stdint.h stdlib.h string.h sys/time.h unistd.h])

AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

AC_CHECK_LIB([rt],[clock_gettime])
AC_CHECK_FUNCS([clock_gettime gettimeofday memmove])

PKG_CHECK_MODULES([PROTOBUF_C], [libprotobuf-c >= 1.0.0])
AC_ARG_WITH([protoc-c],
	[AS_HELP_STRING([--with-protoc-c=/path/of/protoc-c],
		[Location of the protocol buffers compiler protoc-c. Defaults to looking on path.])],
	[PROTOC_C="$withval"],
	[ AS_IF([test "x${PROTOC_C}" == "x"],
		[AC_PATH_PROG([PROTOC_C], [protoc-c], [no])])
	]
)
AS_IF([test "${PROTOC_C}" == "no"], [AC_MSG_ERROR([ProtoBuf compiler "protoc-c" not found.])])

have_directfb=true
AC_ARG_WITH([directfb],
	[AS_HELP_STRING([--without-directfb],[Build without DirectFB support (default enabled)])],
	[
		if test x$withval = xyes; then
			PKG_CHECK_MODULES([DIRECTFB], [directfb])
		else
			have_directfb=false
		fi
	],
	[
		PKG_CHECK_MODULES([DIRECTFB], [directfb],[],[
			have_directfb=false
		])
	]
)
AM_CONDITIONAL([HAVE_DIRECTFB], [$have_directfb])

PKG_CHECK_MODULES([JANSSON], [jansson])
AM_CONDITIONAL([HAVE_JANSSON], [true])

PKG_CHECK_MODULES([LIBSODIUM], [libsodium])
PKG_CHECK_MODULES([LIBZMQ], [libzmq])

AC_SUBST([FREEABODE_CFLAGS],['-I$(top_srcdir) -I$(top_builddir)'])
AC_SUBST([FREEABODE_LIBS],['../libfreeabode/libfreeabode.la'])

AC_CONFIG_FILES([
	Makefile
	fabd-cli/Makefile
	htu21d/Makefile
	libfreeabode/Makefile
	libfreeabode/libfreeabode.pc:libfreeabode/libfreeabode.pc.in
	nbp/Makefile
	tstat/Makefile
	wallknob/Makefile
])
AC_OUTPUT
