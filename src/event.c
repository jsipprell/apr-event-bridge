#include "internal.h"
#include "dispatch.h"

#ifdef HAVE_EVENT2_EVENT_STRUCT_H
#include <event2/event_struct.h>
#endif

#define AEB_FLAG_MASK (AEB_FLAG_SUBPOOL)

AEB_POOL_IMPLEMENT_ACCESSOR(event)

typedef enum {
  aeb_bitop_or,
  aeb_bitop_and,
  aeb_bitop_xor,
  aeb_bitop_and_not,
  aeb_bitop_set
} aeb_bitop_e;

static apr_status_t dispatch_callback(aeb_event_t *ev,
                                      apr_uint16_t flags,
                                      const aeb_libevent_info_t *info,
                                      apr_pool_t *pool);

static apr_status_t *aeb_event_cleanup(aeb_event_t *ev)
{
  if(ev->flags & AEB_EVENT_ADDED) {
    struct event *event = ev->event;
    ev->flags &= ~AEB_EVENT_ADDED;
    ev->event = NULL;
    event_del(event);
  }

  return APR_SUCCESS;
}

AEB_INTERNAL(void) internal_event_add(aeb_event_t *ev)
{
  apr_os_imp_time_t tval,*tv = NULL;

  if(ev->flags & AEB_EVENT_HAS_TIMEOUT) {
    tv = &tval;
    AEB_ASSERT(apr_os_imp_time_get(&tv,&ev->timeout) == APR_SUCCESS,
               "apr_os_imp_time_get failure");
  }
  event_add(ev->event,tv);
  ev->flags |= AEB_EVENT_ADDED;
}

AEB_INTERNAL(void) internal_event_del(aeb_event_t *ev)
{
  ev->flags &= ~AEB_EVENT_ADDED;
  event_del(ev->event);
}

static apr_status_t dispatch_callback(aeb_event_t *ev,
                                      apr_uint16_t flags,
                                      const aeb_libevent_info_t *info,
                                      apr_pool_t *pool)
{
  assert(ev->callback != NULL);
  
  /* FIXME: add event_info as second arg */
  return ev->callback(aeb_event_info_new(pool,ev,AEB_DESCRIPTOR_EVENT_DATA(ev),
                                         flags),ev->user_context);
}

AEB_INTERNAL(apr_status_t) aeb_assign(aeb_event_t *ev,
                               struct event_base *base,
                               evutil_socket_t fd,
                               short events,
                               event_callback_fn cb,
                               void *arg)
{
  struct event_base *old_base = NULL;
  event_callback_fn old_cb = NULL;
  evutil_socket_t old_fd = -1;
  short old_events = -1;
  void *old_arg = NULL;

  event_get_assignment(ev->event,&old_base,&old_fd,&old_events,&old_cb,&old_arg);
  if(!base) base = old_base;
  if(fd < 0) fd = old_fd;
  if(events <= 0) {
    events = old_events;
  } else if(ev->flags & 0xff00)
    events |= (ev->flags >> 8) & ~(EV_READ|EV_WRITE);
  if(!cb) cb = old_cb;
  if(!arg) arg = old_arg;

  ASSERT(event_assign(ev->event,base,fd,events,cb,arg) == 0);
  return APR_SUCCESS;
}

inline static short cvt_apr_events(apr_int16_t reqevents)
{
  short eventset = 0;
  if(reqevents & APR_POLLIN)
    eventset |= EV_READ;
  if(reqevents & APR_POLLOUT)
    eventset |= EV_WRITE;

  return eventset;
}

static apr_status_t aeb_assign_from_descriptor(aeb_event_t *ev, const apr_pollfd_t *d)
{
  evutil_socket_t fd = -1;
  apr_status_t st = APR_SUCCESS;
  short eventset = 0;

  if(!d)
    d = AEB_DESCRIPTOR_EVENT_INFO(ev);

  if (d != NULL) {
    const apr_descriptor *desc = &d->desc;

    if(d->client_data && ev->user_context == NULL)
      ev->user_context = d->client_data;
    eventset |= cvt_apr_events(d->reqevents);
    switch(d->desc_type) {
      case APR_POLL_FILE:
        st = apr_os_file_get(&fd,desc->f);
        break;
      case APR_POLL_SOCKET:
        st = apr_os_sock_get(&fd,desc->s);
        break;
      default:
        st = APR_EINVAL;
        break;
    }

  }

  if(st == APR_SUCCESS) {
    struct event_base *base;
    event_callback_fn callback;
    void *callback_arg;

    event_get_assignment(ev->event,&base,NULL,NULL,&callback,&callback_arg);
    if(callback == NULL)
      callback = aeb_event_dispatcher;
    if(!callback_arg)
      callback_arg = ev;

    if(ev->flags & 0xff00)
      eventset |= (ev->flags >> 8) & ~(EV_READ|EV_WRITE);
    ASSERT(event_assign(ev->event,base,fd,eventset,callback,callback_arg) == 0);
  }

  return st;
}

