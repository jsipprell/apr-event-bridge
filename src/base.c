#include "internal.h"
#include "util.h"

#define HEXFMT "0x%" APR_UINT64_T_HEX_FMT
#define HEX(v) ((apr_uint64_t)(v) & 0xffffffff)

#ifdef AEB_USE_THREADS

#include <apr_thread_proc.h>
#include <apr_thread_mutex.h>

static apr_threadkey_t *aeb_base_thread_key = NULL;
static apr_threadkey_t *aeb_pool_thread_key = NULL;
static apr_threadkey_t *aeb_thread_key = NULL;
static apr_thread_once_t *thread_key_init_once = NULL;
static apr_hash_t *aeb_base_registry = NULL;
static apr_thread_mutex_t *registry_mutex = NULL;

#define LOCK_BASE_REGISTRY AEB_ASSERT(apr_thread_mutex_lock(registry_mutex) == APR_SUCCESS, \
                                      "registry_mutex")
#define UNLOCK_BASE_REGISTRY AEB_ASSERT(apr_thread_mutex_unlock(registry_mutex) == APR_SUCCESS, \
                                      "registry_mutex")
#else /* !AEB_USE_THREADS */

static struct event_base *base = NULL;

#endif /* AEB_USE_THREADS */

static apr_pool_t *aeb_base_pool = NULL;

#ifdef AEB_USE_THREADS
static apr_pool_t *get_thread_pool(void)
{
  apr_pool_t *pool = NULL;

  if (apr_threadkey_private_get((void**)&pool,aeb_pool_thread_key) != APR_SUCCESS || pool == NULL) {
    AEB_ASSERT(apr_pool_create(&pool,NULL) == APR_SUCCESS,
               "apr_pool_create failure");
    AEB_ASSERT(apr_threadkey_private_set(pool,aeb_pool_thread_key) == APR_SUCCESS,
               "apr_threadkey_private_set (pool) failure");
  }

  return pool;
}

static apr_thread_t *get_thread(apr_pool_t *pool)
{
  apr_os_thread_t ost;
  apr_thread_t *t = NULL;

  if (apr_threadkey_private_get((void**)&t,aeb_thread_key) != APR_SUCCESS || t == NULL) {
    if (pool == NULL)
      pool = get_thread_pool();
    ost = apr_os_thread_current();
    AEB_ASSERT(apr_os_thread_put(&t,&ost,pool) == APR_SUCCESS,"apr_os_thread_put failure");
    AEB_ASSERT(t != NULL,"null thread");
  }

  return t;
}

static apr_status_t unregister_thread_base(void *data)
{
  apr_os_thread_t *key = (apr_os_thread_t*)data;
  apr_thread_t *t;
  apr_pool_t *pool = apr_hash_pool_get(aeb_base_registry);

  apr_os_thread_put(&t,key,pool);
  AEB_ASSERT(key != NULL,"invalid thread");
  LOCK_BASE_REGISTRY;
  apr_hash_set(aeb_base_registry,key,sizeof(apr_os_thread_t),NULL);
#if 1
  printf("------------> THREAD " HEXFMT " UNREGISTERED BASE (%d)\n",HEX(t),
                                                  apr_hash_count(aeb_base_registry));
#endif
  UNLOCK_BASE_REGISTRY;
  return APR_SUCCESS;
}

static void register_thread_base(apr_thread_t *t,struct event_base *base, apr_pool_t *pool)
{
  apr_os_thread_t *key,*ost;
  if (!pool)
    pool = apr_thread_pool_get(t);

  AEB_ASSERT(apr_os_thread_get(&ost,t) == APR_SUCCESS,"apr_os_thread_get");
  key = apr_pmemdup(pool,ost,sizeof(apr_os_thread_t));
  AEB_ASSERT(key != NULL,"apr_pmemdup");
  LOCK_BASE_REGISTRY;
  apr_hash_set(aeb_base_registry,key,sizeof(apr_os_thread_t),base);

  apr_pool_cleanup_register(pool,key,unregister_thread_base,apr_pool_cleanup_null);
#if 1
  printf("------------> THREAD " HEXFMT " REGISTERED BASE " HEXFMT " (%d)\n",HEX(t),HEX(base),
                                                apr_hash_count(aeb_base_registry));
#endif
    UNLOCK_BASE_REGISTRY;
}

static struct event_base *lookup_thread_base(apr_thread_t *t)
{
  struct event_base *base;
  apr_os_thread_t *key;
  if (t == NULL)
    t = get_thread(NULL);
  AEB_ASSERT(apr_os_thread_get(&key,t) == APR_SUCCESS,"apr_os_thread_get");
  LOCK_BASE_REGISTRY;
  base = apr_hash_get(aeb_base_registry,key,sizeof(apr_os_thread_t));
  UNLOCK_BASE_REGISTRY;
  return base;
}
#endif

#define AEB_MEMBLOCK_MAGIC APR_UINT64_C(0x34faf22a0baefb0a)
typedef struct {
  apr_uint64_t magic;
  apr_size_t alloc_size;
} aeb_memblock_t;

