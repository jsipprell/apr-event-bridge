#include "internal.h"
#include "util.h"

#ifdef AEB_USE_THREADS

#include <apr_thread_proc.h>

static apr_threadkey_t *aeb_base_thread_key = NULL;
static apr_threadkey_t *aeb_pool_thread_key = NULL;
static apr_thread_once_t *thread_key_init_once = NULL;

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

  if(ptr) {
    block = (aeb_memblock_t*)(((char*)ptr)-sizeof(aeb_memblock_t));
    AEB_ASSERTV(block->magic == AEB_MEMBLOCK_MAGIC,
                "invalid aeb memory block during realloc at %" APR_UINT64_T_HEX_FMT,
                (apr_uint64_t)ptr);
    copy_size = block->alloc_size;
    if (sz < copy_size)
      copy_size = sz;
  }

  block = apr_palloc(pool,(apr_size_t)sz+sizeof(aeb_memblock_t));
  block->magic = AEB_MEMBLOCK_MAGIC;
  block->alloc_size = (apr_size_t)sz;
  newptr = ((char*)block) + sizeof(aeb_memblock_t);
  if (copy_size)
    memcpy(newptr,ptr,copy_size);
  return apr_pmemdup(pool,ptr,(apr_size_t)sz);
}

static void aeb_event_free(void *ptr)
{
  aeb_memblock_t *block = NULL;

  if(ptr)
    block = (aeb_memblock_t*)(((char*)ptr) - sizeof(aeb_memblock_t));
  if(block) {
    AEB_ASSERTV(block->magic == AEB_MEMBLOCK_MAGIC,
                "invalid aeb memory block during free at %" APR_UINT64_T_HEX_FMT,
                (apr_uint64_t)ptr);
    block->alloc_size = 0;
  }
}

#ifdef AEB_USE_THREADS
static apr_status_t cleanup_threadkey_pool(void *p)
{
  AEB_ASSERT(p != NULL,"null pool in cleanup_threadkey_pool");
  AEB_ASSERTV(p == aeb_base_pool,"aeb_base_pool mismatch, %ux != %ux",(unsigned int)p,(unsigned int)aeb_base_pool);
  aeb_base_pool = NULL;
  apr_pool_destroy((apr_pool_t*)p);
  return APR_SUCCESS;
}

static apr_status_t cleanup_event_base(void *data)
{
  struct event_base *base = (struct event_base*)data;

  if(base) {
    fprintf(stderr,"freeing event base in cleanup_event_base(), %ux\n",(unsigned)base);
    event_base_free(base);
  } else
    fprintf(stderr,"cleanup_event_base called with NULL base\n");
  return APR_SUCCESS;
}

static void thread_died(void *base)
{
  AEB_ASSERT(base != NULL,"null base");
  fprintf(stderr,"NOT freeing event base in thread_died()\n");
}

static void destroy_thread_pool(void *pool)
{
  AEB_ASSERT(pool != NULL,"null pool");

  fprintf(stderr,"destroying apr pool after thread death\n");
  apr_pool_destroy((apr_pool_t*)pool);
}

static apr_status_t reinit_event_base(void *data)
{
  struct event_base *base = (struct event_base*)data;

  AEB_ASSERT(base != NULL,"null event_base in reinit_event_base");
  AEB_ASSERT(event_reinit(base) == 0,"could not re-add all events to event base");
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
                thread_died,
                aeb_base_pool) == APR_SUCCESS,
             "apr_threadkey_private_create failure");
  AEB_ASSERT(apr_threadkey_private_create(&aeb_pool_thread_key,
                destroy_thread_pool,
                aeb_base_pool) == APR_SUCCESS,
            "apr_threadkey_private_create failed (aeb_pool_thread_key)");
  AEB_ASSERT(aeb_base_thread_key != NULL,"aeb_base_thread_key should not be NULL");
  AEB_ASSERT(aeb_pool_thread_key != NULL,"aeb_pool_thread_key should not be NULL");

  event_set_mem_functions(aeb_event_malloc,aeb_event_realloc,aeb_event_free);
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

  if (apr_threadkey_private_get((void**)&base,aeb_base_thread_key) != APR_SUCCESS || base == NULL) {
    apr_pool_t *pool;

    /* NOTE: pool is not created until first allocation actually performed */
    AEB_ASSERT((base = event_base_new()) != NULL,"event_base_new returned null");
    pool = get_thread_pool();
    AEB_ASSERT(pool != NULL,"AEB thread pool is NULL");
    apr_pool_cleanup_register(pool,base,
                              cleanup_event_base,
                              reinit_event_base);
    AEB_ASSERT(apr_threadkey_private_set(base,aeb_base_thread_key) == APR_SUCCESS,
              "apr_threadkey_private_set failure");
  }
#else /* !AEB_USE_THREADS */
  if (base == NULL) {
    init_aeb_non_threaded();
    AEB_ASSERT((base = event_base_new()) != NULL,"event_init failed");
  }
#endif /* AEB_USE_THREADS */
  return base;
}