static inline struct event *pcalloc_libevent(apr_pool_t *pool)
{
  apr_size_t event_sz = event_get_struct_event_size();
#ifdef HAVE_EVENT2_EVENT_STRUCT_H
  return (struct event*)apr_pcalloc(pool,event_sz < sizeof(struct event) ?
                                   sizeof(struct event) : event_sz);
#else /* !HAVE_EVENT2_EVENT_STRUCT_H */
  return (struct event*)apr_pcalloc(pool,event_sz);
#endif
}

AEB_INTERNAL(aeb_event_t*) aeb_event_new(apr_pool_t *pool,
                                         aeb_event_callback_fn callback,
                                         apr_interval_time_t *timeout)
{
  aeb_event_t *ev;

  ASSERT(pool != NULL);
  ASSERT((ev = apr_palloc(pool,sizeof(struct aeb_event))) != NULL);
  ev->pool = pool;
  ev->callback = callback;
  ev->associated_pool = NULL;
  ev->type = AEB_NULL_EVENT;
  memset(&ev->d,0,sizeof(ev->d));
  ev->flags = 0;
  memset(&ev->timeout,0,sizeof(ev->timeout));
  if (timeout) {
    memcpy(&ev->timeout,timeout,sizeof(apr_interval_time_t));
    ev->flags |= AEB_EVENT_HAS_TIMEOUT;
  }
  ev->source = NULL;
  ev->event = pcalloc_libevent(ev->pool);
  if (aeb_dispatch_table[aeb_event_type_descriptor] == NULL)
    assert(aeb_event_dispatcher_register(aeb_event_type_descriptor,
                                         dispatch_callback,
                                         NULL,NULL) == APR_SUCCESS);
  AEB_ASSERT(ev->event,"apr_palloc failed while allocating struct event");
  AEB_ASSERT(event_assign(ev->event,aeb_event_base(),-1,0,aeb_event_dispatcher,ev) == 0,
            "event_assign failure");
  apr_pool_cleanup_register(pool,ev,(cleanup_fn)aeb_event_cleanup,
                                          apr_pool_cleanup_null);
  return ev;
}

static apr_status_t aeb_event_clone_cow(void *data)
{
  aeb_event_t *ev = (aeb_event_t*)data;
  struct event *oldevent;
  struct event_base *base;
  evutil_socket_t sock;
  short eventset;
  event_callback_fn callback;
  void *callback_arg;

  apr_uint16_t flags;

  ASSERT(ev != NULL);
  flags = ev->flags;
  ASSERT(ev->event != NULL);

  if (flags & AEB_EVENT_ADDED)
    internal_event_del(ev);

  oldevent = ev->event;
  event_get_assignment(oldevent,&base,&sock,&eventset,&callback,&callback_arg);

  if(ev->source) {
    ev->event = pcalloc_libevent(ev->pool);
    ASSERT(ev->event != NULL);
    ev->source = NULL;
  }

  if(ev->event && oldevent != ev->event) {
    ASSERT(event_assign(ev->event,base,sock,eventset,callback,callback_arg) == 0);
  }

  if (flags & AEB_EVENT_ADDED)
    internal_event_add(ev);

  return APR_SUCCESS;
}

