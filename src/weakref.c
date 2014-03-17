#include "internal.h"
#include "util.h"
#include "weakref.h"

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#ifdef AEB_USE_THREADS
#include <apr_thread_proc.h>
#include <apr_thread_cond.h>
#include <apr_thread_mutex.h>
#endif /* AEB_USE_THREADS */

#ifndef MIN_RELEASES_BEFORE_POOL_CLEAR
#define MIN_RELEASES_BEFORE_POOL_CLEAR 100
#endif

struct aeb_weakref {
  void *ctx;
  apr_pool_t *binding;
#ifdef AEB_USE_THREADS
  apr_thread_mutex_t *mutex;
#endif
};

static apr_status_t unbind_weakref(void*);
static apr_pool_t *weakref_pool = NULL;
static apr_array_header_t *weakrefs = NULL;
static apr_size_t nreleased = 0;
#ifdef AEB_USE_THREADS
static apr_pool_t *perm_mutex_pool = NULL;
static apr_thread_mutex_t *global_weakref_mutex = NULL;
#endif

static void init_weakrefs(void)
{
  if(weakref_pool == NULL) {
    apr_pool_create_unmanaged(&weakref_pool);
    assert(weakref_pool != NULL);
    apr_pool_tag(weakref_pool,"GLOBAL Indirect Unmanaged Pool");
  }

  nreleased = 0;
  weakrefs = apr_array_make(weakref_pool,
                             MIN_RELEASES_BEFORE_POOL_CLEAR,
                             sizeof(struct aeb_weakref));
  assert(weakrefs != NULL);
#ifdef AEB_USE_THREADS
  if (perm_mutex_pool == NULL) {
    apr_pool_create_unmanaged(&perm_mutex_pool);
    assert(perm_mutex_pool != NULL);
    apr_pool_tag(perm_mutex_pool,"really really really permanent mutex pool");
  }
  if (global_weakref_mutex == NULL) {
    assert(apr_thread_mutex_create(&global_weakref_mutex,APR_THREAD_MUTEX_UNNESTED,
                                    perm_mutex_pool) == APR_SUCCESS);
  }
#endif /* AEB_USE_THREADS */
}

static void collect_weakrefs(void)
{
#ifdef AEB_USE_THREADS
  apr_status_t st = apr_thread_mutex_lock(global_weakref_mutex);
  assert(st == APR_SUCCESS);
#endif

  if(nreleased == weakrefs->nelts && nreleased >= MIN_RELEASES_BEFORE_POOL_CLEAR) {
    apr_size_t i;
    struct aeb_weakref *ind = (struct aeb_weakref*)weakrefs->elts;
    for(i = 0; i < nreleased; i++, ind++) {
#ifdef AEB_USE_THREADS
      if (ind->mutex) {
        if(ind->binding != NULL) {
          apr_thread_mutex_t *mutex = NULL;
          assert(apr_thread_mutex_lock(ind->mutex) == APR_SUCCESS);
          mutex = ind->mutex;
          ind->mutex = NULL;
          /* double check for race */
#endif
          if(ind->binding != NULL)
            apr_pool_cleanup_kill(ind->binding,ind,unbind_weakref);
          ind->binding = NULL;
#ifdef AEB_USE_THREADS
          if (mutex)
            assert(apr_thread_mutex_unlock(mutex) == APR_SUCCESS);
        } else ind->mutex = NULL;
      }
      if (st != APR_SUCCESS)
        goto collect_weakrefs_exit;
#endif
      assert(ind->binding == NULL);
      assert(ind->ctx == NULL);
    }

    apr_pool_clear(weakref_pool);
    init_weakrefs();
  }

#ifdef AEB_USE_THREADS
collect_weakrefs_exit:
  assert(apr_thread_mutex_unlock(global_weakref_mutex) == APR_SUCCESS);
  assert(st == APR_SUCCESS);
#endif
}

static apr_status_t unbind_weakref(void *data)
{
  struct aeb_weakref *i = data;
  int perform_gc = 0;

#ifdef AEB_USE_THREADS
  apr_status_t st = apr_thread_mutex_lock(i->mutex);
  /* guard against race from collect_weakrefs */
  if(!i->mutex)
    return APR_SUCCESS;
  else
    assert(st = APR_SUCCESS);
#endif

  if(i->binding != NULL) {
    i->binding = NULL;
    if(i->ctx == NULL) {
      nreleased++;
      if(nreleased >= MIN_RELEASES_BEFORE_POOL_CLEAR)
        perform_gc++;
    }
  }
#ifdef AEB_USE_THREADS
  assert(apr_thread_mutex_unlock(i->mutex) == APR_SUCCESS);
#endif
  if (perform_gc)
    collect_weakrefs();
  return APR_SUCCESS;
}

AEB_INTERNAL(void*) aeb_weakref_consume(aeb_weakref_t *i)
{
  void *ctx;
  int perform_gc = 0;

#ifdef AEB_USE_THREADS
  assert(i->mutex != NULL);
  assert(apr_thread_mutex_lock(i->mutex) == APR_SUCCESS);
#endif

  ctx = i->ctx;
  if(ctx != NULL) {
    i->ctx = NULL;
    if (i->binding == NULL) {
      ctx = NULL; /* no longer valid, bound pool is gone */
      nreleased++;
      perform_gc++;
    }
#ifdef AGGRESSIVELY_COLLECT_WEAKREFS
    else {
      apr_pool_cleanup_kill(i->binding,i,unbind_weakref);
      i->binding = NULL;
      nreleased++;
      perform_gc++;
    }
#endif
    if(perform_gc && (nreleased < MIN_RELEASES_BEFORE_POOL_CLEAR ||
                      nreleased != weakrefs->nelts))
      perform_gc = 0;
  }
#ifdef AEB_USE_THREADS
  assert(apr_thread_mutex_unlock(i->mutex));
#endif

  if(perform_gc)
    collect_weakrefs();

  return ctx;
}

AEB_INTERNAL(aeb_weakref_t*) aeb_weakref_make(void *ctx, apr_pool_t *binding)
{
  apr_status_t st = APR_SUCCESS;
  struct aeb_weakref *i = NULL;

#ifdef AEB_USE_THREADS
  if(global_weakref_mutex == NULL)
    init_weakrefs();

  st = apr_thread_mutex_lock(global_weakref_mutex);
  assert(st == APR_SUCCESS);
#else
  if(weakref_pool == NULL)
    init_weakrefs();
#endif
  i = (struct aeb_weakref*)&APR_ARRAY_PUSH(weakrefs,aeb_weakref_t);
  i->ctx = ctx;
  i->binding = binding;
#ifdef AEB_USE_THREADS
  st = apr_thread_mutex_create(&i->mutex,APR_THREAD_MUTEX_UNNESTED,weakref_pool);
  assert(st == APR_SUCCESS);
#endif
  apr_pool_cleanup_register(binding,i,unbind_weakref,apr_pool_cleanup_null);

#ifdef AEB_USE_THREADS
  assert(apr_thread_mutex_unlock(global_weakref_mutex) == APR_SUCCESS);
#endif

  return i;
}
