#include "internal.h"

AEB_API(void) apr_array_clear(apr_array_header_t *arr)
{
  arr->nelts = 0;
}
