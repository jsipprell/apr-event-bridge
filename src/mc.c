/* libaeb testing memcache client thread */
#include "internal.h"
#include "util.h"
#include <libaeb_assert.h>
#include <libaeb_event_info.h>

#include <apr_network_io.h>
#include <apr_thread_proc.h>
#include <apr_time.h>

#define MC_SLEEP_INTERVAL APR_TIME_C(10000000)
#define DEFAULT_SERVER "localhost:11211"

typedef struct {
  apr_pool_t *p;
  apr_thread_t *thread;
  apr_time_t sleep_time;
  apr_socket_t *sock;
  char *host;
  apr_port_t port;
  aeb_event_t *event;
  apr_pollfd_t pollfd;
} mc_client_t;

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

static apr_status_t memcache_client_read(const aeb_event_info_t *info, void *data)
{
  apr_status_t st;
  mc_client_t *state = (mc_client_t*)data;
  char buf[8192];
  apr_size_t bread = sizeof(buf)-1;

  st = apr_socket_recv(state->sock,buf,&bread);
  if (st == APR_EOF || (st == APR_SUCCESS && bread == 0)) {
    printf("memcache: EOF\n");
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
  AEB_APR_ASSERT(aeb_event_descriptor_events_set(info->event,APR_POLLOUT));
  AEB_APR_ASSERT(aeb_event_callback_set(info->event,memcache_client_write));
  AEB_APR_ASSERT(aeb_event_add(info->event));

  return st;
}

static apr_status_t memcache_client_timeout(const aeb_event_info_t *info, void *data)
{
  mc_client_t *state = (mc_client_t*)data;

  printf("TIMEOUT\n");
  if(state->event)
    aeb_event_del(state->event);

  return aeb_event_loop_terminate();
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
  AEB_APR_ASSERT(aeb_event_user_context_set(event,state));
  AEB_APR_ASSERT(aeb_timer_create(pool,memcache_client_timeout,state->sleep_time,&timeout_event));
  AEB_APR_ASSERT(aeb_event_user_context_set(timeout_event,state));
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