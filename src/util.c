#include "internal.h"
#include "util.h"

#include <apr_errno.h>

static apr_pool_t *error_pool = NULL;
static apr_file_t *aeb_ferror = NULL;
static int error_pool_refcount = 0;

#define ERROR_POOL_BEGIN _access_error_pool(1,0); {
#define ERROR_POOL_END } _access_error_pool(-1,1);
#define ERROR_POOL_END_SAFE } _access_error_pool(-1,0);

static inline apr_pool_t *_access_error_pool(int ref, int clear)
{
  static unsigned error_pool_count = 0;

  error_pool_refcount += ref;
  error_pool_count++;

  if(error_pool == NULL && error_pool_refcount > 0) {
    if(apr_pool_create(&error_pool,NULL) != APR_SUCCESS)
      abort();
    apr_pool_tag(error_pool,"apr-event-bridge global error pool");
  } else if(clear && error_pool != NULL &&
            error_pool_count > 10 && error_pool_refcount == 0)
    apr_pool_clear(error_pool);
  return error_pool;
}

AEB_INTERNAL(const char *) aeb_errorstr(apr_status_t st, apr_pool_t *pool)
{
  char buf[AEB_BUFSIZE] = "";

  ASSERT(apr_strerror(st,buf,sizeof(buf)-1) != NULL);
  buf[sizeof(buf)-1] = '\0';

  if(pool == NULL) {
    const char *e;
    ERROR_POOL_BEGIN
    e = apr_pstrdup(error_pool,buf);
    ERROR_POOL_END_SAFE
    return e;
  }

  return apr_pstrdup(pool,buf);
}

AEB_INTERNAL(apr_status_t) aeb_indirect_wipe(void *data)
{
  void **p = (void**)data;
  if(p && *p)
    *p = NULL;
  return APR_SUCCESS;
}

AEB_API(void) aeb_abort(const char *fmt, ...)
{
  va_list ap;
  apr_pool_t *pool = ERROR_POOL_BEGIN;

  va_start(ap,fmt);
  if(aeb_ferror == NULL) {
    apr_status_t st = apr_file_open_stderr(&aeb_ferror,pool);
    if(st != APR_SUCCESS)
      st = apr_file_open_stdout(&aeb_ferror,pool);
    if(st != APR_SUCCESS)
      aeb_ferror = NULL;
    if (aeb_ferror != NULL)
      apr_pool_cleanup_register(pool,&aeb_ferror,aeb_indirect_wipe,
                                apr_pool_cleanup_null);
  }
  if(aeb_ferror) {
    apr_file_puts(apr_pvsprintf(pool,fmt,ap),aeb_ferror);
    apr_file_putc('\n',aeb_ferror);
    apr_file_flush(aeb_ferror);
  }

  va_end(ap);
  ERROR_POOL_END;
  abort();
}

AEB_API(const char *)aeb_abort_msg(const char *fmt, ...)
{
  va_list ap;
  const char *msg;
  apr_pool_t *pool = ERROR_POOL_BEGIN;

  va_start(ap,fmt);
  msg = apr_pvsprintf(pool,fmt,ap);
  va_end(ap);

  ERROR_POOL_END_SAFE;
  return msg;
}
