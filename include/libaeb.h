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

/* shared common flags */
typedef enum {
  aeb_flag_timeout = EV_TIMEOUT << 8,
#define AEB_FLAG_TIMEOUT aeb_flag_timeout
  aeb_flag_persist = EV_PERSIST << 8,
#define AEB_FLAG_PERSIST aeb_flag_persist
#ifdef EV_ET
  aeb_flag_edge_triggered = EV_ET << 8
#define AEB_FLAG_EDGE_TRIGGERED aeb_flag_edge_triggered
#endif
} aeb_flag_t;

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

/* Other prototypes */
#include <libaeb_event_types.h>


/* ===  MAIN API === */

/* dispatch loop api */

/* This is the main event dispatch loop function. Calling this will result
 * in events for the current thread being dispatched until duration time
 * expires. If duration is NULL the loop will run indefinitely.
 * If the duration timer expires APR_TIMEUP is returned.
 *
 * In all cases where duration is not NULL it will be reset to the total
 * elapsed time right before this call returns.
 *
 * If the input contents of duration is APR_TIME_C(-1) no timeout will occur
 * but duration will still be set on exit.
 *
 * This function may return other values if the event loop is terminated
 * in other ways (such as via aeb_event_loop_terminate()).
 */
AEB_API(apr_status_t) aeb_event_loop(apr_interval_time_t *duration);

/* Run the event loop N times. Each loop will block until at least one
 * event occurs. Duration will be set if non NULL but is not used to
 * limit the maximum run time.
 */
AEB_API(apr_status_t) aeb_event_loopn(apr_ssize_t count,
                                      apr_interval_time_t *duration);

/* A non-blocking version of the event loop. This will handle queued events
 * but never block. Duration will be set if non NULL but is not used
 * to limit the maximum run time. May return APR_EWOULDBLOCK or
 * APR_SUCCESS.
 */
AEB_API(apr_status_t) aeb_event_loop_try(apr_interval_time_t *duration);

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
/* Set an opaque user context that will be passed to callbacks */
AEB_API(apr_status_t) aeb_event_user_context_set(aeb_event_t*,void*);
/* Userdata management */
AEB_API(apr_status_t) aeb_event_userdata_set(const void*,const char *,
                                             apr_status_t (*cleanup)(void*),
                                             aeb_event_t*);
AEB_API(apr_status_t) aeb_event_userdata_get(void**,const char*,aeb_event_t*);

/* NOTE: aeb_event_clone() "moves" an event to a new pool in a copy-on-write fashion.
 * This works by way of simply using the same reference to the original libevent
 * but also adding a cleanup handler on the old event's pool such that if it goes
 * away a brand new event will be created in it's place and all attributes of
 * the old will then be compied to the new (again this only happens *immediately*)
 * before the old event's memory pool is destroyed.
 *
 * This can be useful for situations like a thread pool handler picking up an event
 * from a different thread and calling aeb_event_clone() to take temporary "ownership"
 * of a CoW clone of it.
 */

AEB_API(apr_status_t) aeb_event_clone(apr_pool_t*, const aeb_event_t *oldev,
                                                   aeb_event_t **newev);
/* event info api */
AEB_API(aeb_event_t) *aeb_event_info_event_get(const aeb_event_info_t*);
AEB_API(apr_int16_t) *aeb_event_info_events(const aeb_event_info_t*);
AEB_API(const apr_pollfd_t*) aeb_event_info_descriptor(const aeb_event_info_t*);

/* timer api */
AEB_API(apr_status_t) aeb_timer_create_ex(apr_pool_t*,
                                          aeb_event_callback_fn,
                                          apr_uint16_t flags,
                                          apr_interval_time_t duration,
                                          aeb_event_t**);

#endif /* _LIBAEB_H */
