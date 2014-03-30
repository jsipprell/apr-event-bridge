/* libevent + APR bucket brigades = cheesy goodness */
#include "internal.h"
#include "dispatch.h"

#include <apr_buckets.h>

struct aeb_brigade_info {
  apr_pool_t *p;
  apr_uint32_t flags; /* low 16 bits are just event flags */
  apr_socket_t *s;
  /* The following four callbacks are optional, if not set the
   * underlying event cb is always used.
   */
  apr_uint16_t events;
  aeb_event_callback_fn read_cb,write_cb,timeout_cb;
  apr_bucket_brigade *read_bb, *rpending;
  apr_bucket_brigade *write_bb, *wpending;
#ifdef AEB_USE_THREADS
  apr_thread_mutex_t *read_mutex,*write_mutex;
#endif
};

static apr_status_t dispatch_brigade(aeb_event_t*,apr_uint16_t,
                                     const aeb_libevent_info_t*,
                                     apr_pool_t*);

static apr_status_t brigade_write_handler(aeb_brigade_info_t *state,
                                          const aeb_event_info_t *info)
{
  apr_status_t st = APR_SUCCESS;
  apr_bucket_brigade *more,*bb = state->write_bb;
  apr_bucket_brigade *output = NULL;
  apr_read_type_e block = APR_NONBLOCK_READ;
  apr_pool_t *input_pool = (bb ? bb->p : NULL);
  apr_bucket_alloc_t *output_alloc = NULL;
  apr_bucket *last = NULL;

  assert(bb != NULL);
#ifdef AEB_USE_THREADS
  AEB_APR_ASSERT(apr_thread_mutex_lock(state->write_mutex));
  do {
#endif
  if(state->wpending) {
    APR_BRIGADE_CONCAT(state->wpending,bb);
    bb = state->wpending;
    state->write_bb = bb;
    state->wpending = NULL;
  }
  output_alloc = apr_bucket_alloc_create(info->pool);
  assert(output_alloc != NULL);

  while(bb && !APR_BRIGADE_EMPTY(bb)) {
    apr_size_t nbytes = 0;
    apr_bucket *next,*b;
    apr_uint32_t nvecs = 0;
    more = NULL;
    for(b = APR_BRIGADE_FIRST(bb), next = APR_BUCKET_NEXT(b);
                               b != APR_BRIGADE_SENTINEL(bb);
                               b = (next ? next : APR_BRIGADE_FIRST(bb)),
                               next = APR_BUCKET_NEXT(b)) {
      const char *str = NULL;
      apr_size_t n = 0;

      if (APR_BUCKET_IS_EOS(b)) {
        if(last)
          apr_bucket_delete(last);
        last = b;
        APR_BUCKET_REMOVE(last);
        break;
      } else if(APR_BUCKET_IS_FLUSH(b)) {
        /* noop */
        apr_bucket_delete(b);
        continue;
      }
      st = apr_bucket_read(b,&str,&n,block);
      if (APR_STATUS_IS_EAGAIN(st)) {
        if (b != APR_BRIGADE_FIRST(bb))
          more = apr_brigade_split(bb,b);
        b = NULL;
        break;
      } else {
        AEB_APR_ASSERT(st);
      }
      if(n) {
        apr_size_t sz = (n <= AEB_MAX_WRITE ? n : AEB_MAX_WRITE);

        if(sz < n) {
          apr_bucket_split(b,sz);
          next = APR_BUCKET_NEXT(b);
        }

        APR_BUCKET_REMOVE(b);

        if (!output)
          assert((output = apr_brigade_create(input_pool,output_alloc)) != NULL);

        APR_BRIGADE_INSERT_TAIL(output,b);
        b = NULL;
        nbytes += sz;
        if(++nvecs >= AEB_MAX_IOVECS)
          next = APR_BRIGADE_SENTINEL(bb);
        /* printf(" NVECS %d,%d\n",(int)nvecs,(int)nbytes); */
      }
      if(b)
        apr_bucket_delete(b);
    }
    if(nbytes > 0) {
      struct iovec vec[AEB_MAX_IOVECS];
      int n = AEB_MAX_IOVECS;
      apr_size_t written = nbytes;

      assert(output != NULL && !APR_BRIGADE_EMPTY(output));

      AEB_APR_ASSERT(apr_brigade_to_iovec(output,vec,&n));
      st = apr_socket_sendv(state->sock,vec,n,&written);
      assert(st == APR_SUCCESS || st == APR_EAGAIN);
      nbytes -= written;
      if (nbytes > 0 && written > 0) {
        apr_bucket_brigade *m = more;
        more = NULL;
        if(written) {
          AEB_APR_ASSERT(apr_brigade_partition(output,written-1,&b));
        } else
          b = NULL;

        if(b != NULL)
          assert((more = apr_brigade_split_ex(output,b,more)) != NULL);

        if(m) {
          if(more)
            APR_BRIGADE_CONCAT(more,m);
          else
            more = m;
        }
      } else if (written == 0 && output) {
        assert(more == NULL);
        more = output;
        output = NULL;
      }
      if(output)
        apr_brigade_cleanup(output);
    }
    if(more && !last) {
      assert(state->wpending == NULL);
      state->wpending = more;
      more = NULL;
      break;
    }
  }

  if((bb && !APR_BRIGADE_EMPTY(bb)) || 
            (state->wpending && !APR_BRIGADE_EMPTY(state->wpending))) {
    state->events |= EV_WRITE;
    st = aeb_assign(info->event,NULL,-1,state->events,NULL,NULL);
    if (!IS_EVENT_ADDED(info->event))
      internal_event_add(info->event);
  }

  if(output)
    apr_brigade_destroy(output);
#ifdef AEB_USE_THREADS
  } while(0);
  AEB_APR_ASSERT(apr_thread_mutex_unlock(state->write_mutex));
#endif


  return (APR_STATUS_IS_EAGAIN(st) ? APR_SUCCESS : st);
}