/* per-thread memory management, should only be used for creating event_base!
   (i.e. don't mix event_new() calles with aeb_event_create())
 */
static void *aeb_event_malloc(size_t sz)
{
  apr_pool_t *pool = NULL;
  aeb_memblock_t *block;
#ifdef AEB_USE_THREADS
  pool = get_thread_pool();
#else
  pool = aeb_base_pool;
#endif

#ifdef AEB_DEBUG_EVENT_MM
  printf("AEB_EVENT_MALLOC (" HEXFMT ")\n",HEX(sz));
  fflush(stdout);
#endif
  if((block = apr_palloc(pool,(apr_size_t)sz+sizeof(aeb_memblock_t))) != NULL) {
    block->magic = AEB_MEMBLOCK_MAGIC;
    block->alloc_size = (apr_size_t)sz;
    return ((char*)block)+sizeof(aeb_memblock_t);
  }
  return NULL;
}

static void *aeb_event_realloc(void *ptr, size_t sz)
{
  void *newptr;
  aeb_memblock_t *block = NULL;
  apr_pool_t *pool = NULL;
  apr_size_t copy_size = 0;
#ifdef AEB_USE_THREADS
  pool = get_thread_pool();
#else
  pool = aeb_base_pool;
#endif

#ifdef AEB_DEBUG_EVENT_MM
  printf("AEB_EVENT_REALLOC (" HEXFMT ")\n",HEX(sz));
  fflush(stdout);
#endif

  if(ptr) {
    block = (aeb_memblock_t*)(((char*)ptr)-sizeof(aeb_memblock_t));
    AEB_ASSERTV(block->magic == AEB_MEMBLOCK_MAGIC,
                "invalid aeb memory block during realloc at %" APR_UINT64_T_HEX_FMT,
                (apr_uint64_t)ptr);
    copy_size = block->alloc_size;
    if (sz <= copy_size)
      return ptr;
  }

  block = apr_palloc(pool,(apr_size_t)sz+sizeof(aeb_memblock_t));
  block->magic = AEB_MEMBLOCK_MAGIC;
  block->alloc_size = (apr_size_t)sz;
  newptr = ((char*)block) + sizeof(aeb_memblock_t);
  if (copy_size)
    memcpy(newptr,ptr,copy_size);
  return newptr;
}

static void aeb_event_free(void *ptr)
{
  aeb_memblock_t *block = NULL;


  if(ptr) {
    block = (aeb_memblock_t*)(((char*)ptr) - sizeof(aeb_memblock_t));
#ifdef AEB_DEBUG_EVENT_MM
    printf("AEB_EVENT_FREE (" HEXFMT ")\n",HEX(block->alloc_size));
  } else {
    printf("AEB_EVENT_FREE (0)\n");
  }
  fflush(stdout);
#else
  }
#endif
  if(block) {
    AEB_ASSERTV(block->magic == AEB_MEMBLOCK_MAGIC,
                "invalid aeb memory block during free at %" APR_UINT64_T_HEX_FMT,
                (apr_uint64_t)ptr);
    block->alloc_size = 0;
  }
}

#ifdef AEB_USE_THREADS
static apr_status_t cleanup_event_base(void *data)
{
  struct event_base *base = (struct event_base*)data;

  if(base) {
#if 0
    printf("DESTROY EVENT BASE " HEXFMT "\n",HEX(base));
#endif
    event_base_free(base);
  } else
    fprintf(stderr,"cleanup_event_base called with NULL base\n");
  return APR_SUCCESS;
}

#if 0
static void thread_died(void *base)
{
  AEB_ASSERT(base != NULL,"null base");
  fprintf(stderr,"NOT freeing event base  (" HEXFMT ") in thread_died()\n", HEX(base));
}
#endif

static void destroy_thread_pool(void *pool)
{
  AEB_ASSERT(pool != NULL,"null pool");

#if 0
  fprintf(stderr,"destroying apr pool (" HEXFMT ") after thread death\n",HEX(pool));
#endif
  apr_pool_destroy((apr_pool_t*)pool);
}

static apr_status_t reinit_event_base(void *data)
{
  struct event_base *base = (struct event_base*)data;

  AEB_ASSERT(base != NULL,"null event_base in reinit_event_base");
  AEB_ASSERT(event_reinit(base) == 0,"could not re-add all events to event base");
  fprintf(stderr,"reinit base " HEXFMT "\n",HEX(base));
  return APR_SUCCESS;
}

