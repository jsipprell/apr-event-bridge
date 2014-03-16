/* testing for libaeb */

#include "internal.h"
#include "util.h"

static apr_pool_t *root_pool = NULL;

static void test_aeb(void)
{
  struct event_base *base;
  printf("test_aeb pass 1\n");
  AEB_ASSERT((base = aeb_event_base()) != NULL,"aeb_event_base is null");
  printf("test aeb pass 2, base = %ux\n",(unsigned)base);
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