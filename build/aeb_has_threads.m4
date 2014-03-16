AC_DEFUN([_AEB_HAS_THREADS],
  [if test x"$_aeb_has_threads_checked" = x""; then
    _aeb_has_threads_checked=yes
    _aeb_save_CPPFLAGS="$CPPFLAGS"
    AS_IF([test x"$APR_CFLAGS" != x""],
      [CPPFLAGS="$APR_CFLAGS"])
    AC_PREPROC_IFELSE([AC_LANG_PROGRAM(
            [[#include <apr.h>]],
            [[#ifdef APR_HAS_THREADS
              const char *apr_has_threads = "true";
              #else
              #error no threads
              #endif]])],
    [_apr_has_threads=yes],[_apr_has_threads=no])
    ac_cv_aeb_has_threads="${ac_cv_aeb_has_threads:-$_apr_has_threads}"
    CPPFLAGS="$_aeb_save_CPPFLAGS"
  fi
  ])dnl

# AEB_HAS_THREADS([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
AC_DEFUN([AEB_HAS_THREADS],
  [AC_CACHE_CHECK([whether the Apache Portable Runtime support threading],
      [ac_cv_aeb_has_threads],_AEB_HAS_THREADS)
    AS_VAR_IF(["$ac_cv_aeb_has_threads"],["yes"],[$2],[$1])dnl
  ])