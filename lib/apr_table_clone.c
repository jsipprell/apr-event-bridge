#include "internal.h"

ZEKE_API(apr_table_t*) apr_table_clone(apr_pool_t *pool, const apr_table_t *table)
{
  apr_table_t *new_table;
  const apr_array_header_t *ta = NULL;
  int nelts = 0;

  if(!apr_is_empty_table(table)) {
    ta = apr_table_elts(table);
    nelts= ta->nelts;
  }

  new_table = apr_table_make(pool,nelts);
  assert(new_table != NULL);

  if(!apr_is_empty_array(ta)) {
    int i;
    apr_table_entry_t *te = (apr_table_entry_t*)ta->elts;
    for(i = 0; i < ta->nelts; i++)
      apr_table_set(new_table,te[i].key,te[i].val);
  }

  return new_table;
}