static apr_status_t aeb_assign_From_socket(aeb_event_t *ev, apr_socket_t *s, short eventset)
{
  evutil_socket_t fd = -1;
  apr_status_t st = APR_SUCCESS;

  if(!s)
    s = AEB_BRIGADE_EVENT_INFO(ev);

  assert(s != NULL);
  st = apr_os_sock_get(&fd,s);
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
    assert(event_assign(ev->event,base,fd,eventset,callback,callback_arg) == 0);
  }
  return st;
}

AEB_API(apr_status_t) aeb_brigade_create_ex(apr_pool_t *pool,
                                            aeb_event_callback_fn callback,
                                            apr_socket_t *s,
                                            apr_uint32_t flags,
                                            apr_interval_time_t timeout,
                                            aeb_event_t **evp)

{
  apr_status_t st = APR_SUCCESS;
  apr_pool_t *p = NULL;
  aeb_event_t *ev;
  aeb_brigade_info *b;

  if(!evp)
    return APR_EINVAL;

  if (pool == NULL && s != NULL)
    pool =apr_socket_pool_get(s);
  if(pool == NULL)
    return APR_EINVAL;

  ev = aeb_event_new(pool,callback,(timeout >= 0 ? &timeout : NULL));
  ev->flags |= (((apr_uint16_t)flags) & 0xff00) | AEB_FLAG_SUBPOOL;
  ev->type = AEB_BRIGADE_EVENT;
  if(timeout >= 0)
    ev->flags |= AEB_TIMER_EVENT;

  if(aeb_dispatch_table[ev->type] == NULL)
    assert(aeb_event_dispatcher_register(ev->type,dispatch_brigade,NULL,NULL) == APR_SUCCESS);

  AEB_APR_ASSERT(apr_pool_create(&p,pool));
  AEB_APR_ASSERT(aeb_event_associate_pool(ev,p));
  b = apr_pcalloc(p,sizeof(aeb_brigade_info_t));
  assert(b != NULL);
  b->p = p;
  b->flags = flags;
  b->s = s;
  b->events = 0;
#ifdef AEB_USE_THREADS
  AEB_APR_ASSERT(apr_thread_mutex_create(&b->read_mutex,APR_THREAD_MUTEX_DEFAULT,b->p));
  AEB_APR_ASSERT(apr_thread_mutex_create(&b->write_mutex,APR_THREAD_MUTEX_DEFAULT,b->p));
#endif
  AEB_BRIGADE_EVENT_DATA(ev) = b;
  if (s) {
    if(callback)
      b->events |= EV_READ;
    st = aeb_assign_from_socket(s,b->events);
  }
  if(st == APR_SUCCESS)
    *evp = ev;
  return st;
}

