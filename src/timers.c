/* suppor for libevent timers */
#include "internal.h"
#include "dispatch.h"

#include <apr_errno.h>

static apr_status_t dispatch_timer(aeb_event_t *ev,
                                   apr_uint16_t flags,
                                   const aeb_libevent_info_t *info,
                                   apr_pool_t *pool)
{
  flags |= AEB_EVENT_HAS_TIMEOUT;
  ASSERT(ev->type == AEB_TIMER_EVENT);

  /* FIXME: add timing statistics as third arg to aeb_event_info_new_ex() */
  return ev->callback(aeb_event_info_new_ex(ev,AEB_TIMER_EVENT,NULL,flags,pool),
                                                              ev->user_context);
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
  if (aeb_dispatch_table[ev->type] == NULL)
    assert(aeb_event_dispatcher_register(ev->type,dispatch_timer,NULL,NULL) == APR_SUCCESS);

  AEB_ERRNO_CALL(evtimer_assign(ev->event,event_get_base(ev->event),aeb_event_dispatcher,ev));

  *evp = ev;
  return APR_SUCCESS;
}