static apr_status_t aeb_event_clone_cow_remove(void *data)
{
  aeb_event_t *ev = (aeb_event_t*)data;

  ASSERT(ev != NULL && ev->pool != NULL);
  apr_pool_cleanup_kill(ev->pool,ev,aeb_event_clone_cow);
  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_clone(apr_pool_t *pool,
                                      const aeb_event_t *ev,
                                      aeb_event_t **evp)
{
  aeb_event_t *newev;

  if(!pool || !ev)
    return APR_EINVAL;

  /* if it's already been cloned then make a copy of the clone, not this one */
  while(ev->source != NULL)
    ev = ev->source;

  newev = apr_palloc(pool,sizeof(struct aeb_event));
  ASSERT(newev != NULL);
  newev->pool = pool;
  newev->source = ev;
  newev->callback = ev->callback;
  newev->associated_pool = ev->associated_pool;
  memcpy(&newev->timeout,&ev->timeout,sizeof(apr_interval_time_t));
  newev->flags = ev->flags;
  newev->type = ev->type;
  memcpy(&newev->d,&ev->d,sizeof(ev->d));
  newev->event = ev->event;

  if(newev->event && pool != ev->pool) {
    if(!apr_pool_is_ancestor(pool,ev->pool)) {
      apr_pool_cleanup_register(ev->pool,newev,aeb_event_clone_cow,apr_pool_cleanup_null);
      apr_pool_cleanup_register(pool,newev,aeb_event_clone_cow_remove,apr_pool_cleanup_null);
    }
  }

  if(AEB_DESCRIPTOR_EVENT_INFO(newev) && AEB_DESCRIPTOR_EVENT_DATA(newev)->p)
    apr_pool_cleanup_register(AEB_DESCRIPTOR_EVENT_DATA(newev)->p,newev,
                              (cleanup_fn)aeb_event_cleanup,
                              apr_pool_cleanup_null);

  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_create_ex(apr_pool_t *pool,
                                          aeb_event_callback_fn callback,
                                          const apr_pollfd_t *desc,
                                          apr_interval_time_t *timeout,
                                          aeb_event_t **ev)
{
  aeb_event_t *event;
  AEB_ASSERT(ev != NULL,"null event");

  event = aeb_event_new(pool,callback,timeout);
  if(desc) {
    event->type = AEB_DESCRIPTOR_EVENT;
    AEB_DESCRIPTOR_EVENT_DATA(event) = desc;
  }
  event->flags = 0;

  if(desc && desc->p && desc->p != pool) {
    apr_pool_cleanup_register(desc->p,&AEB_DESCRIPTOR_EVENT_DATA(event),
                              aeb_indirect_wipe,
                              apr_pool_cleanup_null);
    apr_pool_cleanup_register(desc->p,event,
                              (cleanup_fn)aeb_event_cleanup,
                              apr_pool_cleanup_null);
  }
  aeb_assign_from_descriptor(event,desc);
  *ev = event;

  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_callback_set(aeb_event_t *ev, aeb_event_callback_fn cb)
{
  AEB_ASSERT(ev != NULL,"null event");

  if(!cb && !IS_EVENT_ADDED(ev) && (ev->flags & AEB_EVENT_HAS_TIMEOUT) == 0)
    internal_event_del(ev);

  ev->callback = cb;
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
    ev->flags |= AEB_EVENT_HAS_TIMEOUT;
  } else {
    ev->timeout = APR_TIME_C(0);
    ev->flags &= ~AEB_EVENT_HAS_TIMEOUT;
  }

  if((flags & AEB_EVENT_ADDED) && !(ev->flags & AEB_EVENT_ADDED))
    internal_event_add(ev);

  return APR_SUCCESS;
}

static apr_status_t aeb_event_bit_op(aeb_event_t *ev, aeb_bitop_e op, apr_int16_t reqevents)
{
  apr_status_t st = APR_EINVAL;
  apr_pollfd_t *pollfd = NULL;
  apr_uint16_t flags;

  if(!ev || ev->type != AEB_DESCRIPTOR_EVENT ||
              (pollfd = (apr_pollfd_t*)AEB_DESCRIPTOR_EVENT_INFO(ev)) == NULL)
    return st;

  if((flags = ev->flags) & AEB_EVENT_ADDED)
    internal_event_del(ev);

  switch(op) {
    case aeb_bitop_or:
      pollfd->reqevents |= reqevents;
      break;
    case aeb_bitop_and:
      pollfd->reqevents &= reqevents;
      break;
    case aeb_bitop_and_not:
      pollfd->reqevents &= ~reqevents;
      break;
    case aeb_bitop_xor:
      pollfd->reqevents ^= reqevents;
      break;
    default:
      pollfd->reqevents = reqevents;
      break;
  }

  if ((st = aeb_assign(ev,NULL,-1,cvt_apr_events(reqevents),NULL,NULL)) == APR_SUCCESS &&
                                        (flags & AEB_EVENT_ADDED) &&
                                        (reqevents & (APR_POLLOUT|APR_POLLIN)) != 0 &&
                                        !IS_EVENT_ADDED(ev))
    internal_event_add(ev);

  return st;
}

AEB_API(apr_status_t) aeb_event_descriptor_events_or(aeb_event_t *ev, apr_int16_t reqevents)
{
  return aeb_event_bit_op(ev,aeb_bitop_or,reqevents);
}

AEB_API(apr_status_t) aeb_event_descriptor_events_and(aeb_event_t *ev, apr_int16_t reqevents)
{
  return aeb_event_bit_op(ev,aeb_bitop_and,reqevents);
}

AEB_API(apr_status_t) aeb_event_descriptor_events_not(aeb_event_t *ev, apr_int16_t reqevents)
{
  return aeb_event_bit_op(ev,aeb_bitop_and_not,reqevents);
}

AEB_API(apr_status_t) aeb_event_descriptor_events_xor(aeb_event_t *ev, apr_int16_t reqevents)
{
  return aeb_event_bit_op(ev,aeb_bitop_xor,reqevents);
}

AEB_API(apr_status_t) aeb_event_descriptor_events_set(aeb_event_t *ev, apr_int16_t reqevents)
{
  return aeb_event_bit_op(ev,aeb_bitop_set,reqevents);
}

AEB_API(apr_status_t) aeb_event_descriptor_set(aeb_event_t *ev, const apr_pollfd_t *desc)
{
  apr_uint16_t flags;
  int reset_cleanup = 0, reset_indirect_cleanup = 0;
  AEB_ASSERT(ev != NULL,"null event");

  if(ev->type != AEB_DESCRIPTOR_EVENT && !AEB_EVENT_IS_NULL(ev))
    return APR_EINVAL;

  flags = ev->flags;
  if(AEB_DESCRIPTOR_EVENT_INFO(ev) && AEB_DESCRIPTOR_EVENT_DATA(ev)->p) {
    if(!desc || AEB_DESCRIPTOR_EVENT_DATA(ev)->p != desc->p) {
      apr_pool_cleanup_kill(AEB_DESCRIPTOR_EVENT_DATA(ev)->p,
                            ev,(cleanup_fn)aeb_event_cleanup);
      reset_cleanup++;
    }
    if(desc != AEB_DESCRIPTOR_EVENT_DATA(ev)) {
      apr_pool_cleanup_kill(AEB_DESCRIPTOR_EVENT_DATA(ev)->p,
                              &AEB_DESCRIPTOR_EVENT_DATA(ev),
                              aeb_indirect_wipe);
      reset_indirect_cleanup++;
    }
  }

  if (flags & AEB_EVENT_ADDED)
    internal_event_del(ev);

  ev->type = AEB_DESCRIPTOR_EVENT;
  AEB_DESCRIPTOR_EVENT_DATA(ev) = desc;
  if(desc && desc->p) {
    if(reset_indirect_cleanup)
      apr_pool_cleanup_register(desc->p,&AEB_DESCRIPTOR_EVENT_DATA(ev),
                                aeb_indirect_wipe,
                                apr_pool_cleanup_null);
    if(reset_cleanup)
      apr_pool_cleanup_register(desc->p,ev,
                                (cleanup_fn)aeb_event_cleanup,
                                apr_pool_cleanup_null);
  }
  if(desc) {
    if (aeb_assign_from_descriptor(ev,desc) == APR_SUCCESS &&
                                            (flags & AEB_EVENT_ADDED) &&
                                          !(ev->flags & AEB_EVENT_ADDED))
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

AEB_API(apr_uint16_t) aeb_event_is_active(aeb_event_t *ev, apr_int32_t reqevents)
{
  AEB_ASSERT(ev != NULL,"null event");
  if (ev->flags & AEB_EVENT_ADDED) {
    if (reqevents > -1) {
      event_callback_fn cb = NULL;
      short curevents = -1;

      reqevents = cvt_apr_events(reqevents);
      event_get_assignment(ev->event,NULL,NULL,&curevents,&cb,NULL);
      if (cb != NULL && curevents > -1)
        return ((apr_uint16_t)curevents & (apr_uint16_t)reqevents);
    } else return 1;
  }
  return 0;
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

/* Assign opaque user-defined context which will be passed to the callback */
AEB_API(apr_status_t) aeb_event_user_context_set(aeb_event_t *ev, void *context)
{
  AEB_ASSERT(ev != NULL,"null event");

  ev->user_context = context;
  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_userdata_set(const void *data, const char *key,
                                             apr_status_t (*cleanup)(void*),
                                             aeb_event_t *ev)
{
  return apr_pool_userdata_set(data,key,cleanup,ev->pool);
}

AEB_API(apr_status_t) aeb_event_userdata_get(void **data, const char *key,
                                             aeb_event_t *ev)
{
  return apr_pool_userdata_get(data,key,ev->pool);
}

AEB_API(apr_status_t) aeb_event_flags_get(aeb_event_t *ev, apr_uint16_t *flags)
{
  if(!flags || !ev)
    return APR_EINVAL;

  *flags = (ev->flags & AEB_FLAG_ALL);
  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_event_flags_set(aeb_event_t *ev, apr_uint16_t flags)
{
  if(!ev || (flags & ~AEB_FLAG_ALL) != 0)
    return APR_EINVAL;

  ev->flags &= (ev->flags & ~AEB_FLAG_MASK) | (flags & AEB_FLAG_MASK);

  return APR_SUCCESS;
}
