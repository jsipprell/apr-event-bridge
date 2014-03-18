/* testing for libaeb */

#include "internal.h"
#include "util.h"

#include <libaeb_assert.h>

#include <apr_thread_pool.h>
#include <apr_thread_proc.h>
#include <apr_time.h>

#define HEXFMT "0x%" APR_UINT64_T_HEX_FMT
#define HEX(v) ((apr_uint64_t)(v) & 0xffffffff)

static apr_pool_t *root_pool = NULL;

typedef struct {
  apr_pool_t *p;
  const char *name;
  apr_thread_t *t;
  struct timeval tv;
  apr_uint16_t counter;
  struct event *ev;
} threadstate_t;

static inline threadstate_t *STATE(void *data)
{
  threadstate_t *state = (threadstate_t*)data;

  assert(state != NULL);
  if(state->name == NULL) {
    const char *name = NULL;
    assert(apr_thread_data_get((void**)&name,"thread_name",state->t) == APR_SUCCESS);
    if(name == NULL) {
      assert(apr_pool_userdata_get((void**)&name,"thread_name",state->p) == APR_SUCCESS);
    }
    if(name)
      state->name = apr_psprintf(state->p,"[%s]",name);
  }

  assert(state->name != NULL);
  return state;
}

static const char *geterr(apr_status_t st, apr_pool_t *pool)
{
  static char buf[AEB_BUFSIZE];

  buf[0] = '\0';
  if(apr_strerror(st,buf,AEB_BUFSIZE-1) != NULL) {
    buf[AEB_BUFSIZE-1] = '\0';
    if(pool)
      return apr_pstrdup(pool,buf);
    return buf;
  }
  return NULL;
}

static apr_threadattr_t *new_threadattr(apr_pool_t *pool)
{
  apr_threadattr_t *attr;

  assert(apr_threadattr_create(&attr,(pool ? pool : root_pool)) == APR_SUCCESS);
  /* assert(apr_threadattr_detach_set(attr,1) == APR_SUCCESS); */
  return attr;
}

static apr_status_t release_thread_pool(void *data)
{
  apr_pool_destroy((apr_pool_t*)data);
  return APR_SUCCESS;
}

static void event_sleep_callback(evutil_socket_t s, short what, void *data)
{
  threadstate_t *state = STATE(data);

  state->counter++;
  printf("--- %s " HEXFMT " woke up, socket " HEXFMT " code:%d\n",
        state->name,
        HEX(state->t),HEX(s),(int)what);

  if (state->tv.tv_sec > 0) {
    state->tv.tv_sec--;
    printf("--- %s " HEXFMT " secs remaining: %u\n", 
           state->name,HEX(state->t),(unsigned)state->tv.tv_sec);
    evtimer_add(state->ev,&state->tv);
  } else {
    printf("--- %s " HEXFMT " ALL DONE!\n",state->name,HEX(state->t));
    event_base_loopbreak(aeb_event_base());
  }
}

static void event_sleep(threadstate_t *state)
{
  struct event_base *base = NULL;

  base = aeb_event_base();
  printf("++ %s event base is " HEXFMT "\n",STATE(state)->name,HEX(base));
  if(state->ev == NULL)
    state->ev = evtimer_new(base,event_sleep_callback,state);
  evtimer_add(state->ev,&state->tv);
}

static void * APR_THREAD_FUNC test_thread(apr_thread_t *this, void *data)
{
  apr_pool_t *pool = (apr_pool_t*)data;
  const char *name = NULL;
  char *tmp = NULL;
  struct event_base *base = NULL;
  threadstate_t *state;

  assert(pool != NULL);
  assert(apr_pool_userdata_get((void**)&name,"thread_name",pool) == APR_SUCCESS);

  assert(apr_thread_data_get((void**)&tmp,"thread_name",this) == APR_SUCCESS);
  assert(tmp != NULL);
  if (tmp == NULL) {
    assert(name != NULL);
    tmp = apr_pstrdup(pool,name);
    assert(apr_thread_data_set(tmp,"thread_name",NULL,this) == APR_SUCCESS);
    assert(apr_thread_data_set(pool,"thread_pool",NULL,this) == APR_SUCCESS);
  }
  printf("++ [%s] Thread " HEXFMT ", my pool = " HEXFMT "\n", name, HEX(this), HEX(pool));
  AEB_ASSERT((base = aeb_event_base()) != NULL,"aeb_event_base is null");
  printf("++ [%s] base = " HEXFMT "\n",name,HEX(base));
  printf("++ [%s] sleeping for 6...\n",name);
  fflush(stdout);
  assert((state = apr_pcalloc(pool,sizeof(threadstate_t))) != NULL);
  state->t = this;
  state->p = pool;
  state->tv.tv_sec = 4;
  event_sleep(state);
  event_base_loop(base,0);
  printf("++ [%s] FINI\n",name);
  /* apr_thread_exit(this,0); */
  return NULL;
}

