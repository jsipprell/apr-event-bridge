/* "static" memory pools:
 *
 * These pools are allocated in a static fashion (although they are still technically
 * heap allocations). They always over-allocate in 4k chunks and their memory is
 * never released until the process terminates (or thread exists in the case of
 * thread-local static pools). These come in two flavors:
 *
 * A single global static pool: This pool is protected by a recursive mutex
 * (if threading enabled) such that no single thread may hold the pool at one
 * time. When a thread is finished the pool is cleared which *doesn't* release
 * the memory to the heap but *does* make it available for other global static
 * pool users. Finally the mutex is unlocked so that other threads may make
 * use of the pool. Global static pool usage should always take the following
 * form (note the lack of braces):
 *   pool = AEB_GLOBAL_STATIC_POOL_ACQUIRE()
 *   AEB_GLOBAL_STATIC_POOL_RELEASE()
 *
 *
 * Thread-local static pools: These are localized to each thread, and behave
 * much the same way as the global version but no locking is inherently
 * required. The thread-local static pool is emptied though after each "release".
 * When the owning thread terminates the underlying allocation is really
 * released to the heap. Usage:
 *
 * pool = aeb_thread_static_pool_acquire();
 * aeb_thread_static_pool_release();
 *
 * Note that a thread may technically call aeb_thread_static_pool_acquire()
 * as many times as necessary, but the underly allocation will just continue
 * to grow until all that thread's calls to aeb_thread_static_pool_acquire()
 * are matched by the same thread calling aeb_thread_static_pool_release().
 * There is no need to worry about matching acquire/release calls at
 * the time of thread termination, however.
 */
#include "internal.h"
#include "util.h"

#include <apr_atomic.h>
#include <apr_allocator.h>
#define AEB_POOL_BLOCK_SIZE 4096 - APR_MEMNODE_T_SIZE

#ifdef AEB_USE_THREADS
#include <apr_thread_proc.h>
#include <apr_thread_mutex.h>
#endif

typedef struct {
  apr_pool_t *pool;
  apr_uint32_t refcount;
} aeb_tls_static_pool_t;

#ifdef AEB_USE_THREADS
static apr_thread_mutex_t *global_mutex = NULL;
static apr_thread_once_t *global_init_once = NULL;
static apr_pool_t *global_pool = NULL;

/* thread-local aeb_tls_static_pool_t structure is referenced
 * by the following thread key:
 */
static apr_threadkey_t *tls_pools = NULL;

#endif /* AEB_USE_THREADS */

static volatile apr_uint32_t initialized = 0;
static volatile apr_uint32_t global_pool_refs = 0;
static apr_pool_t *current_global_user_pool = NULL;

/* cleanup_thread_static_pool is called when a thread terminates.
 */
static void cleanup_thread_static_pool(void *data)
{
  apr_pool_t *pool;
  aeb_tls_static_pool_t *p = (aeb_tls_static_pool_t*)data;

  ASSERT(p != NULL && (pool = p->pool) != NULL);
  p->pool = NULL;
  apr_pool_destroy(pool);
}

static void init_global_pool(void)
{
  apr_status_t st;

#ifdef AEB_USE_THREADS
  ASSERT(global_pool != NULL);
  ASSERT(global_mutex == NULL);
  AEB_APR_ASSERT(apr_thread_mutex_create(&global_mutex,APR_THREAD_MUTEX_NESTED,
                                         global_pool));
  AEB_APR_ASSERT(apr_threadkey_private_create(&tls_pools,cleanup_thread_static_pool,
                                         global_pool));
#endif

  apr_atomic_inc32(&initialized);
}

#ifdef AEB_USE_THREADS
static inline void possibly_init_global_pool(void)
{
  /* initialization could happen at any point so this is not thread-safe
   * yet we cannot create any apr thread primmitives early on.
   */
  static volatile apr_uint32_t spinlock = 0;
  apr_status_t st;

  assert(apr_atomic_cas32(&spinlock,1,0) == 0);
    if(global_pool == NULL) {
      AEB_APR_ASSERT(apr_pool_create_unmanaged(&global_pool));
      ASSERT(global_init_once == NULL);
      AEB_APR_ASSERT(apr_thread_once_init(&global_init_once,global_pool));
    }
  assert(apr_atomic_cas32(&spinlock,0,1) == 1);
  AEB_APR_ASSERT(apr_thread_once(global_init_once,init_global_pool));
}
#else /* !AEB_USE_THREADS */
#define possibly_init_global_pool init_global_pool
#endif

