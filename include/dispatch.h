#ifndef _LIBAEB_DISPATCH_H
#define _LIBAEB_DISPATCH_H

#include "internal.h"
#include <libaeb_event_info.h>

typedef struct {
  evutil_socket_t fd;
  short flags;
  void *data;
} aeb_libevent_info_t;

typedef apr_status_t (*aeb_dispatch_fn)(aeb_event_t*,apr_uint16_t,
                                        const aeb_libevent_info_t*,
                                        apr_pool_t*);

AEB_DECL_INTERNAL(apr_status_t) aeb_event_dispatcher_register(aeb_event_type_t,
                                                              aeb_dispatch_fn,
                                                              aeb_dispatch_fn *old,
                                                              apr_pool_t*);
AEB_DECL_INTERNAL(aeb_dispatch_fn) aeb_dispatch_table[256];

#endif /* _LIBAEB_DISPATCH_H */
