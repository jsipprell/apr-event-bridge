/* libaeb testing memcache client thread */
#include "internal.h"
#include "util.h"
#include <libaeb_assert.h>
#include <libaeb_event_info.h>

#include <apr_network_io.h>
#include <apr_thread_proc.h>
#include <apr_time.h>

#include <apr_buckets.h>

#define MC_SLEEP_INTERVAL APR_TIME_C(1000000)
#define MC_SLEEP_TICK 8
#define DEFAULT_SERVER "localhost:11211"
#ifndef AEB_MAX_WRITE
#define AEB_MAX_WRITE 10
#endif
#ifndef AEB_MAX_IOVECS
#define AEB_MAX_IOVECS 3
#endif

#define HEXFMT "0x%" APR_UINT64_T_HEX_FMT
#define HEX(v) ((apr_uint64_t)(v) & 0xffffffff)


typedef apr_bucket_brigade aeb_brigade_t;
static int mc_shutdown = 0;

typedef struct {
  apr_pool_t *p;
  apr_thread_t *thread;
  apr_time_t sleep_time;
  apr_int16_t sleep_tick;
  apr_socket_t *sock;
  char *host;
  apr_port_t port;
  aeb_event_t *event;
  apr_pollfd_t pollfd;
  apr_bucket_brigade *read_bb,*write_bb,*pending;
} mc_client_t;

static apr_status_t memcache_client_read(const aeb_event_info_t *info, void *data);

static apr_pollfd_t *make_socket_pollfd(apr_socket_t *s, apr_pool_t *p)
{
  apr_pollfd_t *pollfd = NULL;

  ASSERT((pollfd = apr_palloc(p,sizeof(apr_pollfd_t))) != NULL);
  pollfd->p = p;
  pollfd->desc_type = APR_POLL_SOCKET;
  pollfd->reqevents = APR_POLLIN|APR_POLLOUT;
  pollfd->rtnevents = 0;
  pollfd->desc.s = s;
  return pollfd;
}

