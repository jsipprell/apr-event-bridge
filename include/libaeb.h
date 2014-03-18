#ifndef _LIBAEB_H
#define _LIBAEB_H

#ifndef _LIBAEB_CONFIG_H
#define _LIBAEB_CONFIG_H
# ifdef HAVE_LIBAEB_CONFIG_H
# include <libaeb_config.h>
# endif
#endif /* _LIBAEB_CONFIG_H */

#include <libaeb_version.h>

#if defined(HAVE_RELAXED_ALIAS_ATTRIBUTE) && defined(RELAX_STRICT_ALIASING_ATTRIBUTE)
#define AEB_MAY_ALIAS(type) RELAX_STRICT_ALIASING_ATTRIBUTE type
#else
#define AEB_MAY_ALIAS(type) type
#endif

#ifdef HAVE_VISIBILITY_ATTRIBUTE
# ifdef VISIBILITY_EXPORT
#  define AEB_EXPORT VISIBILITY_EXPORT
#  define AEB_API(type) VISIBILITY_EXPORT extern type
# else
#  define AEB_EXPORT
#  define AEB_API(type) extern type
# endif /* VISIBILITY_EXPORT */

# ifdef VISIBILITY_HIDDEN
#  define AEB_HIDDEN VISIBILITY_HIDDEN
#  define AEB_EXTERN VISIBILITY_HIDDEN extern
# else
#  define AEB_HIDDEN
#  define AEB_EXTERN extern
# endif /* VISIBILITY_HIDDEN */

#else /* HAVE_VISIBILITY_ATTRIBUTE */
# define AEB_EXTERN extern
# define AEB_EXPORT
# define AEB_API(type) extern type
# define AEB_HIDDEN
#endif /* HAVE_VISIBILITY_ATTRIBUTE */

#define AEB_POOL_DECLARE_ACCESSOR(type) \
  AEB_EXTERN apr_pool_t *aeb_##type##_pool_get (const aeb_##type##_t*)

#define AEB_POOL_EXPORT_ACCESSOR(type) \
  AEB_API(apr_pool_t*) aeb_##type##_pool_get (const aeb_##type##_t*)


#ifndef APR_WANT_STRFUNC
#define APR_WANT_STRFUNC
#endif

#ifndef APR_WANT_MEMFUNC
#define APR_WANT_MEMFUNC
#endif

#ifndef APR_WANT_STDIO
#define APR_WANT_STDIO
#endif

#ifndef APR_WANT_BYTEFUNC
#define APR_WANT_BYTEFUNC
#endif

/* libapr-1 */
#include <apr.h>
#include <apr_portable.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_network_io.h>
#include <apr_poll.h>
#include <apr_signal.h>
#include <apr_strings.h>
#include <apr_time.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_want.h>

/* libaprutil-1 */
#include <apr_buckets.h>

/* libevent */
#include <event.h>

#ifdef AEB_USE_THREADS
# ifdef HAVE_EVENT2_THREAD_H
# include <event2/thread.h>
# endif /* HAVE_EVENT2_THREAD_H */
#endif /* AEB_USE_THREADS */

#ifndef APR_VERSION_AT_LEAST
#define APR_VERSION_AT_LEAST(major,minor,patch)                    \
                          (((major) < APR_MAJOR_VERSION)           \
  || ((major) == APR_MAJOR_VERSION && (minor) < APR_MINOR_VERSION) \
  || ((major) == APR_MAJOR_VERSION && (minor) == APR_MINOR_VERSION && (patch) <= APR_PATCH_VERSION))
#endif  /* APR_VERSION_AT_LEAST */

#ifndef APR_ARRAY_PUSH
#define APR_ARRAY_PUSH(ary,type) (*((type *)apr_array_push(ary)))
#endif

#ifndef APR_ARRAY_IDX
#define APR_ARRAY_IDX(ary,i,type) (((type *)(ary)->elts)[i])
#endif

/* common types */
typedef void aeb_context_t;
typedef struct aeb_event aeb_event_t;
typedef struct aeb_event_info aeb_event_info_t;

AEB_POOL_EXPORT_ACCESSOR(event);

typedef apr_status_t (*aeb_event_callback_fn)(apr_pool_t*,const aeb_event_info_t*,void*);

#ifdef HUGE_STRING_LEN
#define AEB_BUFSIZE HUGE_STRING_LEN
#else
#define AEB_BUFSIZE 4096
#endif

/* event api */
AEB_API(apr_status_t) aeb_event_create_ex(apr_pool_t*,
                                            aeb_event_callback_fn,
                                            const apr_pollfd_t *descriptor,
                                            apr_interval_time_t *timeout,
                                            aeb_event_t**);
#define aeb_event_create(pool,callback,ev) aeb_event_create_ex((pool),(callback),NULL,NULL,(ev))
AEB_API(apr_status_t) aeb_event_timeout_set(aeb_event_t*,apr_interval_time_t*);
AEB_API(apr_status_t) aeb_event_timeout_get(aeb_event_t*,apr_interval_time_t*);
AEB_API(apr_status_t) aeb_event_descriptor_set(aeb_event_t*, const apr_pollfd_t*);
AEB_API(apr_status_t) aeb_event_callback_set(aeb_event_t*, aeb_event_callback_fn);
AEB_API(apr_status_t) aeb_event_associate_pool(aeb_event_t*, apr_pool_t*);
AEB_API(apr_status_t) aeb_event_add(aeb_event_t*);
AEB_API(apr_status_t) aeb_event_del(aeb_event_t*);
AEB_API(apr_uint16_t) aeb_event_is_active(aeb_event_t*);

/* event info api */
AEB_API(aeb_event_t) *aeb_event_info_event_get(const aeb_event_info_t*);
AEB_API(apr_int16_t) *aeb_event_info_events(const aeb_event_info_t*);
AEB_API(const apr_pollfd_t*) aeb_event_info_descriptor(const aeb_event_info_t*);

#endif /* _LIBAEB_H */