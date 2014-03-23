#include "internal.h"

#include <apr_atomic.h>

#ifdef HAVE_EVENT2_EVENT_STRUCT_H
#include <event2/event_struct.h>
#endif

enum aeb_loop_event_type {
  aeb_le_idle = 0,
  aeb_le_timeout,
  aeb_le_interval,
  aeb_le_exit,
  aeb_le_thread_abort,
  aeb_le_return_data,
  aeb_le_return_status
};

typedef struct {
  struct event *ev;
  enum aeb_loop_event_type t;
  bool running,fired;
  union {
    void *generic;
    apr_status_t status;
    apr_int64_t code;
    apr_interval_time_t time;
#ifdef AEB_USE_THREADS
    apr_thread_t *thread;
#endif
  } v;
} aeb_loop_event_t;

#ifdef AEB_USE_THREADS
#include <apr_thread_proc.h>
#include <apr_thread_mutex.h>

static apr_threadkey_t *loop_event_key = NULL;
#else /* !AEB_USE_THREADS */
static aeb_loop_event_t *loop_event = NULL;
#endif

static volatile apr_uint32_t initialized = 0;
static apr_pool_t *loop_event_pool = NULL;

static inline apr_size_t get_struct_event_size(void)
{
  apr_size_t event_sz = event_get_struct_event_size();
#ifdef HAVE_EVENT2_EVENT_STRUCT_H
  return (event_sz < sizeof(struct event) ? sizeof(struct event) : event_sz);
#else
  return event_sz;
#endif
}

static inline struct event *pcalloc_event(apr_pool_t *pool)
{
  apr_size_t event_sz = event_get_struct_event_size();
  struct event *ev;
#ifdef HAVE_EVENT2_EVENT_STRUCT_H
  ev = (struct event*)apr_pcalloc(pool,event_sz < sizeof(struct event) ?
                                     sizeof(struct event) : event_sz);
#else
  ev = (struct event*)apr_pcalloc(pool,event_sz);
#endif
  event_base_set(aeb_event_base(),ev);
  return ev;
}

static inline void clear_event(struct event *ev, struct event_base *base)
{
  ASSERT(ev != NULL);
  if(!base)
    base = event_get_base(ev);
  memset(ev,0,get_struct_event_size());
  if(base)
    event_base_set(base,ev);
}

#ifdef AEB_USE_THREADS
static void cleanup_loop_event_tls(void *data)
{
  aeb_loop_event_t *current = (aeb_loop_event_t*)data;

  if(current && current->ev) {
    if(event_initialized(current->ev))
      event_del(current->ev);
    current->ev = NULL;
  }
}

static void init_aeb_loops(void)
{
  ASSERT(loop_event_key == NULL);
  ASSERT(loop_event_pool != NULL);
  ASSERT(apr_threadkey_private_create(&loop_event_key,
                                      cleanup_loop_event_tls,
                                      loop_event_pool) == APR_SUCCESS);
  apr_atomic_inc32(&initialized);
}

static void possibly_init_aeb_loops(void)
{
  apr_status_t st;
  static apr_thread_once_t *init_once = NULL;
  static volatile apr_uint32_t spinlock = 0;

  while(apr_atomic_cas32(&spinlock,1,0) != 0)
    ;
  if (init_once == NULL) {
    AEB_APR_ASSERT(apr_pool_create_unmanaged(&loop_event_pool));
    AEB_APR_ASSERT(apr_thread_once_init(&init_once,loop_event_pool));
  }
  assert(apr_atomic_dec32(&spinlock) == 0);
  AEB_APR_ASSERT(apr_thread_once(init_once,init_aeb_loops));
}
#else /* !AEB_USE_THREADS */
static void init_aeb_loops(void)
{
  apr_status_t st;

  if(loop_event == NULL) {
    if(loop_event_pool == NULL)
      AEB_APR_ASSERT(apr_pool_create_unmanaged(&loop_event_pool));
    loop_event = apr_pcalloc(loop_event_pool,sizeof(aeb_loop_event_t));
    ASSERT(loop_event != NULL);
    loop_event->ev = pcalloc_event(loop_event_pool);
    apr_atomic_inc32(&initialized);
  }
}
#define possibly_init_aeb_loops init_aeb_loops
#endif /* AEB_USE_THREADS */

