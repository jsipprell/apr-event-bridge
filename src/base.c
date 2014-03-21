#include "internal.h"
#include "util.h"

#define HEXFMT "0x%" APR_UINT64_T_HEX_FMT
#define HEXFMT32 "0x%08" APR_UINT64_T_HEX_FMT
#define HEXFMT64 "0x%016" APR_UINT64_T_HEX_FMT
#define HEX(v) ((apr_uint64_t)(v) & 0xffffffff)
#define HEXL(v) ((apr_uint64_t)(v))

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
                                      "cannot lock registry_mutex")
#define UNLOCK_BASE_REGISTRY AEB_ASSERT(apr_thread_mutex_unlock(registry_mutex) == APR_SUCCESS, \
                                      "cannot unlock registry_mutex")
#else /* !AEB_USE_THREADS */

static struct event_base *base = NULL;

#endif /* AEB_USE_THREADS */

static apr_pool_t *aeb_base_pool = NULL;

#ifdef AEB_USE_THREADS
static apr_thread_mutex_t *base_pool_mutex = NULL;

static apr_pool_t *get_thread_pool(const char **tag)
{
  apr_status_t st;
  apr_pool_t *pool = NULL;
  static apr_uint32_t tc = 1;

  if (apr_threadkey_private_get((void**)&pool,aeb_pool_thread_key) != APR_SUCCESS || pool == NULL) {
    const char *new_tag;

    ASSERT(aeb_base_pool != NULL);
    AEB_APR_ASSERT(apr_thread_mutex_lock(base_pool_mutex));
    AEB_APR_ASSERT(apr_pool_create(&pool,aeb_base_pool));
    AEB_APR_ASSERT(apr_thread_mutex_unlock(base_pool_mutex));

    ASSERT((new_tag = apr_psprintf(pool,"thread 0x%x",tc++)) != NULL);
    if(tag) *tag = new_tag;
    apr_pool_tag(pool,new_tag);
    AEB_APR_ASSERT(apr_pool_userdata_set(new_tag,"aeb:thread_tag",NULL,pool));
    AEB_APR_ASSERT(apr_threadkey_private_set(pool,aeb_pool_thread_key));
  } else if(tag) {
    AEB_APR_ASSERT(apr_pool_userdata_get((void**)tag,"aeb:thread_tag",pool));
  }

  return pool;
}

static apr_thread_t *get_thread(apr_pool_t *pool, const char **tag)
{
  apr_status_t st;
  apr_os_thread_t ost;
  apr_thread_t *t = NULL;

  if (apr_threadkey_private_get((void**)&t,aeb_thread_key) != APR_SUCCESS || t == NULL) {
    if (pool == NULL)
      pool = get_thread_pool(tag);
    ost = apr_os_thread_current();
    AEB_APR_ASSERT(apr_os_thread_put(&t,&ost,pool));
    ASSERT(t != NULL);
  } else if(tag)
    get_thread_pool(tag);

  return t;
}

static apr_status_t unregister_thread_base(void *data)
{
  apr_os_thread_t *key = (apr_os_thread_t*)data;
  apr_thread_t *t = NULL;
  apr_pool_t *pool = AEB_GLOBAL_STATIC_POOL_ACQUIRE()
  ASSERT(key != NULL);
  ASSERT(pool != NULL);
  apr_os_thread_put(&t,key,pool);
  LOCK_BASE_REGISTRY;
  apr_hash_set(aeb_base_registry,key,sizeof(apr_os_thread_t),NULL);
#if 0
  printf("------------> THREAD " HEXFMT " UNREGISTERED BASE (%d)\n",HEX(t),
                                                  apr_hash_count(aeb_base_registry));
#endif
  UNLOCK_BASE_REGISTRY;
  AEB_GLOBAL_STATIC_POOL_RELEASE()

  return APR_SUCCESS;
}

