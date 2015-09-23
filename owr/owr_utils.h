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

#ifndef __OWR_UTILS_H__
#define __OWR_UTILS_H__

#include "owr_types.h"

#include <glib.h>
#include <gst/gst.h>

#ifndef __GTK_DOC_IGNORE__

G_BEGIN_DECLS

#define OWR_UNUSED(x) (void)x

void *_owr_require_symbols(void);
OwrCodecType _owr_caps_to_codec_type(GstCaps *caps);
void _owr_utils_call_closure_with_list(GClosure *callback, GList *list);
GClosure *_owr_utils_list_closure_merger_new(GClosure *final_callback,
    GCopyFunc list_item_copy,
    GDestroyNotify list_item_destroy);

/* FIXME: This should be removed when the GStreamer required version
 * is 1.6 and gst_caps_foreach() can be used.
 * Upstream commit: http://cgit.freedesktop.org/gstreamer/gstreamer/commit/?id=bc11a1b79dace8ca73d3367d7c70629f8a6dd7fd
 * The author of the above commit, Sebastian Dröge, agreed
 * relicensing this copy of the function under BSD 2-Clause. */
typedef gboolean (*OwrGstCapsForeachFunc) (GstCapsFeatures *features,
                                           GstStructure    *structure,
                                           gpointer         user_data);
gboolean _owr_gst_caps_foreach(const GstCaps *caps, OwrGstCapsForeachFunc func, gpointer user_data);

void _owr_deep_notify(GObject *object, GstObject *orig,
    GParamSpec *pspec, gpointer user_data);

int _owr_rotation_and_mirror_to_video_flip_method(guint rotation, gboolean mirror);

GHashTable *_owr_value_table_new();
GValue *_owr_value_table_add(GHashTable *table, const gchar *key, GType type);

G_END_DECLS

#endif /* __GTK_DOC_IGNORE__ */

#endif /* __OWR_UTILS_H__ */
