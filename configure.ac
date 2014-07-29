AC_PREREQ([2.69])
AC_INIT([freeabode], [0.0.1], [luke+freeabode@dashjr.org])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_SRCDIR([nest.c])
AC_CONFIG_HEADERS([config.h])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC_C99
if test x$ac_cv_prog_cc_c99 = xno; then
	AC_MSG_ERROR([C99 support is required])
fi

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

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
