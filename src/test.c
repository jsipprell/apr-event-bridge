/* testing for libaeb */
#include "internal.h"
#include "util.h"

#include <libaeb_assert.h>

#include <apr_thread_pool.h>
#include <apr_thread_proc.h>
#include <apr_time.h>
#include <apr_atomic.h>

#define ENTROPY_FILE "/dev/urandom"
#define MAX_SLEEP APR_TIME_C(20000000)
#define MIN_SLEEP APR_TIME_C(5000000)

#define HEXFMT "0x%" APR_UINT64_T_HEX_FMT
#define HEX(v) ((apr_uint64_t)(v) & 0xffffffff)

static apr_pool_t *root_pool = NULL;
static volatile apr_uint32_t work_done = 0;

typedef struct {
  apr_pool_t *p;
  const char *name;
  apr_thread_t *t;
  struct timeval tv;
  apr_interval_time_t sleep_time;
  apr_uint16_t counter;
  aeb_event_t *event;
  struct event *ev;
} threadstate_t;

typedef struct {
  apr_pool_t *pool;
  const char *name;
  apr_interval_time_t sleep_time;
} test_thread_start_info_t;

static inline threadstate_t *STATE(void *data)
{
  apr_status_t st;
  threadstate_t *state = (threadstate_t*)data;

  assert(state != NULL);
  if(state->name == NULL) {
    const char *name = NULL;
    AEB_APR_ASSERT(apr_thread_data_get((void**)&name,"thread_name",state->t));
    if(name == NULL) {
      AEB_APR_ASSERT(apr_pool_userdata_get((void**)&name,"thread_name",state->p));
    }
    if(name)
      state->name = apr_psprintf(state->p,"[%s]",name);
  }

  assert(state->name != NULL);
  return state;
}

static void test_sleep(apr_interval_time_t how_long)
{
  ASSERT(!aeb_event_loop_isrunning());
  ASSERT(aeb_event_loop(&how_long) == APR_TIMEUP);
}

static void event_sleep_callback(evutil_socket_t s, short what, void *data)
{
  threadstate_t *state = STATE(data);

  state->counter++;
#if 0
  printf("--- %s " HEXFMT " woke up, socket " HEXFMT " code:%d\n",
        state->name,
        HEX(state->t),HEX(s),(int)what);
#endif
#if 0
  if (state->tv.tv_sec > 0) {
    state->tv.tv_sec--;

#if 0
    printf("--- %s " HEXFMT " secs remaining: %u\n",
           state->name,HEX(state->t),(unsigned)state->tv.tv_sec);
#endif
    evtimer_add(state->ev,&state->tv);
  } else {
#endif
#if 0
    printf( "%s " HEXFMT " ALL DONE!\n",state->name,HEX(state->t));
#endif
    aeb_event_loop_return_status(APR_CHILD_DONE);
#if 0
  }
#endif
}

static void event_sleep(threadstate_t *state)
{
  struct event_base *base = NULL;

  base = aeb_event_base();

#if 0
  printf("++ %s event base is " HEXFMT "\n",STATE(state)->name,HEX(base));

  if(state->event == NULL) {
    AEB_APR_ASSERT(aeb_timer_create(state->pool,
                                    event_sleep_callback_new,
                                    state->sleep_time,
                                    &state->event));
    ASSERT(state->event != NULL);
    AEB_APR_ASSERT(aeb_event_user_context_set(state->event,state));
    AEB_APR_ASSERT(aeb_event_add(state->event));
  }
#endif
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
  test_thread_start_info_t *info = NULL;
  threadstate_t *state;
  apr_interval_time_t loop_timer;
  apr_time_t start_time = apr_time_now();
  apr_status_t st = APR_SUCCESS;
  apr_uint16_t count;

  assert(pool != NULL);
  assert(apr_pool_userdata_get((void**)&name,"thread_name",pool) == APR_SUCCESS);
  assert(apr_pool_userdata_get((void**)&info,"thread_info",pool) == APR_SUCCESS && info != NULL);
  assert(apr_thread_data_get((void**)&tmp,"thread_name",this) == APR_SUCCESS);
  assert(tmp != NULL);
  if (tmp == NULL) {
    assert(name != NULL);
    tmp = apr_pstrdup(pool,name);
    assert(apr_thread_data_set(tmp,"thread_name",NULL,this) == APR_SUCCESS);
  }
 AEB_ASSERTV(strcmp(tmp,name) == 0,"%s != %s",tmp,name);

#if 0
  printf("++ [%s] Thread " HEXFMT ", my pool = " HEXFMT "\n", name, HEX(this), HEX(pool));
#endif
  AEB_ASSERT((base = aeb_event_base()) != NULL,"aeb_event_base is null");
#if 0
  printf("++ [%s] base = " HEXFMT "\n",name,HEX(base));
  printf("++ [%s] sleeping for 6...\n",name);
  fflush(stdout);
#endif
  assert((state = apr_pcalloc(pool,sizeof(threadstate_t))) != NULL);
  state->t = this;
  state->p = pool;
  state->sleep_time = info->sleep_time;
  state->tv.tv_sec = apr_time_sec(info->sleep_time);
  state->tv.tv_usec = apr_time_usec(info->sleep_time);
  event_sleep(state);

  for(loop_timer = apr_time_from_sec(12), count = 0;
    st == APR_SUCCESS || st == APR_TIMEUP;
        st = aeb_event_loop(&loop_timer), count++) {

    if(count == 0) continue;
    if(st != APR_CHILD_DONE) {
      apr_pool_t *tmp_pool;
      tmp_pool = aeb_thread_static_pool_acquire();
      assert(tmp_pool != NULL);
      printf("[%s] secs:%0.4f work:%u st:%d\n",
             name,(float)(apr_time_now()-start_time)/APR_USEC_PER_SEC,
             (unsigned)apr_atomic_read32(&work_done),
             (int)st);
      aeb_thread_static_pool_release();
    }
    if(st == APR_TIMEUP)
      loop_timer = apr_time_from_sec(3);
  }

  if(st != APR_SUCCESS && st != APR_CHILD_DONE)
    fprintf(stderr,"%s: ERROR %s\n",name,aeb_errorstr(st,pool));

  apr_atomic_inc32(&work_done);
#if 0
  printf("++ [%s] FINI\n",name);
#endif
  /* apr_thread_exit(this,0); */
  return NULL;
}

