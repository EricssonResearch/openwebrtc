/*
 * Copyright (c) 2014-2015, Ericsson AB. All rights reserved.
 * Copyright (c) 2014, Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifndef __OWR_PRIVATE_H__
#define __OWR_PRIVATE_H__

#include "owr_message_origin_private.h"

#include <glib.h>

#include <gst/gst.h>

#ifndef __GTK_DOC_IGNORE__

#define OWR_OBJECT_NAME_LENGTH_MAX 100

G_BEGIN_DECLS

/*< private >*/
gboolean _owr_is_initialized(void);
GMainContext * _owr_get_main_context(void);
GstClockTime _owr_get_base_time(void);
void _owr_schedule_with_user_data(GSourceFunc func, gpointer user_data);
void _owr_schedule_with_hash_table(GSourceFunc func, GHashTable *hash_table);
GHashTable *_owr_create_schedule_table_func(OwrMessageOrigin *origin, const gchar *function_name);

#define _owr_create_schedule_table(origin) _owr_create_schedule_table_func(origin, __FUNCTION__)

G_END_DECLS

#endif /* __GTK_DOC_IGNORE__ */

#endif /* __OWR_PRIVATE_H__ */
