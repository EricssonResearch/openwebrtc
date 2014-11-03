/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
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

/*/
\*\ OwrRemoteMediaSource
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_remote_media_source.h"

#include "owr_media_source.h"
#include "owr_media_source_private.h"
#include "owr_types.h"
#include "owr_utils.h"
#include "owr_private.h"

#include <gst/gst.h>

#define OWR_REMOTE_MEDIA_SOURCE_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_REMOTE_MEDIA_SOURCE, OwrRemoteMediaSourcePrivate))

G_DEFINE_TYPE(OwrRemoteMediaSource, owr_remote_media_source, OWR_TYPE_MEDIA_SOURCE)

struct _OwrRemoteMediaSourcePrivate {
    guint stream_id;
};

static void owr_remote_media_source_class_init(OwrRemoteMediaSourceClass *klass)
{
    g_type_class_add_private(klass, sizeof(OwrRemoteMediaSourcePrivate));
}

static void owr_remote_media_source_init(OwrRemoteMediaSource *source)
{
    source->priv = OWR_REMOTE_MEDIA_SOURCE_GET_PRIVATE(source);

    source->priv->stream_id = 0;
}

#define LINK_ELEMENTS(a, b) \
    if (!gst_element_link(a, b)) \
        GST_ERROR("Failed to link " #a " -> " #b);

OwrRemoteMediaSource *_owr_remote_media_source_new(OwrMediaType media_type,
    guint stream_id, OwrCodecType codec_type, GstElement *transport_bin)
{
    OwrRemoteMediaSource *source;
    OwrRemoteMediaSourcePrivate *priv;
    GEnumClass *enum_class;
    GEnumValue *enum_value;
    gchar *name;
    GstElement *transport_pipeline;
    GstElement *source_bin, *tee;
    GstElement *fakesink, *queue;
    GstPad *srcpad, *sinkpad, *ghostpad;
    gchar *pad_name, *bin_name;

    enum_class = G_ENUM_CLASS(g_type_class_ref(OWR_TYPE_MEDIA_TYPE));
    enum_value = g_enum_get_value(enum_class, media_type);
    name = g_strdup_printf("Remote %s stream", enum_value ? enum_value->value_nick : "unknown");
    g_type_class_unref(enum_class);

    source = g_object_new(OWR_TYPE_REMOTE_MEDIA_SOURCE,
        "name", name,
        "media-type", media_type,
        NULL);

    priv = source->priv;
    priv->stream_id = stream_id;
    g_free(name);

    /* take a ref on the transport bin as the media source element is
     * unreffed on finalization */
    g_object_ref(transport_bin);
    _owr_media_source_set_codec(OWR_MEDIA_SOURCE(source), codec_type);

    /* create source tee and everything */
    if (media_type == OWR_MEDIA_TYPE_VIDEO) {
        bin_name = g_strdup_printf("video-src-%u-%u", codec_type, stream_id);
        pad_name = g_strdup_printf("video_src_%u_%u", codec_type, stream_id);
    } else if (media_type == OWR_MEDIA_TYPE_AUDIO) {
        bin_name = g_strdup_printf("audio-src-%u-%u", codec_type, stream_id);
        pad_name = g_strdup_printf("audio_raw_src_%u", stream_id);
    } else {
        g_assert_not_reached();
    }

    source_bin = gst_bin_new(bin_name);
    _owr_media_source_set_source_bin(OWR_MEDIA_SOURCE(source), source_bin);
    tee = gst_element_factory_make ("tee", "tee");
    _owr_media_source_set_source_tee(OWR_MEDIA_SOURCE(source), tee);
    fakesink = gst_element_factory_make ("fakesink", "fakesink");
    g_object_set(fakesink, "async", FALSE, NULL);
    queue = gst_element_factory_make ("queue", "queue");
    g_free(bin_name);

    transport_pipeline = GST_ELEMENT(gst_element_get_parent(transport_bin));
    gst_bin_add_many(GST_BIN(source_bin), tee, queue, fakesink, NULL);
    LINK_ELEMENTS(tee, queue);
    LINK_ELEMENTS(queue, fakesink);
    sinkpad = gst_element_get_static_pad(tee, "sink");
    ghostpad = gst_ghost_pad_new("sink", sinkpad);
    gst_object_unref(sinkpad);
    gst_element_add_pad(source_bin, ghostpad);
    gst_bin_add(GST_BIN(transport_pipeline), source_bin);
    gst_element_sync_state_with_parent(source_bin);
    gst_object_unref(transport_pipeline);

    /* Link the transport bin to our tee */
    srcpad = gst_element_get_static_pad(transport_bin, pad_name);
    g_free(pad_name);
    if (gst_pad_link(srcpad, ghostpad) != GST_PAD_LINK_OK) {
        GST_ERROR("Failed to link source bin to the outside");
    }

    return source;
}
