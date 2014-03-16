#include "internal.h"
#include "util.h"

#ifdef AEB_USE_THREADS

#include <apr_thread_proc.h>

static apr_threadkey_t *aeb_thread_key = NULL;
static apr_thread_once_t *thread_key_init_once = NULL;
static apr_pool_t *aeb_thread_pool = NULL;

#else /* !AEB_USE_THREADS */

static struct event_base *base = NULL;

#endif /* AEB_USE_THREADS */

#ifdef AEB_USE_THREADS
static apr_status_t cleanup_threadkey_pool(void *p)
{
  AEB_ASSERT(p != NULL,"null pool in cleanup_threadkey_pool");
  AEB_ASSERTV(p == aeb_thread_pool,"aeb_thread_pool mismatch, %ux != %ux",(unsigned int)p,(unsigned int)aeb_thread_pool);
  aeb_thread_pool = NULL;
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
  if(base) {
    event_base_free(base);
    fprintf(stderr,"freeing event base in thread_died()\n");
  } else {
    fprintf(stderr,"thread_died() called with NULL event base\n");
  }
}

static apr_status_t reinit_event_base(void *data)
{
  struct event_base *base = (struct event_base*)data;

  AEB_ASSERT(base != NULL,"null event_base in reinit_event_base");
  AEB_ASSERT(event_reinit(base) == 0,"count not re-add all events to event base");
  return APR_SUCCESS;
}

static void init_aeb_threading(void)
{
#ifdef EVTHREAD_USE_PTHREADS_IMPLEMENTED
  evthread_use_pthreads();
#elif defined(EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED)
  evthread_use_windows_threads();
#endif
  AEB_ASSERT(apr_pool_create(&aeb_thread_pool,NULL) == APR_SUCCESS,
             "apr_pool_create failure");
  AEB_ASSERT(apr_threadkey_private_create(&aeb_thread_key,
                thread_died,
                aeb_thread_pool) == APR_SUCCESS,
             "apr_threadkey_private_create failed");
  AEB_ASSERT(aeb_thread_key != NULL,"aeb_thread_key should not be NULL");
}

#endif /* AEB_USE_THREADS */

AEB_INTERNAL(struct event_base*) aeb_event_base(void)
{
#ifdef AEB_USE_THREADS
  struct event_base *base = NULL;
  if(thread_key_init_once == NULL) {
    AEB_ASSERT(aeb_thread_pool == NULL, "aeb_thread_pool should be null");

    AEB_ASSERT(apr_pool_create(&aeb_thread_pool,NULL) == APR_SUCCESS,
               "apr_pool_create failure");

    AEB_ASSERT(apr_thread_once_init(&thread_key_init_once,aeb_thread_pool) == APR_SUCCESS,
               "apr_thread_once_init failure");
  }
    printf("init init_once\n");
  AEB_ASSERT(apr_thread_once(thread_key_init_once,init_aeb_threading) == APR_SUCCESS,
             "apr_thread_once failure (init_aeb_threading)");
  AEB_ASSERT(aeb_thread_key != NULL,"aeb_thread_key is null");

  if (apr_threadkey_private_get((void**)&base,aeb_thread_key) != APR_SUCCESS || base == NULL) {
    apr_pool_t *cleanup_pool = NULL;

    AEB_ASSERT(apr_pool_create(&cleanup_pool,NULL) == APR_SUCCESS,"apr_pool_create failure");
    AEB_ASSERT((base = event_base_new()) != NULL,"event_base_new returned null");
    AEB_ASSERT((apr_threadkey_data_set(cleanup_pool,"aeb_threadkey_cleanup_pool",NULL,
                           aeb_thread_key)) == APR_SUCCESS,"apr_threadkey_data_set failure");
    apr_pool_cleanup_register(cleanup_pool,base,
                              cleanup_event_base,
                              reinit_event_base);
    AEB_ASSERT(apr_threadkey_private_set(base,aeb_thread_key) == APR_SUCCESS,
              "apr_threadkey_private_set failure");
  }
#else /* !AEB_USE_THREADS */
  if (base == NULL) {
    AEB_ASSERT((base = event_init()) != NULL,"event_init failed");
  }
#endif /* AEB_USE_THREADS */
  return base;
}