static void init_aeb_threading(void)
{
#ifdef EVTHREAD_USE_PTHREADS_IMPLEMENTED
  evthread_use_pthreads();
#elif defined(EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED)
  evthread_use_windows_threads();
#endif

  if(aeb_base_pool == NULL) {
    AEB_ASSERT(apr_pool_create_unmanaged(&aeb_base_pool) == APR_SUCCESS,
               "apr_pool_create_unmanaged failure");
  }
  AEB_ASSERT(apr_threadkey_private_create(&aeb_base_thread_key,
                NULL, /* pool cleanup below takes care of this for us */
                aeb_base_pool) == APR_SUCCESS,
             "apr_threadkey_private_create failure");
  AEB_ASSERT(apr_threadkey_private_create(&aeb_pool_thread_key,
                destroy_thread_pool,
                aeb_base_pool) == APR_SUCCESS,
            "apr_threadkey_private_create failed (aeb_pool_thread_key)");
  AEB_ASSERT(apr_threadkey_private_create(&aeb_thread_key,
                NULL,
                aeb_base_pool) == APR_SUCCESS,
            "apr_threadkey_private_create_failed (aeb_thread_key)");
  AEB_ASSERT(aeb_base_thread_key != NULL,"aeb_base_thread_key should not be NULL");
  AEB_ASSERT(aeb_pool_thread_key != NULL,"aeb_pool_thread_key should not be NULL");
  AEB_ASSERT(aeb_thread_key != NULL,"aeb_thread_key should not be NULL");

  AEB_ASSERT(apr_thread_mutex_create(&registry_mutex,APR_THREAD_MUTEX_DEFAULT,aeb_base_pool) == APR_SUCCESS,
             "apr_thread_mutex_create failure");
  AEB_ASSERT(apr_thread_mutex_lock(registry_mutex) == APR_SUCCESS,
              "apr_thread_mutex_lock");
  AEB_ASSERT((aeb_base_registry = apr_hash_make(aeb_base_pool)) != NULL,
            "apr_hash_mask failure");
  event_set_mem_functions(aeb_event_malloc,aeb_event_realloc,aeb_event_free);
  AEB_ASSERT(apr_thread_mutex_unlock(registry_mutex) == APR_SUCCESS,
             "apr_thread_mutex_unlock");
}
#else
static void init_aeb_non_threaded(void)
{
  if(aeb_base_pool == NULL) {
    AEB_ASSERT(apr_pool_create_unmanaged(&aeb_base_pool) == APR_SUCCESS,
               "apr_pool_create_unmanaged failed");
    event_set_mem_functions(aeb_event_malloc,aeb_event_realloc,aeb_event_free);
  }
}
#endif /* AEB_USE_THREADS */

/* return the event base for a thread, the thread *must* have already acquired
 * a base at some previous point by calling aeb_event_base(). If the requested
 * thread is NULL, this just calls aeb_event_base() to acquire/create the base
 * for the current thread. Returns APR_EINVAL if the requested thread has no
 * base.
 */
AEB_INTERNAL(apr_status_t) aeb_thread_event_base_get(apr_thread_t *t, struct event_base **base)
{
  AEB_ASSERT(base != NULL,"base");
#ifdef AEB_USE_THREADS
  if (t == NULL) {
#else
    *base = aeb_event_base();
#endif
#ifdef AEB_USE_THREADS
  } else
    *base = lookup_thread_base(t);
#endif
  return *base ? APR_SUCCESS : APR_EINVAL;
}

AEB_INTERNAL(struct event_base*) aeb_event_base(void)
{
#ifdef AEB_USE_THREADS
  struct event_base *base = NULL;
  if(thread_key_init_once == NULL) {
    AEB_ASSERT(aeb_base_pool == NULL, "aeb_base_pool should be null");

    AEB_ASSERT(apr_pool_create_unmanaged(&aeb_base_pool) == APR_SUCCESS,
               "apr_pool_create_unmanaged failure");

    AEB_ASSERT(apr_thread_once_init(&thread_key_init_once,aeb_base_pool) == APR_SUCCESS,
               "apr_thread_once_init failure");
  }

  AEB_ASSERT(apr_thread_once(thread_key_init_once,init_aeb_threading) == APR_SUCCESS,
             "apr_thread_once failure (init_aeb_threading)");
  AEB_ASSERT(aeb_base_thread_key != NULL,"aeb_base_thread_key is null");
  AEB_ASSERT(aeb_thread_key != NULL,"aeb_thread_key is null");

  if (apr_threadkey_private_get((void**)&base,aeb_base_thread_key) != APR_SUCCESS || base == NULL) {
    apr_pool_t *pool;
    apr_thread_t *thread;

    /* NOTE: pool is not created until first allocation actually performed */
    AEB_ASSERT((base = event_base_new()) != NULL,"event_base_new returned null");
    printf("CREATE EVENT BASE " HEXFMT "\n",HEX(base));
    pool = get_thread_pool();
    ASSERT(pool != NULL);
    thread = get_thread(pool);
    apr_pool_cleanup_register(pool,base,
                              cleanup_event_base,
                              reinit_event_base);
    AEB_ASSERT(apr_threadkey_private_set(base,aeb_base_thread_key) == APR_SUCCESS,
              "apr_threadkey_private_set failure (aeb_base_thread_key)");
    AEB_ASSERT(apr_threadkey_private_set(thread,aeb_thread_key) == APR_SUCCESS,
              "apr_threadkey_private_set failured (aeb_thread_key)");

    ASSERT(evthread_make_base_notifiable(base) == 0);
    register_thread_base(thread,base,pool);
  }
#else /* !AEB_USE_THREADS */
  if (base == NULL) {
    init_aeb_non_threaded();
    AEB_ASSERT((base = event_base_new()) != NULL,"event_base_new failed");
  }
#endif /* AEB_USE_THREADS */
  return base;
}