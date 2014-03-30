#ifndef _LIBAEB_EVENT_TYPES_H
#define _LIBAEB_EVENT_TYPES_H

#include <libaeb.h>

typedef enum aeb_event_type_e {
  /* The ordering and sequence of these is important! */
  aeb_event_type_rpc = 0,
#define AEB_RPC_EVENT aeb_event_type_rpc
  aeb_event_type_descriptor = 1,
#define AEB_DESCRIPTOR_EVENT aeb_event_type_descriptor
  aeb_event_type_timer = 2,
#define AEB_TIMER_EVENT aeb_event_type_timer
  aeb_event_type_signal = 3,
#define AEB_SIGNAL_EVENT aeb_event_type_signal
  aeb_event_type_brigade = 4,
#define AEB_BRIGADE_EVENT aeb_event_type_brigade
#define AEB_BUCKET_EVENT aeb_event_type_brigade
  aeb_event_type_reserved = 0xfffe,
#define AEB_RESERVED_EVENT aeb_event_type_reserved
  aeb_event_type_null = 0xffff,
#define AEB_NULL_EVENT aeb_event_type_null
} aeb_event_type_t;

#define AEB_EVENT_IS_TYPE(e,t) ((e)->type == aeb_event_type_##t)
#define AEB_EVENT_IS_RPC(e) AEB_EVENT_IS_TYPE(e,rpc)
#define AEB_EVENT_IS_DESCRIPTOR(e) AEB_EVENT_IS_TYPE(e,descriptor)
#define AEB_EVENT_IS_TIMER(e) AEB_EVENT_IS_TYPE(e,timer)
#define AEB_EVENT_IS_SIGNAL(e) AEB_EVENT_IS_TYPE(e,signal)
#define AEB_EVENT_IS_BRIGADE(e) AEB_EVENT_IS_TYPE(e,brigade)
#define AEB_EVENT_IS_BUCKET(e) AEB_EVENT_IS_TYPE(e,brigade)
#define AEB_EVENT_IS_RESERVED(e) AEB_EVENT_IS_TYPE(e,reserved)
#define AEB_EVENT_IS_NULL(e) AEB_EVENT_IS_TYPE(e,null)

#define AEB_EVENT_GET_TYPE(e) ((aeb_event_type_t)((e)->type))
#define AEB_EVENT_INFO(o,evtype) \
  ((o)->type == aeb_event_type_##evtype ? ((o)->d.evtype##_data) : NULL)
#define AEB_RPC_EVENT_INFO(o) AEB_EVENT_INFO(o,rpc)
#define AEB_RPC_EVENT_INFO(o) AEB_EVENT_INFO(o,rpc)
#define AEB_DESCRIPTOR_EVENT_INFO(o) AEB_EVENT_INFO(o,descriptor)
#define AEB_TIMER_EVENT_INFO(o) AEB_EVENT_INFO(o,timer)
#define AEB_SIGNAL_EVENT_INFO(o) AEB_EVENT_INFO(o,signal)
#define AEB_BRIGADE_EVENT_INFO(o) AEB_EVENT_INFO(o,brigade)
#define AEB_BUCKET_EVENT_INFO(o) AEB_EVENT_INFO(o,brigade)

#endif /* _LIBAEB_EVENT_TYPES_H */
