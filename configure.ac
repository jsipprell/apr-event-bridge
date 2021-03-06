AC_PREREQ([2.63])
AC_INIT(apr-event-bridge,0.0.1)
AC_COPYRIGHT([[Copyright 2014, Jesse Sipprell.
All rights reserved.]])
builtin(include, build/m4/aeb_args.m4)
builtin(include, build/m4/compat.m4)
AC_CONFIG_AUX_DIR(build)
AC_CONFIG_MACRO_DIR(build)
AC_CONFIG_SRCDIR(src)
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE([subdir-objects])
AM_MAINTAINER_MODE([enable])
AM_SILENT_RULES([yes])

LIBAEB_EXTRA_CPPFLAGS=
LIBAEB_EXTRA_CFLAGS=
LIBAEB_EXTRA_LIBS=
AEB_USER_ASSERTED_CFLAGS="$CFLAGS"

AC_PROG_CC
LT_INIT([disable-static])
AC_PROG_INSTALL
AC_LANG_C
AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG

AC_CONFIG_LIBOBJ_DIR([lib])

AC_ARG_VAR(APR_LIBS,[Apache Portable Runtime libraries])
AC_ARG_VAR(APR_UTIL_LIBS,[Apache Portable Runtime Utility libaries])
AC_ARG_VAR(APR_CFLAGS,[Apache Portable Runtime CFLAGS])
AC_ARG_VAR(DEBUG_CFLAGS,[additional debugging CFLAGS])
AC_ARG_VAR(PLATFORM_CFLAGS,[CFLAGS specific to the target platform])

LIBAEB_ABI_COMPAT=0
LIBAEB_ABI_MAJOR=0
LIBAEB_API_VERSION=0
LIBAEB_REVISION=1

dnl -fvisibility=hidden # for darwin
AS_IF([test x"$PLATFORM_CFLAGS" = x""],
  [AS_CASE($target_os,
    [darwin*],[PLATFORM_CFLAGS=-fvisibility=hidden],
    [])])


EXTRA_CPPFLAGS="-DHAVE_LIBAEB_CONFIG_H"
LIBAEB_LINK=
LIBAEB_LIBS=
LIBAEB_INCLUDE=

LIBAEB_VERSION="\"${LIBAEB_API_VERSION}.${LIBAEB_REVISION}\""

libaeb_cf_debugging_macro=no

AEB_ARG_ENABLE(debug,
  AS_HELP_STRING([--enable-debug@<:@=MODE@:>@],
    [Enable runtime debugging]),
    [unset],[yes no gdb])

AEB_ARG_ENABLE(install-pc,
  AS_HELP_STRING([--enable-install-pc],
    [Install apr_event_bridge.pc file for use with pkg-config]),
    [yes],[yes no])

AEB_ARG_ENABLE(threads,
  [AS_HELP_STRING([--enable-threads@<:@=OPTION@:>@],
    [Enable thread support])],[auto],[yes no auto])

AEB_ARG_WITH(libevent,
  [AS_HELP_STRING([--with-libevent@<:@=sys@:>@],
    [Build included libevent (sys uses system libevent)])],[sys],[yes no sys],
    [AS_CASE($aeb_with_libevent,
        [yes],,
        [no],[AC_MSG_ERROR([Libevent is required, please use --with-libevent to build bundled version])],
        [sys],,
        [aeb_with_libevent=yes])
    ])

AS_VAR_IF([aeb_with_libevent],[sys],[aeb_with_libevent=no])

AM_CONDITIONAL([BUILD_INCLUDED_LIBEVENT],[test x"$aeb_with_libevent" = x"yes"])
AM_CONDITIONAL([INSTALL_PKGCONFIG],[test x"$aeb_enable_install_pc" = x"yes" ])

AS_IF([test x"$aeb_enable_debug" = x"unset"],
  [],
  [enable_shared=no
   enable_static=yes])

AS_CASE($aeb_enable_debug,
  [gdb],[AS_VAR_IF([AEB_USER_ASSERTED_CFLAGS],[""],
            [CFLAGS="-ggdb3"],[AX_APPEND_FLAG([-ggdb3],[CFLAGS])])
         AEB_IF_AUTOCONF_VERSION([2.69],[AX_CFLAGS_WARN_ALL])
         libaeb_cf_debugging_macro=yes],
  [yes],[AS_VAR_IF([AEB_USER_ASSERTED_CFLAGS],[""],
            [CFLAGS="-g"],[AX_APPEND_FLAG([-g],[CFLAGS])])
         AEB_IF_AUTOCONF_VERSION([2.69],[AX_CFLAGS_WARN_ALL])
         libaeb_cf_debugging_macro=yes],
  [no],[],
   [AEB_IF_AUTOCONF_VERSION([2.69],[AX_CFLAGS_WARN_ALL])
   libaeb_cf_debugging_macro=yes])

