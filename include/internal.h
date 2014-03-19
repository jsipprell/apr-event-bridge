#ifndef _LIBAEB_INTERNAL_H
#define _LIBAEB_INTERNAL_H

#include <libaeb.h>
#include <libaeb_assert.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifndef AEB_DECL_INTERNAL
#define AEB_DECL_INTERNAL(type) AEB_EXTERN type
#endif

#ifndef AEB_INTERNAL
#define AEB_INTERNAL(type) AEB_HIDDEN type
#endif

#undef AEB_API
#define AEB_API(type) AEB_EXPORT type

#define AEB_POOL_IMPLEMENT_ACCESSOR(type) \
  AEB_API(apr_pool_t*) aeb_##type##_pool_get(const aeb_##type##_t * type##object) \
    { return type##object->pool; }

typedef apr_status_t (*cleanup_fn)(void*);

#include "compat.h"
#include "util.h"
#include "event_types.h"
    
typedef enum {
  aeb_event_added = 0x01,
#define AEB_EVENT_ADDED aeb_event_added
  aeb_event_has_timeout = 0x02
#define AEB_EVENT_HAS_TIMEOUT aeb_event_has_timeout
} aeb_event_flag_e;

/* private event data structure */
struct aeb_event {
  apr_pool_t *pool;
  apr_pool_t *associated_pool;
  struct event *event;
  aeb_event_callback_fn callback;
  void *user_context;
  apr_time_t timeout;
  apr_uint16_t flags;

  enum aeb_event_type_e type;
  /* note, this union MUST have the same names as that found in the public
   * struct aeb_event_info so that the AEB_*_EVENT_INFO macros will work on it.
   */
  union {
    const void *data;
    const void *reserved_data;
    const apr_pollfd_t *descriptor_data;
    const void *timer_data;
    const void *signal_data;
  } d;

  /* Copy-on-Write clone source, NULL unless aeb_event_clone() has been called. */
  const struct aeb_event *source;
};

AEB_DECL_INTERNAL(struct event_base*) aeb_event_base(void);
AEB_DECL_INTERNAL(aeb_event_t*) aeb_event_new(apr_pool_t*,aeb_event_callback_fn,
                                              apr_interval_time_t*);
AEB_DECL_INTERNAL(const aeb_event_info_t*) aeb_event_info_new_ex(aeb_event_t*,
                                                                 aeb_event_type_t,
                                                                 const void*,
                                                                 apr_uint16_t flags,
                                                                 apr_pool_t*);
#define aeb_event_info_new(ev,data,flags) \
                aeb_event_info_new_ex((ev),aeb_event_type_null, (data), (flags), NULL)

#endif /* _LIBAEB_INTERNAL_H */