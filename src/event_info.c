/* generic event_info support */
#include "internal.h"

#include <libaeb_event_info.h>

#define AEB_INFO_NAME(t) "AEB:event_info" APR_STRINGIFY(t)

static inline const char *event_info_key(aeb_event_type_t evtype)
{
  const char *name;

  switch(evtype) {
  case AEB_RPC_EVENT:
    name = AEB_INFO_NAME(rpc);
    break;
  case AEB_DESCRIPTOR_EVENT:
    name = AEB_INFO_NAME(descriptor);
    break;
  case AEB_TIMER_EVENT:
    name = AEB_INFO_NAME(timer);
    break;
  case AEB_SIGNAL_EVENT:
    name = AEB_INFO_NAME(signal);
    break;
  case AEB_RESERVED_EVENT:
    name = AEB_INFO_NAME(reserved);
    break;
  case AEB_NULL_EVENT:
    name = NULL;
    break;
  default:
    aeb_abort("invalid event type %d",(int)evtype);
    break;
  }
  return name;
}

AEB_INTERNAL(const aeb_event_info_t*) aeb_event_info_new_ex(aeb_event_t *ev,
                                                            aeb_event_type_t evtype,
                                                            const void *data,
                                                            apr_uint16_t flags,
                                                            apr_pool_t *pool)
{
  aeb_event_info_t *info = NULL;

  if(!pool)
    pool = ev->pool;

  if(evtype == aeb_event_type_null)
    evtype = ev->type;

  if(pool == ev->pool)
    apr_pool_userdata_get((void**)&info,event_info_key(evtype),pool);

  if(info == NULL) {
    ASSERT((info = apr_pcalloc(pool,sizeof(aeb_event_info_t))) != NULL);
    info->type = evtype;
    apr_pool_userdata_set(info,event_info_key(evtype),NULL,pool);
  } else
    info->type = evtype;

  if(ev)
    info->event = ev;

  switch(AEB_EVENT_GET_TYPE(info)) {
  case AEB_RPC_EVENT:
    AEB_RPC_EVENT_DATA(info) = data;
    break;
  case AEB_DESCRIPTOR_EVENT:
    AEB_DESCRIPTOR_EVENT_DATA(info) = (const apr_pollfd_t*)data;
    break;
  case AEB_TIMER_EVENT:
    AEB_TIMER_EVENT_DATA(info) = data;
    break;
  case AEB_SIGNAL_EVENT:
    AEB_SIGNAL_EVENT_DATA(info) = data;
    break;
  case AEB_RESERVED_EVENT:
    AEB_RESERVED_EVENT_DATA(info) = data;
    break;
  default:
    aeb_abort("unsupported event info type");
    break;
  }

  info->flags = flags;
  return info;
}