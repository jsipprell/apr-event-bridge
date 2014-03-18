#ifndef _LIBAEB_ASSERT_H
#define _LIBAEB_ASSERT_H

#include <libaeb.h>

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

AEB_API(void) aeb_abort(const char *fmt, ...)
              __attribute__((format(printf,1,2)));
AEB_API(const char*) aeb_abort_msg(const char *fmt, ...)
              __attribute__((format(printf,1,2)));

#define ASSERT assert
#if defined(NDEBUG) && !defined(DEBUGGING)
# define AEB_ASSERT(cond,msg) assert(cond)
# define AEB_ASSERTV(cond,fmt,...) assert(cond)
#else /* !NDEBUG || DEBUGGING */
# define AEB_ASSERT(cond,msg) { if ( !(cond) ) { \
  aeb_abort("assertion_failure: '%s' at " APR_POOL__FILE_LINE__, (msg) ); \
  } }
# define AEB_ASSERTV(cond, fmt, ...) { if ( !(cond) ) { \
  aeb_abort("assertion failure: '%s' at " APR_POOL__FILE_LINE__, \
                  (aeb_abort_msg((fmt), __VA_ARGS__)) ); \
  } }
#endif /* !NDEBUG || DEBUGGING */
#endif /* _LIBAEB_ASSERT_H */