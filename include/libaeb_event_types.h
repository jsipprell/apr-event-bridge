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
  aeb_event_type_reserved = 0xfffe,
#define AEB_RESERVED_EVENT aeb_event_type_reserved
  aeb_event_type_null = 0xffff,
#define AEB_NULL_EVENT aeb_event_type_null
} aeb_event_type_t;

#define AEB_EVENT_IS_TYPE(e,t) ((e)->type == aeb_event_type##t)
#define AEB_EVENT_GET_TYPE(e) ((aeb_event_type_t)((e)->type))
#define AEB_EVENT_INFO(o,evtype) \
  ((o)->type == aeb_event_type_##evtype ? ((o)->d.evtype##_data) : NULL)
#define AEB_EVENT_IS_NULL(o) ((o)->type == aeb_event_type_null)
#define AEB_RPC_EVENT_INFO(o) AEB_EVENT_INFO(o,rpc)
#define AEB_DESCRIPTOR_EVENT_INFO(o) AEB_EVENT_INFO(o,descriptor)
#define AEB_TIMER_EVENT_INFO(o) AEB_EVENT_INFO(o,timer)
#define AEB_SIGNAL_EVENT_INFO(o) AEB_EVENT_INFO(o,signal)

#endif /* _LIBAEB_EVENT_TYPES_H */