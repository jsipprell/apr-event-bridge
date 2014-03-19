#include "internal.h"

/* high-level event_loop processing */
AEB_INTERNAL(apr_status_t) aeb_run_event_loop(apr_interval_time_t *timeout,
                                              apr_interval_time_t *duration,
                                              apr_ssize_t *counter,
                                              int nonblocking)
{
  apr_status_t st = APR_SUCCESS;
  int flags = 0;
  apr_size_t total = -1;
  apr_time_t start_time = apr_time_now();
  struct event_base *base = aeb_event_base();
  apr_os_imp_time_t *tv = NULL;

  ASSERT(base != NULL);
  if(nonblocking)
    flags |= EVLOOP_NONBLOCK;
  if(counter) {
    total = *counter;
    *counter = 0;
    flags |= EVLOOP_ONCE;
  }

  for(; total == -1 || total > 0; total--) {
    int rc;

    if(timeout && tv == NULL) {
      ASSERT(apr_os_imp_time_get(&tv,timeout) == APR_SUCCESS);
    }

    if(tv)
      assert(event_base_loopexit(base,tv) == 0);

    rc = event_base_loop(base,flags);
    if(rc != 0) {
      st = apr_get_os_error();
      break;
    }
    if(counter)
      (*counter)++;
  }

  if(duration)
    *duration = apr_time_now() - start_time;
  return st;
}

AEB_API(apr_status_t) aeb_event_loop(apr_interval_time_t *duration)
{
  apr_interval_time_t t;
  apr_interval_time_t *timeout = NULL;
  if(duration) {
    t = *duration;
    timeout = &t;
  }

  return aeb_run_event_loop(timeout,duration,NULL,0);
}

AEB_API(apr_status_t) aeb_event_loopn(apr_ssize_t count, apr_interval_time_t *duration)
{
  return aeb_run_event_loop(NULL,duration,&count,0);
}

AEB_API(apr_status_t) aeb_event_loop_try(apr_interval_time_t *duration)
{
  return aeb_run_event_loop(NULL,duration,NULL,1);
}
