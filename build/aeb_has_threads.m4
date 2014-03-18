#dnl AEB_HAS_THREADS([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
AC_DEFUN([AEB_HAS_THREADS],
  [AC_MSG_CHECKING([whether the Apache Portable Runtime is configured for multi-threading])
   _aeb_save_CPPFLAGS="$CPPFLAGS"
   CPPFLAGS="$APR_CFLAGS"
   AC_PREPROC_IFELSE([AC_LANG_PROGRAM(
            [[#include <apr.h>]],
            [[#ifdef APR_HAS_THREADS
              const char *apr_has_threads = "true";
              #else
              #error no threads
              #endif]])],
      [_apr_has_threads=yes],
      [_apr_has_threads=no])
   AC_MSG_RESULT([$_apr_has_threads])
   CPPFLAGS="$_aeb_save_CPPFLAGS"
   AS_VAR_IF([_apr_has_threads],[yes],[$1],[$2])dnl
  ])