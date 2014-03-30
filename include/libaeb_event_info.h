#ifndef _LIBAEB_EVENT_INFO
#define _LIBAEB_EVENT_INFO

#include <libaeb_event_types.h>

/* publically accessible information structure passed to callbacks containing information
 * regarding the event.
 */
struct aeb_event_info {
  apr_pool_t *pool;
  aeb_event_t *event;  /* opaque event handle */
  aeb_event_type_t type;
  apr_uint16_t flags;

  /* NB: The contents of the following union cannot be arbitrarly changed without
   * also changing AEB internals.
   */
  union {
    const void *reserved_data;
    const void *rpc_data;
    const apr_pollfd_t *descriptor_data;
    const void *timer_data;
    const void *signal_data;
    const apr_bucket_brigade *brigade_data;
  } d;

  /* AWLAYS use the macros from libaeb_event_types.h to access the above union,
   * i.e.:
   *
   * static void apr_status_t callback(const aeb_event_info_t *info, void *data)
   * {
   *   const apr_pollfd_t *desc = AEB_DESCRIPTOR_EVENT_INFO(info);
   *   ...
   * }
   *
   * This will ensure that `desc` is NULL unless the event really is a descriptor type
   * event and has it's descriptor_data member set.
   */
};

#endif /* _LIBAEB_EVENT_INFO */