static apr_status_t brigade_client_write(const aeb_event_info_t *info, void *data)
{
  mc_client_t *state = (mc_client_t*)data;
  apr_status_t st = APR_SUCCESS;
  apr_bucket_brigade *more,*bb = state->write_bb;
  apr_bucket_brigade *output = NULL;
  apr_read_type_e block = APR_NONBLOCK_READ;
  apr_pool_t *input_pool = (bb ? bb->p : NULL);
  apr_bucket_alloc_t *output_alloc = NULL;
  apr_bucket *last = NULL;

  assert(bb != NULL);
  if(state->pending) {
    APR_BRIGADE_CONCAT(state->pending,bb);
    bb = state->pending;
    state->pending = NULL;
  }

#if 0
  output_alloc = apr_bucket_alloc_create(input_pool ? input_pool : bb->p);
#else
  output_alloc = bb->bucket_alloc;
#endif

  while(bb && !APR_BRIGADE_EMPTY(bb)) {
    apr_size_t nbytes = 0;
    apr_bucket *next,*b;
    apr_uint32_t nvecs = 0;
    more = NULL;
    for(b = APR_BRIGADE_FIRST(bb), next = APR_BUCKET_NEXT(b);
                               b != APR_BRIGADE_SENTINEL(bb);
                               b = (next ? next : APR_BRIGADE_FIRST(bb)),
                               next = APR_BUCKET_NEXT(b)) {
      const char *str = NULL;
      apr_size_t n = 0;

      if (APR_BUCKET_IS_EOS(b)) {
        if(last)
          apr_bucket_delete(last);
        last = b;
        APR_BUCKET_REMOVE(last);
        break;
      } else if(APR_BUCKET_IS_FLUSH(b)) {
        /* noop */
        apr_bucket_delete(b);
        continue;
      }
      st = apr_bucket_read(b,&str,&n,block);
      if (APR_STATUS_IS_EAGAIN(st)) {
        if (b != APR_BRIGADE_FIRST(bb))
          more = apr_brigade_split(bb,b);
        b = NULL;
        break;
      } else {
        AEB_APR_ASSERT(st);
      }
      if(n) {
        apr_size_t sz = (n <= AEB_MAX_WRITE ? n : AEB_MAX_WRITE);

        if(sz < n) {
          apr_bucket_split(b,sz);
          next = APR_BUCKET_NEXT(b);
        }

        APR_BUCKET_REMOVE(b);

        if (!output)
          assert((output = apr_brigade_create(input_pool,output_alloc)) != NULL);

        APR_BRIGADE_INSERT_TAIL(output,b);
        b = NULL;
        nbytes += sz;
        if(++nvecs >= AEB_MAX_IOVECS)
          next = APR_BRIGADE_SENTINEL(bb);
        /* printf(" NVECS %d,%d\n",(int)nvecs,(int)nbytes); */
      }
      if(b)
        apr_bucket_delete(b);
    }

    if(nbytes > 0) {
      struct iovec vec[AEB_MAX_IOVECS];
      int n = AEB_MAX_IOVECS;
      apr_size_t written = nbytes;

      assert(output != NULL && !APR_BRIGADE_EMPTY(output));

      AEB_APR_ASSERT(apr_brigade_to_iovec(output,vec,&n));
      st = apr_socket_sendv(state->sock,vec,n,&written);
      assert(st == APR_SUCCESS || st == APR_EAGAIN);
      nbytes -= written;
      if (nbytes > 0 && written > 0) {
        apr_bucket_brigade *m = more;
        more = NULL;
        if(written) {
          AEB_APR_ASSERT(apr_brigade_partition(output,written-1,&b));
        } else
          b = NULL;

        if(b != NULL)
          assert((more = apr_brigade_split_ex(output,b,more)) != NULL);

        if(m) {
          if(more)
            APR_BRIGADE_CONCAT(more,m);
          else
            more = m;
        }
      } else if (written == 0 && output) {
        assert(more == NULL);
        more = output;
        output = NULL;
      }
      if(output)
        apr_brigade_cleanup(output);
    }

#if 0
        st = apr_socket_send(state->sock,str,&written);
        assert(st == APR_SUCCESS || st == APR_EAGAIN);
        nbytes += written;
        if (written < n) {
          str += written;
          n -= written;
          if(more == NULL)
            assert((more = apr_brigade_create(state->p,bb->bucket_alloc)) != NULL);
          apr_brigade_write(more,NULL,NULL,str,n);
          if(temp == NULL)
            assert((temp = apr_brigade_create(input_pool,bb->bucket_alloc)) != NULL);

          next = (b == APR_BRIGADE_FIRST(bb) ? NULL : APR_BUCKET_PREV(b));
          APR_BUCKET_REMOVE(b);
          APR_BRIGADE_INSERT_TAIL(temp,b);
          b = NULL;
          if (st == APR_SUCCESS)
            st = APR_EAGAIN;
        }
    }
#endif

    if(more && !last) {
      assert(state->pending == NULL);
      state->pending = more;
      more = NULL;
      break;
    }
  }

  if((bb && !APR_BRIGADE_EMPTY(bb)) || (state->pending && !APR_BRIGADE_EMPTY(state->pending))) {
    AEB_APR_ASSERT(aeb_event_descriptor_events_or(state->event,APR_POLLOUT));
#if 0
    AEB_APR_ASSERT(aeb_event_callback_set(info->event,brigade_client_write));
#endif
    AEB_APR_ASSERT(aeb_event_add(state->event));
  }

  if (last) {
    apr_interval_time_t timeout = apr_time_from_sec(30);
    apr_bucket_delete(last);
#if 0
    state->write_bb = NULL;
    if (input_pool != info->pool)
      apr_pool_destroy(input_pool);
#endif
    AEB_APR_ASSERT(aeb_event_del(state->event));
    AEB_APR_ASSERT(aeb_event_descriptor_events_set(state->event,APR_POLLIN));
    AEB_APR_ASSERT(aeb_event_callback_set(state->event,memcache_client_read));
    AEB_APR_ASSERT(aeb_event_timeout_set(state->event,&timeout));
    AEB_APR_ASSERT(aeb_event_add(state->event));
    if(mc_shutdown)
      apr_socket_shutdown(state->sock,APR_SHUTDOWN_WRITE);
  }

  if(output)
    apr_brigade_destroy(output);

  return (APR_STATUS_IS_EAGAIN(st) ? APR_SUCCESS : st);
}

