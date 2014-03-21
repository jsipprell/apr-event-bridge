#dnl Determine what type of threads libevent supports (if any)
#dnl variable libevent_thread_type will be set if and only if
#dnl the first action is executed.
#dnl AEB_EVENT_CHECK_THREADS([ACTION-IF-FOUND],[ACTION-IF-NOT-FOUND])
AC_DEFUN([AEB_EVENT_CHECK_THREADS],
  [AC_MSG_CHECKING([type of threads supported by libevent])
   AX_SAVE_FLAGS([aeb_event_check_threads])
   AS_VAR_IF([LIBEVENT_INCDIR],[""],[CPPFLAGS=],[CPPFLAGS="-I$LIBEVENT_INCDIR"])
   AS_IF([test x"$LIBEVENT_PTHREADS_CFLAGS" = x""],
      [CFLAGS="$LIBEVENT_CFLAGS"],
      [CFLAGS="$LIBEVENT_PTHREADS_CFLAGS"])
   AS_IF([test x"$LIBEVENT_PTHREADS_LIBS" = x""],
      [LIBS="$LIBEVENT_LIBS"],
      [LIBS="$LIBEVENT_PTHREADS_LIBS"])
   AC_RUN_IFELSE([AC_LANG_PROGRAM(
          [[#include <stdio.h>
            #include <event.h>
            #ifdef HAVE_EVENT2_THREAD_H
            #include <event2/thread.h>
            #endif]],
          [[FILE *f = fopen("conftest.out","w");
            #ifdef EVTHREAD_USE_PTHREADS_IMPLEMENTED
            evthread_use_pthreads();
            fprintf(f,"pthreads\n");
            #elif defined(EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED)
            evthread_use_windows_threads();
            fprintf(f,"windows\n");
            #else
            #error no thread support
            #endif
            fclose(f);]])],
      [libevent_thread_type=`cat conftest.out`
       AC_MSG_RESULT([$libevent_thread_type])],
      [libevent_thread_type=
       AC_MSG_RESULT([none])])
   AX_RESTORE_FLAGS([aeb_event_check_threads])
   AS_VAR_IF([libevent_thread_type],[""],[$2],[$1])dnl
])