static apr_status_t debug_release_thread_pool(void *x)
{
#if 0
  printf("---- DEBUG: debug_release_thread_pool " HEXFMT "\n",HEX(x));
#endif
  return APR_SUCCESS;
}

static apr_status_t debug_destroy_thread_pool(void *x)
{
#if 0
  printf("---- DEBUG: debug_destroy_thread_pool " HEXFMT "\n",HEX(x));
#endif
  return APR_SUCCESS;
}

static void * APR_THREAD_FUNC test_thread_launcher(apr_thread_t *this, void *data)
{
  apr_status_t st;
  test_thread_start_info_t *info,*start = (test_thread_start_info_t*)data;
  const char *name = start->name;
  apr_pool_t *tls_pool = aeb_thread_static_pool_acquire();
  apr_pool_t *work_pool = NULL;
  void *rv;

  assert(name != NULL);
  assert(tls_pool != NULL);
  AEB_APR_ASSERT(apr_pool_create(&work_pool,tls_pool));

#if 0
    printf("!!!! DEBUG: " HEXFMT ": new pool " HEXFMT " from " HEXFMT " !!\n",
           HEX(this),HEX(work_pool),HEX(apr_thread_pool_get(this)));
#endif

  ASSERT((name = apr_pstrdup(work_pool,name)) != NULL);
  AEB_APR_ASSERT(apr_pool_userdata_set(name,"thread_name",NULL,work_pool));
  AEB_APR_ASSERT(apr_thread_data_set((void*)name,"thread_name",NULL,this));
  info = apr_pmemdup(work_pool,start,sizeof(test_thread_start_info_t));
  info->pool = work_pool;
  AEB_APR_ASSERT(apr_pool_userdata_set(info,"thread_info",NULL,work_pool));
  apr_pool_destroy(start->pool);
  rv = test_thread(this,work_pool);
  assert(apr_thread_data_set(NULL,"thread_name",NULL,this) == APR_SUCCESS);
  aeb_thread_static_pool_release();
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
  apr_status_t st;
  apr_file_t *entropy = NULL;
  apr_thread_pool_t *workers = NULL;
  apr_pool_t *pool = NULL;
  static unsigned tcount = 0;

  struct event_base *base;
#if 0
  printf("test_aeb_pools pass 1\n");
#endif
  AEB_ASSERT((base = aeb_event_base()) != NULL,"aeb_event_base is null");
#if 0
  printf("test aeb_pools pass 2, base = " HEXFMT "\n",HEX(base));
#endif
  assert(apr_pool_create(&pool,root_pool) == APR_SUCCESS);

  AEB_APR_ASSERT(apr_file_open(&entropy,ENTROPY_FILE,APR_READ,0,pool));
  printf("=== NEW MEMORY POOL " HEXFMT " FOR THREAD POOL ===\n",HEX(pool));
  apr_pool_cleanup_register(pool,pool,debug_destroy_worker_pool,apr_pool_cleanup_null);
  assert(apr_thread_pool_create(&workers,2,10,pool) == APR_SUCCESS);
  for(tcount = 1; tcount < 100 || apr_thread_pool_tasks_count(workers) > 0; tcount++) {
    if (tcount < 100) {
      apr_pool_t *temp_pool = NULL;
      char *name = apr_psprintf(root_pool,"pool thread %u",tcount);
      test_thread_start_info_t *start = NULL;
      apr_interval_time_t sleep_time = APR_USEC_PER_SEC * 10;
      apr_size_t bread = sizeof(apr_interval_time_t);
      AEB_APR_ASSERT(apr_file_read(entropy,&sleep_time,&bread));
      assert(bread == sizeof(apr_interval_time_t));
      AEB_APR_ASSERT(apr_pool_create(&temp_pool,root_pool));
      start = apr_pcalloc(temp_pool,sizeof(test_thread_start_info_t));
      start->pool = temp_pool;
      start->name = name;
      start->sleep_time = (sleep_time % (MAX_SLEEP - MIN_SLEEP)) + MIN_SLEEP;
      /* printf("MAIN: new thread name '%s'\n",name); */
      assert(apr_thread_pool_push(workers,test_thread_launcher,start,
                                      APR_THREAD_TASK_PRIORITY_NORMAL,
                                      base) == APR_SUCCESS);
    }
    printf("*** MAIN: stats/%u q:%u th:%u busy:%u idle:%u hi:%u ti:%u to:%u\n",
                        (unsigned)apr_atomic_read32(&work_done),
                        (unsigned)apr_thread_pool_tasks_count(workers),
                        (unsigned)apr_thread_pool_threads_count(workers),
                        (unsigned)apr_thread_pool_busy_count(workers),
                        (unsigned)apr_thread_pool_idle_count(workers),
                        (unsigned)apr_thread_pool_threads_high_count(workers),
                        (unsigned)apr_thread_pool_threads_idle_timeout_count(workers),
                        (unsigned)apr_thread_pool_tasks_run_count(workers));
    fflush(stdout);
    test_sleep(apr_time_from_sec( (-1 * (int)tcount + 5) < 1 ? 1 : (-1 * (int)tcount + 5)));
    if (tcount < 100 && tcount % 10 == 0) {
      int j;
#if 0
      printf("*** MAIN: taking a little break\n");
#endif
      for(j = 1; j < 5; j++) {
        printf("*** MAIN: stats/%u q:%u th:%u busy:%u idle:%u hi:%u ti:%u to:%u\n",
                        (unsigned)apr_atomic_read32(&work_done),
                        (unsigned)apr_thread_pool_tasks_count(workers),
                        (unsigned)apr_thread_pool_threads_count(workers),
                        (unsigned)apr_thread_pool_busy_count(workers),
                        (unsigned)apr_thread_pool_idle_count(workers),
                        (unsigned)apr_thread_pool_threads_high_count(workers),
                        (unsigned)apr_thread_pool_threads_idle_timeout_count(workers),
                        (unsigned)apr_thread_pool_tasks_run_count(workers));
        fflush(stdout);
        test_sleep(apr_time_from_sec( (-1 * (int)tcount + 5) < 1 ? 1 : (-1 * (int)tcount + 5))-APR_TIME_C(500000));
      }
    }
  }
  if (tcount >= 100) {
    apr_uint32_t w;
    for(w = apr_atomic_read32(&work_done); w < 100 && apr_thread_pool_busy_count(workers) > 0;
                                           w = apr_atomic_read32(&work_done)) {
      printf("*** MAIN: stats/%u remain:%u q:%u th:%u busy:%u idle:%u hi:%u ti:%u to:%u\n",
                    (unsigned)apr_atomic_read32(&work_done),
                    (unsigned)(100 - w)-1,
                    (unsigned)apr_thread_pool_tasks_count(workers),
                    (unsigned)apr_thread_pool_threads_count(workers),
                    (unsigned)apr_thread_pool_busy_count(workers),
                    (unsigned)apr_thread_pool_idle_count(workers),
                    (unsigned)apr_thread_pool_threads_high_count(workers),
                    (unsigned)apr_thread_pool_threads_idle_timeout_count(workers),
                    (unsigned)apr_thread_pool_tasks_run_count(workers));
      fflush(stdout);
      test_sleep(apr_time_from_sec(1));
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

#if 0
  printf("test_aeb pass 1\n");
#endif
  AEB_ASSERT((base = aeb_event_base()) != NULL,"aeb_event_base is null");
#if 0
  printf("test aeb pass 2, base = " HEXFMT "\n",HEX(base));
#endif
  assert(apr_pool_create(&pool,root_pool) == APR_SUCCESS);
  apr_pool_cleanup_register(pool,pool,debug_destroy_worker_pool,apr_pool_cleanup_null);
  printf("=== NEW POOL " HEXFMT " FOR UNBORN THREAD ===\n",HEX(pool));
  t = new_thread("thread 1",NULL,NULL,NULL,pool);
  test_sleep(apr_time_from_sec(20));
  assert(t != NULL);
  printf("MAIN joining thread 1\n");
  fflush(stdout);
  fflush(stderr);
  rv = apr_thread_join(&st,t);
  if(rv != APR_SUCCESS)
    printf("apr_thread_join: %s\n",aeb_errorstr(rv,pool));
  else
    printf("thread exited, status %u\n",(unsigned)st);
  apr_pool_destroy(pool);
  test_sleep(apr_time_from_sec(5));
  /* apr_pool_destroy(pool); */
}

int main(int argc, const char *const *argv, const char *const *env)
{
  apr_status_t rc = apr_initialize();

  AEB_ASSERT(rc == APR_SUCCESS,"apr_initialize failed");
  rc = apr_pool_create(&root_pool,NULL);
  AEB_ASSERT(rc == APR_SUCCESS,"apr_pool_create failued");
  apr_pool_tag(root_pool,"LIBAEB ROOT POOT");

  test_aeb_pools();
  apr_terminate();
  return 0;
}