/* return the active managment loop data structure */
static aeb_loop_event_t *get_loop_event(void)
{
#ifdef AEB_USE_THREADS
  apr_status_t st;
  aeb_loop_event_t *lev = NULL;

  AEB_APR_ASSERT(apr_threadkey_private_get((void**)&lev,loop_event_key));
  return lev;
#else /* !AEB_USE_THREADS */
  if(!loop_event)
    init_aeb_loops();
  return loop_event;
#endif
}

static void set_loop_event(aeb_loop_event_t *lev)
{
#ifdef AEB_USE_THREADS
  aeb_loop_event_t *current = get_loop_event();
  bool running = (current ? current->running : 0);

  if(current && current->ev && event_initialized(current->ev))
    event_del(current->ev);

  if(lev)
    lev->running = running;
  ASSERT(apr_threadkey_private_set(lev,loop_event_key) == APR_SUCCESS);
#else /* !AEB_USE_THREADS */
  if(loop_event && loop_event->ev && event_initialized(loop_event->ev))
    event_del(loop_event->ev);
  loop_event->running = lev->running;
  loop_event = lev;
#endif
}

static apr_status_t clear_loop_event(void *data)
{
  aeb_loop_event_t *current = (aeb_loop_event_t*)data;
#ifdef AEB_USE_THREADS
  ASSERT(apr_threadkey_private_set(NULL,loop_event_key) == APR_SUCCESS);
#else
  loop_event = NULL;
#endif
  if(current && current->ev && event_initialized(current->ev)) {
    event_del(current->ev);
    memset(current->ev,0,get_struct_event_size());
  }
  return APR_SUCCESS;
}

static void event_loop_generic_cb(evutil_socket_t s, short flags, void *v)
{
  aeb_loop_event_t *lev = (aeb_loop_event_t*)v;

  ASSERT(lev != NULL);

  lev->fired++;
  assert(event_base_loopbreak(event_get_base(lev->ev)) == 0);
}

static void event_loop_timeout_cb(evutil_socket_t s, short flags, void *v)
{
  aeb_loop_event_t *lev = (aeb_loop_event_t*)v;

  ASSERT(lev != NULL);
  if(!lev->fired && (lev->t == aeb_le_timeout ||
                     lev->t == aeb_le_interval)) {
    apr_time_t now = apr_time_now();
    if (lev->v.time < now)
      lev->v.time = now - lev->v.time;
  }
  lev->fired++;
  assert(event_base_loopbreak(event_get_base(lev->ev)) == 0);
}