static apr_status_t cleanup_brigade_event(void *d)
{
  aeb_brigade_info_t **bp = (aeb_brigade_info_t**)d;

  if(bp && *bp) {
    aeb_brigade_info_t *b = *bp;
    *bp = NULL;
    if(b->read_bb)
      apr_brigade_destroy(b->read_bb);
    if(b->write_bb)
      apr_brigade_destroy(b->write_bb);
    if(b->rpending)
      apr_brigade_destroy(b->rpending);
    if(b->wpending)
      apr_brigade_destroy(b->wpending);
    b->read_bb = b->write_bb = b->rpending = b->wpending = NULL;
    b->s = NULL;
  }

  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_brigade_socket_set(aeb_event_t *ev, apr_socket_t *s)
{
  apr_status_t st = APR_SUCCESS;
  apr_pool_t *p = NULL;
  apr_uint16_t flags;
  aeb_brigrade_info_t *b = NULL;
  int reset_cleanup = 0;

  if(!ev)
    return APR_EINVAL;

  flags = ev->flags;
  if(s)
    p = apr_socket_pool_get(s);

  if(AEB_EVENT_IS_BRIGADE(ev)) {
    b = AEB_BRIGADE_EVENT_DATA(ev);

    if(b && b->p && (b->p != p || b->s != s)) {
      apr_pool_cleanup_kill(p,&APR_BRIGADE_EVENT_DATA(ev),(cleanup_fn)cleanup_brigade_event);
      reset_cleanup++;
    }
    if(flags & AEB_EVENT_ADDED)
      internal_event_del(ev);
  }

  if(!b) {
    if(!p)
      AEB_APR_ASSERT(apr_pool_create(&p,ev->pool));

    b = pcalloc(p,sizeof(aeb_brigade_info_t));
    b->p = p;
    b->flags = ev->flags;
    b->s = s;
  } else {
    if (s != b->s) {
      if(b->read_bb)
        apr_brigade_destroy(b->read_bb);
      if(b->write_bb)
        apr_brigade_destroy(b->write_bb);
      if(b->rpending)
        apr_brigade_destroy(b->rpending);
      if(b->wpending)
        apr_brigade_destroy(b->wpending);
      b->read_bb = b->write_bb = b->rpending = b->wpending = NULL;
    }
    b->s = s;
  }

  ev->type = AEB_BRIGADE_EVENT;
  if(b) {
    apr_pool_cleanup_register(b->p,&APR_BRIGADE_EVENT_DATA(ev),cleanup_brigade_event,
                                                               apr_pool_cleanup_null);

    if(!b->read_bb || !b->write_bb) {
      if(!b->read_bb)
        b->read_bb = apr_brigade_create(b->p,apr_bucket_alloc_create(b->p));
      if(!b->write_bb)
        b->write_bb = apr_brigade_create(b->p,apr_bucket_alloc_create(b->p));
  }
  AEB_BRIGADE_EVENT_DATA(ev) = b;

  if(b && b->s) {
    if(aeb_assign_From_socket(ev,NULL) == APR_SUCCESS &&
          (flags & AEB_EVENT_ADDED) && !(ev->flags & AEB_EVENT_ADDED))
    internal_event_add(ev);
  }
  return st;
}

AEB_API(apr_status_t) aeb_brigade_socket_get(const aeb_event_t *ev, apr_socket_t **sp)
{
  apr_status_t st = APR_SUCCESS;

  if(!ev || !sp || !AEB_EVENT_IS_BRIGADE(ev))
    return APR_EINVAL;

  *sp = AEB_BRIGADE_EVENT_DATA(ev)->s;
  return st;
}

AEB_API(apr_status_t) aeb_event_bucket_brigade_get(const aeb_event_t *ev,
                                               apr_bucket_brigade **read_bbp,
                                               apr_bucket_brigade **write_bbp)
{
  if(!ev || !sp || !AEB_EVENT_IS_BRIGADE(ev))
    return APR_EINVAL;

  if(read_bbp)
    *read_bbp = AEB_BRIGADE_EVENT_DATA(ev)->read_bb;
  if(write_bbp)
    *write_bbp = AEB_BRIGADE_EVENT_DATA(ev)->write_bb;
  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_brigade_flush(aeb_event_t *ev)
{
  aeb_brigade_info_t *b;
  apr_bucket_t *flush;
  if(!ev || !AEB_EVENT_IS_BRIGADE(ev))
    return APR_EINVAL;

  b = AEB_BRIGADE_EVENT_DATA(ev);
  if(!b || !b->write_bb || !b->s)
    return APR_EINVAL;

#ifdef AEB_USE_THREADS
  assert(apr_thread_mutex_lock(b->write_mutex) == APR_SUCCESS);
#endif
  assert((flush = apr_bucket_flush_create(b->write_bb->bucket_alloc)) != NULL);
  APR_BRIGADE_INSERT_TAIL(b,flush);
#ifdef AEB_USE_THREADS
  assert(apr_thread_mutex_unlock(b->write_mutex) == APR_SUCCESS);
#endif

  assert(aeb_event_del(ev) == APR_SUCCESS);
  b->events |= EV_WRITE;
  assert(aeb_assign(ev,NULL,-1,b->events,aeb_event_dispatcher,ev) == APR_SUCCESS);
  assert(aeb_event_add(ev) == APR_SUCCESS);
  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_brigade_close(const aeb_event_t *ev)
{
  aeb_brigade_info_t *b;
  apr_bucket_t *flush;
  apr_interval_time_t instant = APR_TIME_C(1);

  if(!ev || !AEB_EVENT_IS_BRIGADE(ev))
    return APR_EINVAL;

  b = AEB_BRIGADE_EVENT_DATA(ev);
  if(!b || !b->write_bb || !b->s)
    return APR_EINVAL;

#ifdef AEB_USE_THREADS
  assert(apr_thread_mutex_lock(b->write_mutex) == APR_SUCCESS);
#endif
  assert((flush = apr_bucket_eos_create(b->write_bb->bucket_alloc)) != NULL);
  APR_BRIGADE_INSERT_TAIL(b,flush);
#ifdef AEB_USE_THREADS
  assert(apr_thread_mutex_unlock(b->write_mutex) == APR_SUCCESS);
#endif

  assert(aeb_event_del(ev) == APR_SUCCESS);
  assert(aev_event_timeout_set(ev,&instant));
  b->events |= EV_WRITE;
  assert(aeb_assign(ev,NULL,-1,b->events,aeb_event_dispatcher,ev) == APR_SUCCESS);
  assert(aeb_event_add(ev) == APR_SUCCESS);
  return APR_SUCCESS;
}

AEB_API(apr_status_t) aeb_brigade_read_enable(aeb_event_t *ev, int enabled)
{
  apr_status_t st = APR_SUCCESS;
  aeb_brigade_info_t *b;
  evutil_socket_t fd = -1;
  apr_uint16_t flags;

  if(!ev || !AEB_EVENT_IS_BRIGADE(ev))
    return APR_EINVAL;

  b = AEB_BRIGADE_EVENT_DATA(ev);
  if(!b || !b->read_bb || !b->s)
    return APR_EINVAL;

  AEB_APR_ASSERT(apr_os_sock_get(&fd,s));

  flags = ev->flags;
  if(IS_EVENT_ADDED(ev))
    internal_event_del(ev);

  if(enabled)
    b->events |= EV_READ;
  else
    b->events &= ~EV_READ;

  AEB_APR_ASSERT(aeb_assign(ev,NULL,fd,b->events,aeb_event_dispatcher,ev));
  if((b->events & (EV_WRITE|EV_READ)) != 0 || (ev->flags & AEB_FLAG_TIMEOUT))
    AEB_APR_ASSERT(aeb_event_add(ev));

  return st;
}

AEB_API(apr_status_t) aeb_brigade_write_enable(aeb_event_t *ev, int enabled)
{
  apr_status_t st = APR_SUCCESS;
  aeb_brigade_info_t *b;
  evutil_socket_t fd = -1;
  apr_uint16_t flags;

  if(!ev || !AEB_EVENT_IS_BRIGADE(ev))
    return APR_EINVAL;

  b = AEB_BRIGADE_EVENT_DATA(ev);
  if(!b || !b->write_bb || !b->s)
    return APR_EINVAL;

  AEB_APR_ASSERT(apr_os_sock_get(&fd,s));

  flags = ev->flags;
  if(IS_EVENT_ADDED(ev))
    internal_event_del(ev);

  if(enabled)
    b->events |= EV_WRITE;
  else
    b->events &= ~EV_WRITE;

  AEB_APR_ASSERT(aeb_assign(ev,NULL,fd,b->events,aeb_event_dispatcher,ev));
  if((b->events & (EV_WRITE|EV_READ)) != 0 || (ev->flags & AEB_FLAG_TIMEOUT))
    AEB_APR_ASSERT(aeb_event_add(ev));

  return st;
}