AEB_INTERNAL(apr_status_t) aeb_event_brigade_flush(aeb_event_t *ev, aeb_brigade_t *bb)
{
  apr_status_t st;
  apr_bucket *b = apr_bucket_flush_create(bb->bucket_alloc);
  assert(b != NULL);

  APR_BRIGADE_INSERT_TAIL(bb,b);
  AEB_APR_ASSERT(aeb_event_del(ev));
  AEB_APR_ASSERT(aeb_event_descriptor_events_set(ev,APR_POLLOUT));
  AEB_APR_ASSERT(aeb_event_callback_set(ev,brigade_client_write));
  AEB_APR_ASSERT(aeb_event_add(ev));
  return st;
}

AEB_INTERNAL(apr_status_t) aeb_event_brigade_close(aeb_event_t *ev, aeb_brigade_t *bb)
{
  apr_status_t st = APR_SUCCESS;
  apr_bucket *b = apr_bucket_eos_create(bb->bucket_alloc);
  assert(b != NULL);

  APR_BRIGADE_INSERT_TAIL(bb,b);
  if(!(ev->flags & AEB_FLAG_TIMEOUT)) {
    apr_interval_time_t instant = APR_TIME_C(0);

    AEB_APR_ASSERT(aeb_event_del(ev));
    AEB_APR_ASSERT(aeb_event_descriptor_events_not(ev,APR_POLLIN|APR_POLLOUT));
    AEB_APR_ASSERT(aeb_event_timeout_set(ev,&instant));
    printf("set close timeout of 0 sec\n");
    mc_shutdown++;
    AEB_APR_ASSERT(aeb_event_callback_set(ev,brigade_client_write));
    AEB_APR_ASSERT(aeb_event_add(ev));
  }
  return st;
}

static apr_status_t aeb_brigade_flush(apr_bucket_brigade *bb, void *ctx)
{
  aeb_event_t *ev = (aeb_event_t*)ctx;
  assert(ev != NULL);
  return aeb_event_brigade_flush(ev,bb);
}

static apr_status_t memcache_client_read(const aeb_event_info_t *info, void *data)
{
  apr_status_t st;
  mc_client_t *state = (mc_client_t*)data;
  char buf[8192];
  apr_size_t bread = sizeof(buf)-1;

  printf("IN READ\n");
  st = apr_socket_recv(state->sock,buf,&bread);
  if (st == APR_EOF || (st == APR_SUCCESS && bread == 0)) {
    printf("memcache: EOF\n");
  } else if (st == APR_EAGAIN && bread <= 0) {
    aeb_event_add(info->event);
  } else {
    AEB_APR_ASSERT(st);
    buf[bread] = '\0';
    printf("memcache read %u octets.\n",(unsigned)bread);
    printf("---- data ----\n%s\n",buf);
    apr_socket_shutdown(state->sock,APR_SHUTDOWN_WRITE);
    AEB_APR_ASSERT(aeb_event_add(info->event));
    return APR_SUCCESS;
  }
  return aeb_event_loop_terminate();
}

static apr_status_t memcache_client_write(const aeb_event_info_t *info, void *data)
{
  apr_status_t st;
  mc_client_t *state = (mc_client_t*)data;
  const char *cmd = "stats\n";
  apr_size_t bwritten = strlen(cmd)+1;

  printf("FLAGS %d\n",info->flags);
  if(info->flags & APR_POLLOUT)
    printf("WRITE: YES!\n");
  if(info->flags & APR_POLLIN)
    printf("READ: YES\n");
  AEB_APR_ASSERT(apr_socket_send(state->sock,cmd,&bwritten));
  printf("sent memcached '%s' (%u bytes)\n",cmd,(unsigned)bwritten);

  AEB_APR_ASSERT(aeb_event_descriptor_events_set(info->event,APR_POLLIN));
  AEB_APR_ASSERT(aeb_event_callback_set(info->event,memcache_client_read));
  AEB_APR_ASSERT(aeb_event_add(info->event));
  return st;
}