static void register_thread_base(apr_thread_t *t,struct event_base *base, apr_pool_t *pool)
{
  apr_status_t st;
  apr_os_thread_t *key,*ost = NULL;
  if (!pool)
    pool = apr_thread_pool_get(t);

  ASSERT(t != NULL);
  AEB_APR_ASSERT(apr_os_thread_get(&ost,t));
  key = apr_pmemdup(pool,ost,sizeof(apr_os_thread_t));
  ASSERT(key != NULL);
  LOCK_BASE_REGISTRY;
  apr_hash_set(aeb_base_registry,key,sizeof(apr_os_thread_t),base);

  apr_pool_cleanup_register(pool,key,unregister_thread_base,apr_pool_cleanup_null);
#if 0
  printf("------------> THREAD " HEXFMT " REGISTERED BASE " HEXFMT " (%d)\n",HEX(t),HEX(base),
                                                apr_hash_count(aeb_base_registry));
#endif
    UNLOCK_BASE_REGISTRY;
}

static struct event_base *lookup_thread_base(apr_thread_t *t)
{
  apr_status_t st;
  struct event_base *base;
  apr_os_thread_t *key;
  if (t == NULL)
    t = get_thread(NULL,NULL);
  AEB_APR_ASSERT(apr_os_thread_get(&key,t));
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
  const char *tag = "??";
  aeb_memblock_t *block;
#ifdef AEB_USE_THREADS
  pool = get_thread_pool(&tag);
#else
  pool = aeb_base_pool;
#endif

#ifdef AEB_DEBUG_EVENT_MM
  printf( HEXFMT64 "/%s/AEB_EVENT_MALLOC (" HEXFMT ")\n",HEXL(pool),tag,HEX(sz));
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
  const char *tag = "??";
  aeb_memblock_t *block = NULL;
  apr_pool_t *pool = NULL;
  apr_size_t copy_size = 0;
#ifdef AEB_USE_THREADS
  pool = get_thread_pool(&tag);
  ASSERT(pool != NULL);
#else
  pool = aeb_base_pool;
#endif

#ifdef AEB_DEBUG_EVENT_MM
  printf(HEXFMT64"/%s/AEB_EVENT_REALLOC (" HEXFMT ")\n",HEXL(pool),tag,HEX(sz));
  fflush(stdout);
#endif

  if(ptr) {
    block = (aeb_memblock_t*)(((char*)ptr)-sizeof(aeb_memblock_t));
    AEB_ASSERTV(block->magic == AEB_MEMBLOCK_MAGIC,
                "invalid aeb memory block during realloc at 0x%" APR_UINT64_T_HEX_FMT,
                (apr_uint64_t)ptr);
    copy_size = (sz < block->alloc_size ? sz : block->alloc_size);
  }

  block = apr_palloc(pool,sz+sizeof(aeb_memblock_t));
  ASSERT(block != NULL);
  block->magic = AEB_MEMBLOCK_MAGIC;
  block->alloc_size = (apr_size_t)sz;
  newptr = ((char*)block) + sizeof(aeb_memblock_t);
  if (copy_size)
    memmove(newptr,ptr,copy_size);
  return newptr;
}

static void aeb_event_free(void *ptr)
{
  aeb_memblock_t *block = NULL;

  if(ptr) {
    block = (aeb_memblock_t*)(((char*)ptr) - sizeof(aeb_memblock_t));
#ifdef AEB_DEBUG_EVENT_MM
    {
      const char *tag = "??";
      apr_pool_t *pool = get_thread_pool(&tag);

      printf(HEXFMT64"/%s/AEB_EVENT_FREE (" HEXFMT ")\n",HEXL(pool),tag,HEX(block->alloc_size));
    }
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
    AEB_ASSERTV(block->alloc_size > 0,
          "double free of aeb_event_base memory at 0x" HEXFMT ".\n",HEX(ptr));
    block->alloc_size = 0;
  }
}

