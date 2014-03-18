#ifndef _LIBAEB_UTIL_H
#define _LIBAEB_UTIL_H

#include "internal.h"

/* apr pool cleanup function that will set a pointer to a pointer NULL but check
   to make sure the pointer is !NULL first */
AEB_DECL_INTERNAL(apr_status_t) aeb_indirect_wipe(void*);
AEB_DECL_INTERNAL(const char*) aeb_errorstr(apr_status_t,apr_pool_t*);

#endif /* _LIBAEB_UTIL_H */