static apr_bucket_brigade *create_brigade(mc_client_t *state, apr_bucket_alloc_t **alloc)
{
  apr_bucket_alloc_t *balloc = NULL;
  apr_bucket_brigade *bb = NULL;

  if(alloc) {
    if(*alloc)
      balloc = *alloc;
    else
      *alloc = balloc = apr_bucket_alloc_create(state->p);
  } else
    balloc = apr_bucket_alloc_create(state->p);

  assert(alloc != NULL);
  bb = apr_brigade_create(state->p,balloc);
  assert(bb != NULL);
  return bb;
}

static void create_read_brigade(mc_client_t *state, apr_bucket_brigade **bbp)
{
  apr_bucket_alloc_t *alloc = NULL;
  apr_bucket *b = NULL;

  *bbp = create_brigade(state,&alloc);
  assert(*bbp != NULL);
  assert(alloc != NULL);

  b = apr_bucket_socket_create(state->sock,alloc);
#if 0
  switch(state->pollfd.desc_type) {
    case APR_POLL_SOCKET:
      b = apr_bucket_socket_create(state->pollfd.desc.s,alloc);
      break;
    case APR_POLL_FILE:
      b = apr_bucket_pipe_create(state->pollfd.desc.f,alloc);
      break;
    default:
      AEB_ASSERTV(state->pollfd.desc_type == APR_POLL_SOCKET,
          "unsupported apr_pollfd_t descriptor type: %d",(int)state->pollfd.desc_type);
      break;
  }
#endif
  assert(b != NULL);
  APR_BRIGADE_INSERT_TAIL(*bbp,b);
}

static void create_write_brigade(mc_client_t *state, apr_bucket_brigade **bbp)
{
  apr_bucket_alloc_t *alloc = NULL;

  state->pending = NULL;
  *bbp = create_brigade(state,&alloc);
  assert(bbp && alloc);

}
static apr_status_t memcache_client_connect(const aeb_event_info_t *info, void *data)
{
  apr_status_t st;
  apr_sockaddr_t *sa = NULL;
  mc_client_t *state = (mc_client_t*)data;
  char *ip = NULL;
  assert(apr_socket_addr_get(&sa,APR_REMOTE,state->sock) == APR_SUCCESS);
  assert(apr_sockaddr_ip_get(&ip,sa) == APR_SUCCESS);
  printf("connected to %s:%u\n",ip,(unsigned)state->port);
  if(info->flags & APR_POLLIN) {
    char buf;
    apr_size_t bread = 0;

    st = apr_socket_recv(state->sock,&buf,&bread);
    if(st != APR_EAGAIN && st != APR_SUCCESS) {
      if(st == APR_ECONNREFUSED) {
        printf("memcache: connection refushed\n");
        aeb_event_loop_terminate();
        return st;
      }
      AEB_APR_ASSERT(st);
    }
  }

  if(!state->read_bb)
    create_read_brigade(state,&state->read_bb);
  if(!state->write_bb)
    create_write_brigade(state,&state->write_bb);

  AEB_APR_ASSERT(apr_brigade_printf(state->write_bb,NULL,NULL,
      "This is a really long and stupid\nfoo bar baz\n1 2 3 4\n"
      "connected to %s:%u\n",ip,(unsigned)state->port));
  AEB_APR_ASSERT(aeb_event_brigade_flush(info->event,state->write_bb));
  return st;
}

static apr_status_t tester(void *x)
{
  printf("info->pool cleanup\n");
  fflush(stdout);
  return APR_SUCCESS;
}