#ifdef AEB_USE_THREADS
static apr_status_t cleanup_event_base(void *data)
{
  const char *tag = "??";
  struct event_base *base = (struct event_base*)data;
  apr_pool_t *pool = get_thread_pool(&tag);
  if(base) {
#if 0
    printf(HEXFMT64"/%s/DESTROY EVENT BASE " HEXFMT "\n",
          HEXL(pool),tag,HEX(base));
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
  ASSERT(pool != NULL);

#if 0
  fprintf(stderr,"destroying apr pool (" HEXFMT ") after thread death\n",HEX(pool));
#endif
  apr_pool_destroy((apr_pool_t*)pool);
}

static apr_status_t reinit_event_base(void *data)
{
  apr_status_t st = APR_SUCCESS;
  struct event_base *base = (struct event_base*)data;

  ASSERT(base != NULL);
  AEB_ERRNO_CALL(event_reinit(base));
  fprintf(stderr,"reinit base " HEXFMT "\n",HEX(base));
  return st;
}

static void init_aeb_threading(void)
{
  apr_status_t st;

#ifdef EVTHREAD_USE_PTHREADS_IMPLEMENTED
  evthread_use_pthreads();
#elif defined(EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED)
  evthread_use_windows_threads();
#endif

  if(aeb_base_pool == NULL) {
    AEB_APR_ASSERT(apr_pool_create_unmanaged(&aeb_base_pool));
  }
  AEB_APR_ASSERT(apr_threadkey_private_create(&aeb_base_thread_key,NULL,aeb_base_pool));
  /* pool cleanup below takes care of this for us */
  AEB_APR_ASSERT(apr_threadkey_private_create(&aeb_pool_thread_key, destroy_thread_pool,
                aeb_base_pool));
  AEB_APR_ASSERT(apr_threadkey_private_create(&aeb_thread_key, NULL,
                aeb_base_pool));
  ASSERT(aeb_base_thread_key != NULL);
  ASSERT(aeb_pool_thread_key != NULL);
  ASSERT(aeb_thread_key != NULL);

  AEB_APR_ASSERT(apr_thread_mutex_create(&registry_mutex,APR_THREAD_MUTEX_DEFAULT,
                 aeb_base_pool));
  AEB_APR_ASSERT(apr_thread_mutex_lock(registry_mutex));
  ASSERT((aeb_base_registry = apr_hash_make(aeb_base_pool)) != NULL);
  AEB_APR_ASSERT(apr_thread_mutex_create(&base_pool_mutex,APR_THREAD_MUTEX_DEFAULT,aeb_base_pool));
  event_set_mem_functions(aeb_event_malloc,aeb_event_realloc,aeb_event_free);
  AEB_APR_ASSERT(apr_thread_mutex_unlock(registry_mutex));
}
#else
static void init_aeb_non_threaded(void)
{
  apr_status_t st;

  if(aeb_base_pool == NULL) {
    AEB_APR_ASSERT(apr_pool_create_unmanaged(&aeb_base_pool));
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
  ASSERT(base != NULL);
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
  apr_status_t st;
  struct event_base *base = NULL;
  if(thread_key_init_once == NULL) {
    ASSERT(aeb_base_pool == NULL);
    AEB_APR_ASSERT(apr_pool_create_unmanaged(&aeb_base_pool));
    AEB_APR_ASSERT(apr_thread_once_init(&thread_key_init_once,aeb_base_pool));
  }

  AEB_APR_ASSERT(apr_thread_once(thread_key_init_once,init_aeb_threading));
  ASSERT(aeb_base_thread_key != NULL);
  ASSERT(aeb_thread_key != NULL);
  ASSERT(base_pool_mutex != NULL);

  if (apr_threadkey_private_get((void**)&base,aeb_base_thread_key) != APR_SUCCESS || base == NULL) {
    apr_pool_t *pool = NULL;
    apr_thread_t *thread = NULL;

    /* NOTE: pool is not created until first allocation actually performed */
    ASSERT((base = event_base_new()) != NULL);
    pool = get_thread_pool(NULL);
    ASSERT(pool != NULL);
    thread = get_thread(pool,NULL);
    apr_pool_cleanup_register(pool,base,
                              cleanup_event_base,
                              reinit_event_base);
    AEB_APR_ASSERT(apr_threadkey_private_set(base,aeb_base_thread_key));
    AEB_APR_ASSERT(apr_threadkey_private_set(thread,aeb_thread_key));

    AEB_ZASSERT(evthread_make_base_notifiable(base));
    register_thread_base(thread,base,pool);
  }
#else /* !AEB_USE_THREADS */
  if (base == NULL) {
    init_aeb_non_threaded();
    ASSERT((base = event_base_new()) != NULL);
  }
#endif /* AEB_USE_THREADS */
  return base;
}
