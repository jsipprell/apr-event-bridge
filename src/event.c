#include "internal.h"

#ifdef HAVE_EVENT2_EVENT_STRUCT_H
#include <event2/event_struct.h>
#endif

AEB_POOL_IMPLEMENT_ACCESSOR(event)

static void dispatch_callback(evutil_socket_t, short, void*);
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

static void internal_event_add(aeb_event_t *ev)
{
  apr_os_imp_time_t *tv = NULL;

  if(ev->flags & AEB_EVENT_HAS_TIMEOUT) {
    AEB_ASSERT(apr_os_imp_time_get(&tv,&ev->timeout) == APR_SUCCESS,
               "apr_os_imp_time_get failure");
  }
  event_add(ev->event,tv);
  ev->flags |= AEB_EVENT_ADDED;
}

static void internal_event_del(aeb_event_t *ev)
{
  ev->flags &= ~AEB_EVENT_ADDED;
  event_del(ev->event);
}

static void dispatch_callback(evutil_socket_t fd, short evflags, void *data)
{
  apr_status_t st = APR_SUCCESS;
  apr_uint16_t flags = ((evflags & 0x00ff) << 8);
  apr_pool_t *pool;
  int destroy_pool = 0;
  aeb_event_t *ev = (aeb_event_t*)data;

  if(ev->callback) {
    if(ev->associated_pool)
      pool = ev->associated_pool;
    else {
      ASSERT(apr_pool_create(&pool,ev->pool) == APR_SUCCESS);
      destroy_pool++;
    }

    /* FIXME: add event_info as second arg */
    st = ev->callback(pool,aeb_event_info_new(ev,AEB_DESCRIPTOR_EVENT_DATA(ev),flags),
                      ev->user_context);
    if(st != APR_SUCCESS)
      fprintf(stderr,"%s\n",aeb_errorstr(st,pool));
  }

  if(destroy_pool)
    apr_pool_destroy(pool);
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
    if(d->reqevents & APR_POLLIN)
      eventset |= EV_READ;
    if(d->reqevents & APR_POLLOUT)
      eventset |= EV_WRITE;

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
      callback = dispatch_callback;
    if(!callback_arg)
      callback_arg = ev->user_context;

    if(ev->flags & 0xff00)
      eventset |= (ev->flags >> 8);
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
  return event_sz;
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
  AEB_ASSERT(ev->event,"apr_palloc failed while allocating struct event");
  AEB_ASSERT(event_assign(ev->event,aeb_event_base(),-1,0,dispatch_callback,ev) == 0,
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

  if(ev->type != AEB_DESCRIPTOR_EVENT && !AEB_EVENT_IS_NULL(ev))
    return APR_EINVAL;

  flags = ev->flags;
  if(AEB_DESCRIPTOR_EVENT_INFO(ev) && AEB_DESCRIPTOR_EVENT_DATA(ev)->p) {
    if(!desc || AEB_DESCRIPTOR_EVENT_DATA(ev)->p != desc->p)
      apr_pool_cleanup_kill(AEB_DESCRIPTOR_EVENT_DATA(ev)->p,
                            ev,(cleanup_fn)aeb_event_cleanup);

    if(desc != AEB_DESCRIPTOR_EVENT_DATA(ev))
      apr_pool_cleanup_kill(AEB_DESCRIPTOR_EVENT_DATA(ev)->p,
                            &AEB_DESCRIPTOR_EVENT_DATA(ev),
                            aeb_indirect_wipe);
  }

  if (flags & AEB_EVENT_ADDED)
    internal_event_del(ev);

  ev->type = AEB_DESCRIPTOR_EVENT;
  AEB_DESCRIPTOR_EVENT_DATA(ev) = desc;
  if(desc) {
    if(desc->p && desc->p != ev->pool) {
      apr_pool_cleanup_register(desc->p,ev,
                                (cleanup_fn)aeb_event_cleanup,
                                apr_pool_cleanup_null);
    }
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