AS_VAR_IF([aeb_with_libevent],[yes],[
  ACX_PTHREAD([
        AC_DEFINE(HAVE_PTHREADS, 1,
                [Define if we have pthreads on this system])
        have_pthreads=yes])
  CFLAGS="$CFLAGS${CFLAGS:+ }$PTHREAD_CFLAGS"
  AC_CHECK_SIZEOF(pthread_t, ,
     [AC_INCLUDES_DEFAULT()
      #include <pthread.h>])
])

AS_VAR_IF([PLATFORM_CFLAGS],[""],[],
  [CFLAGS="$CFLAGS${CFLAGS:+ }$PLATFORM_CFLAGS"])

AS_IF([test x"$libaeb_cf_debugging_macro" = x"yes"],
  [AC_DEFINE([DEBUGGING],[1],[Define to enable extra debugging])])

AEB_CHECK_GCC_VISIBILITY
AEB_CHECK_ALIAS_ATTRIBUTE

dnl we don't use pkg-config for this because it's difficult to precisely control
dnl linkage of additional libs beyond apr/apr-util (ldap, etc) with pkg-config.
APR_FIND_APR([],[],[1],[1])
AS_IF([test x"$apr_found" != x"yes"],
  [AC_MSG_ERROR([cannot find libapr])],
  [libaeb_apr_version="`$apr_config --version`"])

APR_FIND_APU([],[],[1],[1])
AS_IF([test x"$apu_found" != x"yes"],
  [AC_MSG_ERROR([cannot find libapr-util])],
  [libaeb_apu_version="`$apu_config --version`"
   libaeb_apu_minor="`$apu_config --version | sed 's/^.\+\.\(.\+\)\..\+$/\1/'`"])

LIBAEB_LINK="`$apr_config --ldflags --link-ld --libs`"
LIBAEB_LINK="$LIBAEB_LINK${LIBAEB_LINK:+ }`$apu_config --ldflags --link-ld`"
LIBAEB_INCLUDE="-I`$apr_config --includedir`"
AS_IF([test x"$APR_CFLAGS" = x""],
  [APR_CFLAGS="`$apr_config --cppflags --cflags` $LIBAEB_INCLUDE"])
AS_IF([test x"$APR_LIBS" = x""],
  [APR_LIBS="`$apr_config --ldflags --link-ld --libs`"])
AS_IF([test x"$APR_UTIL_LIBS" = x""],
  [APR_UTIL_LIBS="`$apu_config --ldflags --link-ld`"])

dnl libapr and libaprutil
AS_IF([test x"$APR_CFLAGS" != x""],
  [CFLAGS="$CFLAGS${CFLAGS:+ }$APR_CFLAGS"],[])
AS_IF([test x"$APR_LIBS" != x""],
  [LIBAEB_LIBS="$LIBAEB_LIBS${LIBAEB_LIBS:+ }$APR_LIBS"],[])
AS_IF([test x"$APR_UTIL_LIBS" != x""],
  [LIBAEB_LIBS="$LIBAEB_LIBS${LIBAEB_LIBS:+ }$APR_UTIL_LIBS"],[])

AS_IF([test x"$aeb_enable_threads" != x"no"],
  [AEB_HAS_THREADS([],
    [AS_VAR_IF([aeb_enable_threads],[auto],
                [aeb_enable_threads=no
                 AC_MSG_WARN([Thread support disabled -- APR support not detected])],
                [AC_MSG_ERROR([Apache Portable Runtime not configured for thread support!])])
     ])dnl
])

AS_VAR_IF([aeb_with_libevent],[no],[
  AS_VAR_IF([aeb_enable_threads],[no],
    [LIBEVENT_PKGCONFIG_NAME=libevent
     PKG_CHECK_MODULES([LIBEVENT],[libevent >= 2.0.0])],
    [PKG_CHECK_MODULES([LIBEVENT],[libevent_pthreads >= 2.0.0],
      [LIBEVENT_PKGCONFIG_NAME=libevent_pthreads],
      [AS_VAR_IF([aeb_enable_threads],[yes],
        [AC_MSG_WARN([libevent thread support unavailable via pkg-config, ignoring, but may cause issues later...])
         LIBEVENT_PKGCONFIG_NAME=libevent
         AC_MSG_NOTICE([Switching pkg-config to non-thread detection mode for libevent])
         PKG_CHECK_MODULES([LIBEVENT],[libevent >= 2.0.0])],
        [AC_MSG_WARN([libevent thread support unavailable via pkg-config, please use --enable-threads to force building w/ thread support])
         aeb_enable_threads=no
         LIBEVENT_PKGCONFIG_NAME=libevent
         AC_MSG_NOTICE([Switching pkg-config to non-thread detection mode for libevent])
         PKG_CHECK_MODULES([LIBEVENT],[libevent >= 2.0.0])])
      ])
    ])
  PKG_CHECK_VAR([LIBEVENT_INCDIR],[$LIBEVENT_PKGCONFIG_NAME],[includedir],
                  [LIBEVENT_CPPFLAGS="-I$LIBEVENT_INCDIR"])

  AS_IF([test x"$LIBEVENT_CFLAGS" != x""],
    [CFLAGS="$CFLAGS${CFLAGS:+ }$LIBEVENT_CFLAGS"
     LIBAEB_INCLUDE="$LIBAEB_INCLUDE${LIBAEB_INCLUDE:+ }$LIBEVENT_CFLAGS"],[])
  AS_IF([test x"$LIBEVENT_PTHREADS_CFLAGS" != x""],
    [CFLAGS="$CFLAGS${CFLAGS:+ }$LIBEVENT_PTHREADS_CFLAGS"
     LIBAEB_INCLUDE="$LIBAEB_INCLUDE${LIBAEB_INCLUDE:+ }$LIBEVENT_PTHREADS_CFLAGS"],[])

  AS_IF([test x"$LIBEVENT_LIBS" != x""],
    [LIBAEB_LIBS="$LIBAEB_LIBS${LIBAEB_LIBS:+ }$LIBEVENT_LIBS"
     LIBAEB_LINK="$LIBAEB_LINK${LIBAEB_LINK:+ }$LIBEVENT_LIBS"],[])
  AS_IF([test x"$LIBEVENT_PTHREADS_LIBS" != x""],
    [LIBAEB_LIBS="$LIBAEB_LIBS${LIBAEB_LIBS:+ }$LIBEVENT_PTHREADS_LIBS"
     LIBAEB_LINK="$LIBAEB_LINK${LIBAEB_LINK:+ }$LIBEVENT_PTHREADS_LIBS"],[])

],[LIBEVENT_PKGCONFIG_NAME=])

dnl Start standard autoconf stuff
AC_STDC_HEADERS
AC_HEADER_ASSERT
AC_CHECK_HEADERS([stdbool.h signal.h assert.h])
AC_FUNC_ALLOCA
AC_FUNC_STRTOLD
AC_CHECK_FUNCS_ONCE([strtod])

AX_SAVE_FLAGS([aeb])
LIBS="$LIBS${LIBS:+ }$LIBAEB_LIBS"

AS_VAR_IF([aeb_with_libevent],[no],[
  AC_CHECK_FUNCS([event_set_mem_functions],[],
    [AC_MSG_FAILURE([Your libevent is too old or was configured with --disable-malloc-replacement])])
])

AS_IF([test x"$ac_cv_func_alloca_works" != x"yes"],
  [AC_MSG_ERROR([this system does not support alloca(), yet a compatible implementation is not included with this distribution])])

AC_REPLACE_FUNCS([apr_table_clone apr_array_clear])
AX_RESTORE_FLAGS([aeb])

AS_IF([test x"$aeb_with_libevent" != x"yes"],[
  AS_IF([test x"$aeb_enable_threads" != x"no"],
    [AEB_EVENT_CHECK_THREADS(
      [AC_DEFINE_UNQUOTED([PLATFORM_THREAD_TYPE],[["]$libevent_thread_type["]],
          [Define to the type of thread supported on your platform (pthreads, windows, etc)])
      aeb_enable_threads=yes],
      [AS_VAR_IF([aeb_enable_threads],[auto],
        [aeb_enable_threads=no
        AC_MSG_WARN([Thread support disabled -- libevent support not detected])],
        [AC_MSG_FAILURE([Your libevent is too old or not configured for thread support!])]
      )])
    ])
  ],[test x"$aeb_enable_threads" != x"no"],
    [AS_VAR_IF([PTHREAD_CFLAGS],[""],[],
      [AC_DEFINE_UNQUOTED([PLATFORM_THREAD_TYPE],[pthreads])])
     AS_VAR_IF([aeb_enable_threads],[auto],[aeb_enable_threads=yes])
    ])

AC_RUN_IFELSE(
  [AC_LANG_PROGRAM([AC_INCLUDES_DEFAULT()
                    [#include <apr_version.h>
                     #include <stdio.h>]],
                   [[FILE *fp = fopen("conftest.out","w");
                     fprintf(fp,"%u\n%u\n%u",APR_MAJOR_VERSION,
                                         APR_MINOR_VERSION,
                                         APR_PATCH_VERSION);]])],
  [eval `
    cat conftest.out | {
    if read x; then
      echo APR_MAJOR_VERSION=\$x
      if read x; then
        echo APR_MINOR_VERSION=\$x
        if read x; then
          echo APR_PATCH_VERSION=\$x
        else
          false
        fi
      else
        false
      fi
    else
      false
    fi }
  `],[AC_MSG_FAILURE([Cannot determine Apache Portable Runtime version.])])

AS_IF([test 0"$APR_MAJOR_VERSION" -eq 1],
  [AS_IF([test 0"$APR_MINOR_VERSION" -gt 2],
    [AC_DEFINE([USING_APR_13],[1],[Define if you are building against the Apache Portable Runtime Version 1.3 or higher.])dnl
    ],[test 0"$APR_MINOR_VERSION" -eq 2],
    [AC_DEFINE([USING_APR_12],[1],[Define if you are building against the Apache Portable Runtime Version 1.2.])],
    [AC_MSG_ERROR([Unsupported minor version of the Apache Portable Runtime, require version 1.2.0 or higher.])dnl
  ])],
  [AC_MSG_ERROR([Unsupported major version of the Apache Portable Runtime, require version 1.2/1.3/1.x.])dnl
])

AS_IF([test x"$aeb_enable_threads" != x"no"],
  [AC_DEFINE([AEB_USE_THREADS],[1],[Define to enable thread support])])

AS_VAR_IF([aeb_with_libevent],[yes],[
  LIBAEB_EXTRA_LIBS="$LIBAEB_EXTRA_LIBS${LIBAEB_EXTRA_LIBS:+ }\$(top_srcdir)/libs/libevent/libevent.la"
  AS_VAR_IF([aeb_enable_threads],[no],[],[
    LIBAEB_EXTRA_LIBS="$LIBAEB_EXTRA_LIBS \$(top_srcdir)/libs/libevent/libevent_pthreads.la"
  ])
  AX_APPEND_FLAG([[-I$srcdir/libs/libevent/include]],[LIBAEB_EXTRA_CPPFLAGS])
])

AX_SAVE_FLAGS([aeb_event2])
CPPFLAGS="$CPPFLAGS $LIBAEB_EXTRA_CPPFLAGS"
AC_CHECK_HEADERS([event2/thread.h event2/event_struct.h])
AX_RESTORE_FLAGS([aeb_event2])

AC_SUBST([EXTRA_CPPFLAGS])
AC_SUBST([LIBAEB_EXTRA_CPPFLAGS])
AC_SUBST([LIBAEB_EXTRA_CFLAGS])
AC_SUBST([LIBAEB_EXTRA_LIBS])

AC_SUBST([LIBAEB_ABI_MAJOR])
AC_SUBST([LIBAEB_ABI_COMPAT])
AC_SUBST([LIBAEB_API_VERSION])
AC_SUBST([LIBAEB_REVISION])
AC_SUBST([LIBAEB_VERSION])
AC_SUBST([LIBAEB_LIBS])
AC_SUBST([LIBAEB_LINK])
AC_SUBST([LIBAEB_INCLUDE])

AC_SUBST([LIBEVENT_PKGCONFIG_NAME])

AC_SUBST([SHELL])
AC_SUBST([module_configure_flags],["$ac_configure_args"])

AC_CONFIG_FILES([libs/configure-libs],[chmod a+x libs/configure-libs])
AM_CONFIG_HEADER([include/libaeb_config.h])

AC_OUTPUT([Makefile include/libaeb_version.h src/apr_event_bridge.pc])
cd $srcdir/libs && ./configure-libs
