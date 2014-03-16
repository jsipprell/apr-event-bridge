#ifndef _LIBAEB_COMPAT_H
#define _LIBAEB_COMPAT_H

/* libaeb static compatibility library */
#ifndef HAVE_APR_TABLE_CLONE
AEB_EXTERN apr_table_t *apr_table_clone(apr_pool_t*,const apr_table_t*);
#endif

#ifndef HAVE_APR_ARRAY_CLEAR
AEB_EXTERN void apr_array_clear(apr_array_header_t*);
#endif

#ifndef apr_pool_create_unmanaged 
#define apr_pool_create_unmanaged(p) apr_pool_create( (p), NULL )
#endif

#endif /* _LIBAEB_COMPAT_H */