static apr_status_t abort_on_early_cleanup(void *d)
{
  if(apr_atomic_read32(&global_pool_refs) > 0) {
    fprintf(stderr,"attempt to clear or destroy global static pool with refcount > 0\n");
    abort();
  }

  current_global_user_pool = NULL;
  return APR_SUCCESS;
}

AEB_INTERNAL(apr_pool_t*) aeb_global_static_pool_acquire(void)
{
  apr_status_t st;
  apr_pool_t *pool = NULL;

  if(apr_atomic_read32(&initialized) == 0)
    possibly_init_global_pool();

#ifdef AEB_USE_THREADS
  AEB_APR_ASSERT(apr_thread_mutex_lock(global_mutex));
#endif

  if((pool = current_global_user_pool) == NULL) {
    AEB_APR_ASSERT(apr_pool_create(&pool,NULL));
    current_global_user_pool = pool;
    /* prealloc */
    apr_palloc(pool,AEB_POOL_BLOCK_SIZE);
    apr_pool_clear(pool);
  }

  if(apr_atomic_inc32(&global_pool_refs) == 0)
    apr_pool_cleanup_register(pool,pool,abort_on_early_cleanup,apr_pool_cleanup_null);

  return pool;
}

AEB_INTERNAL(void) aeb_global_static_pool_release(void)
{
#ifdef AEB_USE_THREADS
  apr_status_t st;
#endif
  apr_pool_t *pool = current_global_user_pool;

  ASSERT(pool != NULL);
  if(apr_atomic_dec32(&global_pool_refs) == 0) {
    apr_pool_cleanup_kill(pool,pool,abort_on_early_cleanup);
    apr_pool_clear(pool);
  }

#ifdef AEB_USE_THREADS
  AEB_APR_ASSERT(apr_thread_mutex_unlock(global_mutex));
#endif
}

/* get a thread-local static memory pool that is preallocated and thus
 * low-contention for repeated (if small) use by the same thread.
 * A given thread should pair each call to aeb_thread_static_pool_acquire()
 * with a matching aeb_thread_static_pool_release() but acquire calls
 * may be nested inside pairings. When the thread terminates the pool
 * is globally destroyed and all memory returned to the heap.
 */
AEB_INTERNAL(apr_pool_t*) aeb_thread_static_pool_acquire(void)
{
#ifdef AEB_USE_THREADS
  apr_status_t st;
  aeb_tls_static_pool_t *info = NULL;

  if(apr_atomic_read32(&initialized) == 0)
    possibly_init_global_pool();

  AEB_APR_ASSERT(apr_threadkey_private_get((void**)&info,tls_pools));
  if(info == NULL) {
    apr_pool_t *pool = NULL;
    AEB_APR_ASSERT(apr_thread_mutex_lock(global_mutex));
      AEB_APR_ASSERT(apr_pool_create(&pool,global_pool));
      /* preallocate space and immediately clear it so it remains available
       * for use without future heap allocations and associated contention.
       */
      ASSERT(apr_palloc(pool,AEB_POOL_BLOCK_SIZE) != NULL);
      apr_pool_clear(pool);
      ASSERT((info = apr_palloc(pool,sizeof(aeb_tls_static_pool_t))) != NULL);
      info->pool = pool;
    AEB_APR_ASSERT(apr_thread_mutex_unlock(global_mutex));

    apr_atomic_set32(&info->refcount,1);
    AEB_APR_ASSERT(apr_threadkey_private_set(info,tls_pools));
  } else {
    apr_atomic_inc32(&info->refcount);
  }

  return info->pool;
#else /* !AEB_USE_THREADS */
  return aeb_global_static_pool_acquire();
#endif /* AEB_USE_THREADS */
}

AEB_INTERNAL(void) aeb_thread_static_pool_release(void)
{
#ifdef AEB_USE_THREADS
  apr_status_t st;
  aeb_tls_static_pool_t *info = NULL;

  if(apr_atomic_read32(&initialized) == 0)
    possibly_init_global_pool();

  AEB_APR_ASSERT(apr_threadkey_private_set((void**)&info,tls_pools));
  ASSERT(info != NULL);
  if(apr_atomic_dec32(&info->refcount) == 0)
    apr_pool_clear(info->pool);
#else /* !AEB_USE_THREADS */
  aeb_global_static_pool_release();
#endif
}
