/* Generic dispatch handling */
#include "dispatch.h"
#include <apr_atomic.h>

AEB_INTERNAL(aeb_dispatch_fn) aeb_dispatch_table[] = {
  NULL,                     /* rpc type */
  NULL,                     /* descriptor type */
  NULL,                     /* timer type */
  NULL,                     /* signal type */
  NULL,                     /* brigade type */
  NULL,                     /* future use */
};

#define MAX_EVENT_TYPE sizeof(aeb_dispatch_table)

static apr_pool_t *global_pool = NULL;
#ifdef AEB_USE_THREADS
static apr_thread_mutex_t *global_mutex = NULL;
static volatile apr_uint32_t initialized = 0;

static void init_global_pool(void)
{
  apr_status_t st = APR_SUCCESS;
  if(global_mutex == NULL) {
    AEB_APR_ASSERT(apr_thread_mutex_create(&global_mutex,
                   APR_THREAD_MUTEX_DEFAULT,global_pool));
    apr_atomic_set32(&initialized,1);
  }
}

static void possibly_init_global_pool(void)
{
  apr_status_t st = APR_SUCCESS;
  static apr_thread_once_t *init_once = NULL;

  if(init_once == NULL) {
    static volatile apr_uint32_t spinlock = 0;

    while(apr_atomic_cas32(&spinlock,1,0) != 0)
      ;

    if((st = apr_pool_create_unmanaged(&global_pool)) == APR_SUCCESS)
      st = apr_thread_once_init(&init_once,global_pool);

    assert(apr_atomic_dec32(&spinlock) == 0);
    assert(st == APR_SUCCESS);
  }

  while(!apr_atomic_read32(&initialized))
    AEB_APR_ASSERT(apr_thread_once(init_once,init_global_pool));
}
#else /* !AEB_USE_THREADS */
static void init_global_pool(void)
{
  if(global_pool == NULL)
    AEB_APR_ASSERT(apr_pool_create_unmanaged(&global_pool));
}
#define possibly_init_global_pool init_global_pool
#endif /* AEB_USE_THREADS */

/* generic handling from libevent, hands off to per-event-type dispatchers
 * which in turn call user callbacks.
 */
AEB_INTERNAL(void) aeb_event_dispatcher(evutil_socket_t fd, short evflags, void *data)
{
  apr_status_t st = APR_SUCCESS;
  apr_uint16_t flags = ((evflags & 0x00ff) << 8);
  apr_pool_t *pool = NULL;
  int destroy_pool = 0;
#ifdef AEB_USE_THREADS
  int release_thread_pool = 0;
#endif
  aeb_event_t *ev = (aeb_event_t*)data;

  if(evflags & EV_READ) {
    flags |= APR_POLLIN;
    evflags &= ~EV_READ;
  }
  if(evflags & EV_WRITE) {
    flags |= APR_POLLOUT;
    evflags &= ~EV_WRITE;
  }

  if(!IS_EVENT_PERSIST(ev))
    internal_event_del(ev);

  if(ev->callback) {
    if(ev->associated_pool) {
      if(ev->flags & AEB_FLAG_SUBPOOL) {
        AEB_APR_ASSERT(apr_pool_create(&pool,ev->associated_pool));
        destroy_pool++;
      } else
        pool = ev->associated_pool;
    } else {
#ifdef AEB_USE_THREADS
      ASSERT((pool = aeb_thread_static_pool_acquire()) != NULL);
      release_thread_pool++;
      if(ev->flags & AEB_FLAG_SUBPOOL) {
        apr_pool_t *p = NULL;
        AEB_APR_ASSERT(apr_pool_create(&p,pool));
        pool = p;
        destroy_pool++;
      }
else
      AEB_APR_ASSERT(apr_pool_create(&pool,ev->pool));
      destroy_pool++;
#endif
    }

    assert((int)ev->type >= 0 && ((int)ev->type < MAX_EVENT_TYPE
                              || (int)ev->type == AEB_RESERVED_EVENT));
    if(ev->type != AEB_RESERVED_EVENT) {
      aeb_libevent_info_t libevent_info;

      assert(aeb_dispatch_table[ev->type] != NULL);
      libevent_info.fd = fd;
      libevent_info.flags = evflags;
      libevent_info.data = data;
      st = aeb_dispatch_table[ev->type](ev,flags,&libevent_info,pool);
    } else
      st = ev->callback(aeb_event_info_new(pool,ev,AEB_RESERVED_EVENT_DATA(ev),flags),
                                            ev->user_context);
    if(st != APR_SUCCESS)
      fprintf(stderr,"%s\n",aeb_errorstr(st,pool));
  }

  if(IS_EVENT_PERSIST(ev) && !IS_EVENT_ADDED(ev))
    internal_event_add(ev);

  if(destroy_pool)
    apr_pool_destroy(pool);
#ifdef AEB_USE_THREADS
  if(release_thread_pool)
    aeb_thread_static_pool_release();
#endif
}

/* register the dispatcher for a given type. if old_handler is non-NULL it will be set to the
 * old handler. if a pool is passed, a cleanup will be regisered to remove the handler
 * when the pool is destroyed.
 */
struct _closure {
  aeb_event_type_t t;
  aeb_dispatch_fn fn;
};

static apr_status_t dispatch_handler_cleanup(void *data)
{
  struct _closure *c = (struct _closure*)data;
  if (c->fn && aeb_dispatch_table[c->t] == c->fn)
    aeb_dispatch_table[c->t] = NULL;
  c->fn = NULL;
  return APR_SUCCESS;
}

AEB_INTERNAL(apr_status_t) aeb_event_dispatcher_register(aeb_event_type_t event_type,
                                                         aeb_dispatch_fn handler,
                                                         aeb_dispatch_fn *old_handler,
                                                         apr_pool_t *cont)
{
  aeb_dispatch_fn old = NULL;
  apr_status_t st;
  struct _closure *c = NULL;

  if((int)event_type < 0 || (int)event_type >= MAX_EVENT_TYPE)
    return APR_EINVAL;

  if (cont)
    c = apr_palloc(cont,sizeof(struct _closure));

#ifdef AEB_USE_THREADS
  if(!apr_atomic_read32(&initialized))
#else
  if(!global_pool)
#endif
    possibly_init_global_pool();

#ifdef AEB_USE_THREADS
  AEB_APR_ASSERT(apr_thread_mutex_lock(global_mutex));
#endif

  old = aeb_dispatch_table[event_type];
  aeb_dispatch_table[event_type] = handler;
  if(c) {
    c->t = event_type;
    c->fn = handler;
    apr_pool_cleanup_register(cont,c,dispatch_handler_cleanup,apr_pool_cleanup_null);
  }
#ifdef AEB_USE_THREADS
  AEB_APR_ASSERT(apr_thread_mutex_unlock(global_mutex));
#endif

  if(old_handler)
    *old_handler = old;
  return st;
}
