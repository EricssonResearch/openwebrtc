/*/
\*\ OwrDeviceList private
/*/

#ifndef device_list_private_h
#define device_list_private_h

#include "owr_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

/*
 * Callback signature is (GList *owrMediaSources, gpointer user_data)
 */
void _owr_get_capture_devices(OwrMediaType types, GClosure *callback);

G_END_DECLS

#endif /* device_list_private_h */
