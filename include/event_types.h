#ifndef _LIBAEB_INTERNAL_EVENT_TYPES_H
#define _LIBAEB_INTERNAL_EVENT_TYPES_H

/* This is just a faster version of libaeb_event_types.h for internal only use */

#define AEB_EVENT_DATA(o,evtype) ((o)->d.evtype##_data)

#define AEB_RPC_EVENT_DATA(o) AEB_EVENT_DATA(o,rpc)
#define AEB_DESCRIPTOR_EVENT_DATA(o) AEB_EVENT_DATA(o,descriptor)
#define AEB_TIMER_EVENT_DATA(o) AEB_EVENT_DATA(o,timer)
#define AEB_SIGNAL_EVENT_DATA(o) AEB_EVENT_DATA(o,signal)
#define AEB_BRIGADE_EVENT_DATA(o) AEB_EVENT_DATA(o,brigade)
#define AEB_BUCKET_EVENT_DATA(o) AEB_EVENT_DATA(o,brigade)
#define AEB_RESERVED_EVENT_DATA(o) AEB_EVENT_DATA(o,reserved)

#endif /* _LIBAEB_INTERNAL_EVENT_TYPES_H */