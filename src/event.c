#include "internal.h"

struct aeb_event {
  apr_pool_t *pool;
  apr_pool_t *associated_pool;
  struct event event;
  const apr_pollfd_t *descriptor;
  aeb_event_callback_fn callback;
  apr_time_t timeout;
  apr_uint16_t flags;
};

AEB_POOL_IMPLEMENT_ACCESSOR(event)

typedef apr_status_t (*cleanup_fn)(void*);

static apr_status_t *aeb_event_cleanup(aeb_event_t *ev)
{
  if(ev->flags & AEB_EVENT_ADDED) {
    ev->flags &= ~AEB_EVENT_ADDED;
    event_del(&ev->event);
  }

  return APR_SUCCESS;
}

static void internal_event_add(aeb_event_t *ev)
{
  apr_os_imp_time_t *tv = NULL;

  if(ev->flags & AEB_EVENT_HAS_TIMEOUT) {
    AEB_ASSERT(apr_os_imp_time_get(&tv,&ev->timeout) == APR_SUCCESS,
               "apr_os_imp_time_get failure");
  }
  event_add(&ev->event,tv);
  ev->flags |= AEB_EVENT_ADDED;  
}

static void internal_event_del(aeb_event_t *ev)
{
  ev->flags &= ~AEB_EVENT_ADDED;
  event_del(&ev->event);
}

AEB_API(apr_status_t) aeb_event_create_ex(apr_pool_t *pool,
                                          aeb_event_callback_fn callback,
                                          const apr_pollfd_t *descriptor,
                                          apr_interval_time_t *timeout,
                                          aeb_event_t **ev)
{
  aeb_event_t *event;
  AEB_ASSERT(ev != NULL,"null event");

  event = apr_palloc(pool,sizeof(struct aeb_event));
  AEB_ASSERT(event != NULL,"apr_pcalloc failure");
  event->pool = pool;
  event->associated_pool = NULL;
  event->descriptor = descriptor;
  event->flags = 0;
  memset(&event->timeout,0,sizeof(event->timeout));
  if (timeout) {
    memcpy(&event->timeout,timeout,sizeof(apr_interval_time_t));
    event->flags |= AEB_EVENT_HAS_TIMEOUT;
  }
  memset(&event->event,0,sizeof(event->event));
  apr_pool_cleanup_register(pool,event,(cleanup_fn)aeb_event_cleanup,
                                               apr_pool_cleanup_null);

  if(descriptor && descriptor->p && descriptor->p != pool) {
    apr_pool_cleanup_register(descriptor->p,&event->descriptor,
                              aeb_indirect_wipe,
                              apr_pool_cleanup_null);
    apr_pool_cleanup_register(descriptor->p,event,
                              (cleanup_fn)aeb_event_cleanup,
                              apr_pool_cleanup_null);
  }
  *ev = event;
  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_timeout_get(aeb_event_t *ev, apr_interval_time_t *t)
{
  AEB_ASSERT(ev != NULL,"null event");
  AEB_ASSERT(t != NULL,"null interval time pointer");

  if (ev->flags & AEB_EVENT_HAS_TIMEOUT) {
    memcpy(t,&ev->timeout,sizeof(apr_interval_time_t));
  } else {
    return APR_ENOTIME;
  }
  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_timeout_set(aeb_event_t *ev, apr_interval_time_t *t)
{
  apr_uint16_t flags;
  AEB_ASSERT(ev != NULL,"null event");

  flags = ev->flags;
  if((!t && (flags & AEB_EVENT_HAS_TIMEOUT)) || (t && !(flags & AEB_EVENT_HAS_TIMEOUT)) 
                                             || (t && *t != ev->timeout))
    internal_event_del(ev);

  if(t) {
    memcpy(&ev->timeout,t,sizeof(apr_interval_time_t));
    ev->timeout |= AEB_EVENT_HAS_TIMEOUT;
  } else {
    ev->timeout = APR_TIME_C(0);
    ev->flags &= ~AEB_EVENT_HAS_TIMEOUT;
  }

  if((flags & AEB_EVENT_ADDED) && !(ev->flags & AEB_EVENT_ADDED))
    internal_event_add(ev);

  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_descriptor_set(aeb_event_t *ev, const apr_pollfd_t *desc)
{
  apr_uint16_t flags;
  AEB_ASSERT(ev != NULL,"null event");

  flags = ev->flags;
  if(ev->descriptor && ev->descriptor->p &&
     (!desc || ev->descriptor->p != desc->p))
    apr_pool_cleanup_kill(ev->descriptor->p,ev,(cleanup_fn)aeb_event_cleanup);

  if(ev->descriptor && ev->descriptor->p && desc != ev->descriptor)
    apr_pool_cleanup_kill(ev->descriptor->p,&ev->descriptor,aeb_indirect_wipe);

  if (flags & AEB_EVENT_ADDED)
    internal_event_del(ev);

  ev->descriptor = desc;
  if(desc) {
    if(desc->p && desc->p != ev->pool) {
      apr_pool_cleanup_register(desc->p,ev,
                                (cleanup_fn)aeb_event_cleanup,
                                apr_pool_cleanup_null);
    }
    if ((flags & AEB_EVENT_ADDED) && !(ev->flags & AEB_EVENT_ADDED))
      internal_event_add(ev);
  }

  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_associate_pool(aeb_event_t *ev, apr_pool_t *pool)
{
  AEB_ASSERT(ev != NULL,"null event");

  if(ev->associated_pool && ev->associated_pool != pool) {
    apr_pool_cleanup_kill(ev->associated_pool,&ev->associated_pool,aeb_indirect_wipe);
    if(ev->associated_pool != ev->pool)
      apr_pool_cleanup_kill(ev->associated_pool,ev,(cleanup_fn)aeb_event_cleanup);
  }

  ev->associated_pool = pool;
  if(pool) {
    apr_pool_cleanup_register(pool,&ev->associated_pool,
                              aeb_indirect_wipe,apr_pool_cleanup_null);
    if(pool != ev->pool)
      apr_pool_cleanup_register(pool,ev,(cleanup_fn)aeb_event_cleanup,
                                apr_pool_cleanup_null);
  }

  return APR_SUCCESS;
}

AEB_API(apr_uint16_t) aeb_event_is_active(aeb_event_t *ev)
{
  AEB_ASSERT(ev != NULL,"null event");

  return (ev->flags & AEB_EVENT_ADDED);
}

AEB_API(apr_status_t) aeb_event_add(aeb_event_t *ev)
{
  AEB_ASSERT(ev != NULL,"null event");

  if ((ev->flags & AEB_EVENT_ADDED) == 0)
    internal_event_add(ev);

  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_del(aeb_event_t *ev)
{
  AEB_ASSERT(ev != NULL,"null event");

  if (ev->flags & AEB_EVENT_ADDED)
    internal_event_del(ev);

  return APR_SUCCESS;
}
