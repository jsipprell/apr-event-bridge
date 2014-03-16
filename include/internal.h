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

#include "compat.h"
#include "util.h"
  
typedef enum {
  aeb_event_added = 0x01,
#define AEB_EVENT_ADDED aeb_event_added
  aeb_event_has_timeout = 0x02
#define AEB_EVENT_HAS_TIMEOUT aeb_event_has_timeout
} aeb_event_flag_e;

#endif /* _LIBAEB_INTERNAL_H */