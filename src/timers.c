/* suppor for libevent timers */
#include "internal.h"

#include <apr_errno.h>

static void dispatch_timer(evutil_socket_t unused, short evflags, void *data)
{
  apr_status_t st;
  apr_uint16_t flags = ((evflags & 0x00ff) << 8)|AEB_EVENT_HAS_TIMEOUT;
  apr_pool_t *pool;
  int destroy_pool = 0;
  aeb_event_t *ev = (aeb_event_t*)data;

  ASSERT(ev->type == AEB_TIMER_EVENT);

  if(!IS_EVENT_PERSIST(ev))
    internal_event_del(ev);

  if(ev->callback) {
    if(ev->associated_pool)
      pool = ev->associated_pool;
    else {
#ifdef AEB_USE_THREADS
      ASSERT((pool = aeb_thread_static_pool_acquire()) != NULL);
#else
      ASSERT(apr_pool_create(&pool,ev->pool) == APR_SUCCESS);
#endif
      destroy_pool++;
    }

    /* FIXME: add timing statistics as third arg to aeb_event_info_new_ex() */
    st = ev->callback(aeb_event_info_new_ex(ev,AEB_TIMER_EVENT,NULL,flags,pool),
                                                              ev->user_context);
    if(st != APR_SUCCESS)
      fprintf(stderr,"%s\n",aeb_errorstr(st,pool));
  }

  if(IS_EVENT_PERSIST(ev) && !IS_EVENT_ADDED(ev))
    internal_event_add(ev);

  if(destroy_pool)
#ifdef AEB_USE_THREADS
    aeb_thread_static_pool_release();
#else
    apr_pool_destroy(pool);
#endif

}

AEB_API(apr_status_t) aeb_timer_create_ex(apr_pool_t *pool,
                                          aeb_event_callback_fn callback,
                                          apr_uint16_t flags,
                                          apr_interval_time_t duration,
                                          aeb_event_t **evp)
{
  aeb_event_t *ev;
  if(!evp)
    return APR_EINVAL;

  ev = aeb_event_new(pool,callback,&duration);
  ev->flags |= (flags & 0xff00);
  ev->flags |= AEB_EVENT_HAS_TIMEOUT;
  ev->type = AEB_TIMER_EVENT;
  AEB_ERRNO_CALL(evtimer_assign(ev->event,event_get_base(ev->event),dispatch_timer,ev));

  *evp = ev;
  return APR_SUCCESS;
}