static apr_status_t memcache_client_timeout(const aeb_event_info_t *info, void *data)
{
  apr_status_t st = APR_SUCCESS;
  mc_client_t *state = (mc_client_t*)data;

  apr_pool_cleanup_kill(info->pool,info->pool,tester);
  apr_pool_cleanup_register(info->pool,info->pool,tester,apr_pool_cleanup_null);
  if(state->event && state->write_bb) {
    state->sleep_tick--;
    if(state->sleep_tick > 0) {
      char *msg = apr_psprintf(info->pool,"SLEEP_TICK (%d) ["HEXFMT"]",
                                (int)state->sleep_tick,HEX(info->pool));

      fputs(msg,stdout);
      apr_brigade_puts(state->write_bb,aeb_brigade_flush,state->event,msg);
      apr_brigade_putc(state->write_bb,aeb_brigade_flush,state->event,'\n');
      if(!aeb_event_is_active(state->event,APR_POLLOUT)) {
        printf(" <flush>\n");
        aeb_event_brigade_flush(state->event,state->write_bb);
      } else printf("\n");
      aeb_event_add(info->event);
    } else {
      printf("closing 1\n");
      fflush(stdout);
      AEB_APR_ASSERT(aeb_event_brigade_close(state->event,state->write_bb));
      AEB_APR_ASSERT(aeb_event_add(state->event));
    }
  } else
    st = aeb_event_loop_terminate();
  return st;
}

void * APR_THREAD_FUNC memcache_client_thread(apr_thread_t *t, void *data)
{
  apr_status_t st;
  const char *server = NULL;
  char *scope_id = NULL;
  apr_sockaddr_t *sa = NULL;
  apr_pool_t *pool = NULL,*tls_pool = aeb_thread_static_pool_acquire();
  mc_client_t *state = NULL;
  aeb_event_t *event = NULL;
  aeb_event_t *timeout_event = NULL;

  AEB_APR_ASSERT(apr_pool_create(&pool,tls_pool));
  ASSERT((state = apr_pcalloc(pool,sizeof(mc_client_t))) != NULL);
  state->p = pool;
  state->thread = t;
  state->sleep_time = MC_SLEEP_INTERVAL;
  state->sleep_tick = MC_SLEEP_TICK;
  if (data)
    server = apr_pstrdup(pool,(char*)data);
  else
    server = apr_pstrdup(pool,DEFAULT_SERVER);
  AEB_APR_ASSERT(apr_parse_addr_port(&state->host,&scope_id,&state->port,server,pool));
  ASSERT(scope_id == NULL);
  ASSERT(state->host != NULL);
  printf("HOST: %s  PORT: %u\n",state->host,(unsigned)state->port);
  if(state->port == 0)
    state->port = 11211;
  AEB_APR_ASSERT(apr_sockaddr_info_get(&sa,state->host,APR_UNSPEC,state->port,APR_IPV4_ADDR_OK,pool));
  AEB_APR_ASSERT(apr_socket_create(&state->sock,APR_INET,SOCK_STREAM,APR_PROTO_TCP,pool));
  AEB_APR_ASSERT(apr_socket_opt_set(state->sock,APR_SO_LINGER,1));
  AEB_APR_ASSERT(apr_socket_opt_set(state->sock,APR_SO_NONBLOCK,1));
  AEB_APR_ASSERT(apr_socket_opt_set(state->sock,APR_SO_REUSEADDR,1));
  st = apr_socket_connect(state->sock,sa);
  ASSERT(st == APR_SUCCESS || st == APR_EINPROGRESS);

  AEB_APR_ASSERT(aeb_event_create_ex(pool,memcache_client_connect,
                          make_socket_pollfd(state->sock,pool),NULL,&event));
  ASSERT(event != NULL);
  state->event = event;
  AEB_APR_ASSERT(aeb_event_flags_set(event,AEB_FLAG_SUBPOOL));
  AEB_APR_ASSERT(aeb_event_user_context_set(event,state));
  AEB_APR_ASSERT(aeb_timer_create(pool,memcache_client_timeout,state->sleep_time,&timeout_event));
  AEB_APR_ASSERT(aeb_event_user_context_set(timeout_event,state));
  AEB_APR_ASSERT(aeb_event_flags_set(timeout_event,AEB_FLAG_SUBPOOL));
  ASSERT(timeout_event != NULL);

  AEB_APR_ASSERT(aeb_event_add(event));
  AEB_APR_ASSERT(aeb_event_add(timeout_event));
  st = aeb_event_loop(NULL);
  if (st != APR_SUCCESS && st != APR_EINTR) {
    AEB_APR_ASSERT(st);
  }
  aeb_thread_static_pool_release();
  return NULL;
}