/* high-level event_loop processing */
AEB_INTERNAL(apr_status_t) aeb_run_event_loop(apr_interval_time_t *timeout,
                                              apr_interval_time_t *duration,
                                              apr_ssize_t *counter,
                                              int nonblocking)
{
  apr_status_t st = APR_SUCCESS;
  aeb_loop_event_t *lev = NULL;
  apr_pool_t *tpool = NULL;
  int flags = 0;
  apr_ssize_t total = -1;
  apr_os_imp_time_t ostime;
  apr_time_t start_time = apr_time_now();
  struct event_base *base = aeb_event_base();
  apr_interval_time_t elapsed = APR_TIME_C(0);
  apr_os_imp_time_t *tv = &ostime;

  if(apr_atomic_read32(&initialized) == 0)
    possibly_init_aeb_loops();

  if((lev = get_loop_event()) == NULL) {
    ASSERT((tpool = aeb_thread_static_pool_acquire()) != NULL);
    ASSERT((lev = apr_pcalloc(tpool,sizeof(aeb_loop_event_t))) != NULL);
    ASSERT((lev->ev = pcalloc_event(tpool)) != NULL);
    set_loop_event(lev);
    apr_pool_pre_cleanup_register(tpool,lev,clear_loop_event);
  }

  if(event_initialized(lev->ev)) {
      event_del(lev->ev);
    clear_event(lev->ev,base);
  } else lev->fired = 0;

  ASSERT(base != NULL);
  if(nonblocking)
    flags |= EVLOOP_NONBLOCK;
  if(counter) {
    total = *counter;
    *counter = 0;
    flags |= EVLOOP_ONCE;
  }

  if(timeout && tv) {
    lev->t = aeb_le_timeout;
    lev->v.time = start_time;
    AEB_APR_ASSERT(apr_os_imp_time_get(&tv,timeout));
    AEB_ZASSERT(event_assign(lev->ev,base,-1,0,event_loop_timeout_cb,lev));
    AEB_ZASSERT(event_priority_set(lev->ev,1));
    AEB_ZASSERT(event_add(lev->ev,tv));
  }

  for(lev->running = 1; st == APR_SUCCESS && (total == -1 || total > 0); ) {
    int rc;

    rc = event_base_loop(base,flags);
    elapsed = apr_time_now() - start_time;

    if(rc < 0) {
      st = apr_get_os_error();
      break;
    } else {
      if(event_base_got_exit(base) || event_base_got_break(base))
        ASSERT(lev && lev->fired);
      else if(counter) {
        (*counter)++;
        if(rc >= 0 && total > 0)
          total--;
      }
    }

#if 0
    if(tv) {
      ASSERT(apr_os_imp_time_get(&tv,&elapsed) == APR_SUCCESS);
      printf("DEBUG: tv.tv_sec=%lu tv.tv_usec=%lu\n",(unsigned long)tv->tv_sec,
                                                     (unsigned long)tv->tv_usec);
    }
#endif

    if(lev && lev->fired) {
      unsigned done = 0;
      lev->fired--;

      switch(lev->t) {
      case aeb_le_return_status:
        st = lev->v.status;
        done++;
        break;
      case aeb_le_interval:
      case aeb_le_timeout:
        if(st == APR_SUCCESS) {
          st = APR_TIMEUP;
        }
        done++;
        break;
      case aeb_le_return_data:
      case aeb_le_exit:
        done++;
        break;
      default:
        AEB_ASSERTV(lev == NULL,"unsupported loop event type %d",(int)lev->t);
        break;
      }
      if(done) break;
    }
  }

  if(lev->running)
    lev->running = 0;

  if(duration)
    *duration = elapsed;

  if(tpool)
    aeb_thread_static_pool_release();
  return st;
}

AEB_API(apr_status_t) aeb_event_loop_return_status(apr_status_t rst)
{
  aeb_loop_event_t *lev = NULL;
  struct event_base *base = NULL;
  if(apr_atomic_read32(&initialized) == 0)
    possibly_init_aeb_loops();

  if((lev = get_loop_event()) == NULL)
    return APR_EINVAL;

  ASSERT(lev->ev != NULL);
  if(event_initialized(lev->ev) && !lev->fired) {
    event_del(lev->ev);
    base = event_get_base(lev->ev);
  }

  AEB_ASSERT(lev->fired == 0,"cannot nest event loop operations (returns, etc)");
  lev->fired++;
  lev->t = aeb_le_return_status;
  lev->v.status = rst;

  event_base_loopbreak(base ? base : aeb_event_base());
  return APR_SUCCESS;
}

/* aeb_event_loop_terminate() is just a call to aeb_event_loop_return_status
 * that always returns APR_EINTR (for now).
 */
AEB_API(apr_status_t) aeb_event_loop_terminate(void)
{
  return aeb_event_loop_return_status(APR_EINTR);
}

AEB_API(bool) aeb_event_loop_isrunning(void)
{
  aeb_loop_event_t *lev = NULL;

  if(apr_atomic_read32(&initialized) == 0)
    possibly_init_aeb_loops();
  else if((lev = get_loop_event()) != NULL)
    return lev->running;

  return 0;
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

AEB_API(apr_status_t) aeb_event_timed_loopn(apr_ssize_t count, apr_interval_time_t *duration)
{
  return aeb_run_event_loop(NULL,duration,&count,0);
}

AEB_API(apr_status_t) aeb_event_timeout_loopn(apr_ssize_t count, apr_interval_time_t *duration)
{
  apr_interval_time_t t;
  apr_interval_time_t *timeout = NULL;
  if(duration) {
    t = *duration;
    timeout = &t;
  }

  return aeb_run_event_loop(timeout,duration,&count,0);
}

AEB_API(apr_status_t) aeb_event_loopn(apr_ssize_t count)
{
  return aeb_run_event_loop(NULL,NULL,&count,0);
}

AEB_API(apr_status_t) aeb_event_loop_try(apr_interval_time_t *duration)
{
  return aeb_run_event_loop(NULL,duration,NULL,1);
}