static apr_status_t debug_release_thread_pool(void *x)
{
  printf("!!!! DEBUG: debug_release_thread_pool " HEXFMT "\n",HEX(x));
  return APR_SUCCESS;
}

static apr_status_t debug_destroy_thread_pool(void *x)
{
  printf("^^^^ DEBUG: debug_destroy_thread_pool " HEXFMT "\n",HEX(x));
  return APR_SUCCESS;
}

static void * APR_THREAD_FUNC test_thread_launcher(apr_thread_t *this, void *data)
{
  const char *name = (const char *)data;
  apr_pool_t *work_pool = NULL;
  void *rv;

  assert(name != NULL);
  if (apr_thread_data_get((void**)&work_pool,"thread_pool",this) != APR_SUCCESS || !work_pool) {
    ASSERT(apr_pool_create(&work_pool,apr_thread_pool_get(this)) == APR_SUCCESS);
    printf("!!!! DEBUG: " HEXFMT ": new pool " HEXFMT " from " HEXFMT " !!\n",
           HEX(this),HEX(work_pool),HEX(apr_thread_pool_get(this)));
    ASSERT(apr_thread_data_set(work_pool,"thread_pool",debug_release_thread_pool,this) == APR_SUCCESS);
    apr_pool_cleanup_register(work_pool,work_pool,debug_destroy_thread_pool,
                              apr_pool_cleanup_null);
    apr_pool_cleanup_register(apr_thread_pool_get(this),apr_thread_pool_get(this),
                              debug_destroy_thread_pool,
                              apr_pool_cleanup_null);
  }
  name = apr_pstrdup(work_pool,name);
  assert(apr_pool_userdata_set(name,
                                "thread_name",NULL,work_pool) == APR_SUCCESS);
  assert(apr_thread_data_set((void*)name,"thread_name",NULL,this) == APR_SUCCESS);
  rv = test_thread(this,work_pool);
  assert(apr_thread_data_set(NULL,"thread_name",NULL,this) == APR_SUCCESS);
  return rv;
}

static apr_thread_t *new_thread(const char *name, 
                                apr_threadattr_t *attr,
                                apr_thread_start_t func,
                                void *data,
                                apr_pool_t *pool)
{
  apr_pool_t *tpool;
  apr_thread_t *t;

  if(!pool) {
    tpool = pool = root_pool;
    /* assert(apr_pool_create(&tpool,pool) == APR_SUCCESS); */
  } else {
    tpool = pool;
  }

#if 0
  if(!attr)
    attr = new_threadattr(pool);
#endif
  if(!data)
    data = (void*)name;
  if(!func)
    func = &test_thread_launcher;
/*
  assert(apr_pool_userdata_set(apr_pstrdup(tpool,name),"thread_name",NULL,tpool) == APR_SUCCESS);
*/
  assert(apr_thread_create(&t,attr,func,data,tpool) == APR_SUCCESS);
  return t;
}

static apr_status_t debug_destroy_worker_pool(void *p)
{
 printf("---> WORKER: BYE BYE " HEXFMT " <----\n",HEX(p));
 return APR_SUCCESS;
}

