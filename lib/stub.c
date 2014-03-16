#include "internal.h"


/* This function is only here so that libcompat can build when no
   replacement objects are needed (empty libraries are not portable)
 */
AEB_API(const char*) aeb_compatibility_library_version(void)
{
  return LIBAEB_VERSION;
}
