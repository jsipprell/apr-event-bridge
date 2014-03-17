#ifndef _LIBAEB_WEAKREF_H
#define _LIBAEB_WEAKREF_H

#include "internal.h"

/* Weakrefs are "context proxies"; object which reside in a separate isolated apr_pool_t and point to
a "real" context. After creation they can be used *exactly* once at which time they are considered
abandoned. At regular intervals, during consumption, the underlying apr pool will be cleared as to
release memory as long as no outstanding weakref exist. */

typedef struct aeb_weakref aeb_weakref_t;

/* This creates a new weakref proxying to a context of some arbirary kind. The weakref is "bound" to
a pool which should be a pool from which it was allocated. When said pool is cleared or destroyed the
weakref will be considered a zombie; meaning that it will remain valid and in existence (until) consumed
but will return NULL when zeke_weakref_consume() is called on it. */
AEB_DECL_INTERNAL(aeb_weakref_t*) aeb_weakref_make(void*,apr_pool_t*);

/* This consumes an weakref and returns whatever the weakref proxies to.
!! ONCE THIS IS CALLED THE INDIRECT CAN NEVER BE USED AGAIN !!

Note that all callers MUST assume this may return NULL meaning that the memory which provided the original
context has been released and thus said context no longer exists. */
AEB_DECL_INTERNAL(void*) aeb_weakref_consume(aeb_weakref_t*);

#endif /* _LIBAEB_WEAKREF_H */