static void test_aeb_pools(void)
{
  apr_thread_pool_t *workers = NULL;
  apr_pool_t *pool = NULL;
  static unsigned tcount = 0;

  struct event_base *base;
  printf("test_aeb_pools pass 1\n");
  AEB_ASSERT((base = aeb_event_base()) != NULL,"aeb_event_base is null");
  printf("test aeb_pools pass 2, base = " HEXFMT "\n",HEX(base));
  assert(apr_pool_create(&pool,root_pool) == APR_SUCCESS);
  printf("=== NEW MEMORY POOL " HEXFMT " FOR THREAD POOL ===\n",HEX(pool));
  apr_pool_cleanup_register(pool,pool,debug_destroy_worker_pool,apr_pool_cleanup_null);
  assert(apr_thread_pool_create(&workers,2,10,pool) == APR_SUCCESS);
  for(tcount = 1; tcount < 100 || apr_thread_pool_tasks_count(workers) > 0; tcount++) {
    if (tcount < 100) {
      char *name = apr_psprintf(root_pool,"pool thread %u",tcount);
      /* printf("MAIN: new thread name '%s'\n",name); */
      assert(apr_thread_pool_push(workers,test_thread_launcher,name,
                                      APR_THREAD_TASK_PRIORITY_NORMAL,
                                      base) == APR_SUCCESS);
    }
    printf("*** MAIN: stats tasks:%u th:%u busy:%u idle:%u hi:%u ti:%u to:%u\n",
                        (unsigned)apr_thread_pool_tasks_count(workers),
                        (unsigned)apr_thread_pool_threads_count(workers),
                        (unsigned)apr_thread_pool_busy_count(workers),
                        (unsigned)apr_thread_pool_idle_count(workers),
                        (unsigned)apr_thread_pool_threads_high_count(workers),
                        (unsigned)apr_thread_pool_threads_idle_timeout_count(workers),
                        (unsigned)apr_thread_pool_tasks_run_count(workers));
    fflush(stdout);
    apr_sleep(apr_time_from_sec( (-1 * (int)tcount + 5) < 1 ? 1 : (-1 * (int)tcount + 5)));
    if (tcount < 100 && tcount % 10 == 0) {
      int j;
      printf("*** MAIN: taking a little break\n");
      for(j = 1; j < 5; j++) {
        printf("*** MAIN: stats tasks:%u th:%u busy:%u idle:%u hi:%u ti:%u to:%u\n",
                        (unsigned)apr_thread_pool_tasks_count(workers),
                        (unsigned)apr_thread_pool_threads_count(workers),
                        (unsigned)apr_thread_pool_busy_count(workers),
                        (unsigned)apr_thread_pool_idle_count(workers),
                        (unsigned)apr_thread_pool_threads_high_count(workers),
                        (unsigned)apr_thread_pool_threads_idle_timeout_count(workers),
                        (unsigned)apr_thread_pool_tasks_run_count(workers));
        fflush(stdout);
        apr_sleep(apr_time_from_sec( (-1 * (int)tcount + 5) < 1 ? 1 : (-1 * (int)tcount + 5)));
      }
    }
  }
  assert(apr_thread_pool_destroy(workers) == APR_SUCCESS);
  apr_pool_destroy(pool);
}

static void test_aeb(void)
{
  apr_status_t st = APR_SUCCESS;
  apr_status_t rv;
  apr_thread_t *t;
  apr_pool_t *pool;

  struct event_base *base;

  printf("test_aeb pass 1\n");
  AEB_ASSERT((base = aeb_event_base()) != NULL,"aeb_event_base is null");
  printf("test aeb pass 2, base = " HEXFMT "\n",HEX(base));
  assert(apr_pool_create(&pool,root_pool) == APR_SUCCESS);
  apr_pool_cleanup_register(pool,pool,debug_destroy_worker_pool,apr_pool_cleanup_null);
  printf("=== NEW POOL " HEXFMT " FOR UNBORN THREAD ===\n",HEX(pool));
  t = new_thread("thread 1",NULL,NULL,NULL,pool);
  apr_sleep(apr_time_from_sec(20));
  assert(t != NULL);
  printf("MAIN joining thread 1\n");
  fflush(stdout);
  fflush(stderr);
  rv = apr_thread_join(&st,t);
  if(rv != APR_SUCCESS)
    printf("apr_thread_join: %s\n",geterr(rv,NULL));
  else
    printf("thread exited, status %u\n",(unsigned)st);
  apr_pool_destroy(pool);
  apr_sleep(apr_time_from_sec(5));
  /* apr_pool_destroy(pool); */
}

int main(int argc, const char *const *argv, const char *const *env)
{
  apr_status_t rc = apr_initialize();

  AEB_ASSERT(rc == APR_SUCCESS,"apr_initialize failed");
  rc = apr_pool_create(&root_pool,NULL);
  AEB_ASSERT(rc == APR_SUCCESS,"apr_pool_create failued");
  apr_pool_tag(root_pool,"LIBAEB ROOT POOT");

  test_aeb();
  apr_terminate();
}