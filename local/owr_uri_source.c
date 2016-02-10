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

/*/
\*\ OwrURISource
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_uri_source.h"

#include "owr_media_source.h"
#include "owr_media_source_private.h"
#include "owr_private.h"
#include "owr_uri_source_private.h"
#include "owr_types.h"
#include "owr_utils.h"

#include <gst/gst.h>

#define OWR_URI_SOURCE_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_URI_SOURCE, OwrURISourcePrivate))

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface);

G_DEFINE_TYPE_WITH_CODE(OwrURISource, owr_uri_source, OWR_TYPE_MEDIA_SOURCE,
    G_IMPLEMENT_INTERFACE(OWR_TYPE_MESSAGE_ORIGIN, owr_message_origin_interface_init))

struct _OwrURISourcePrivate {
    guint stream_id;
    OwrMessageOriginBusSet *message_origin_bus_set;
};

static void owr_uri_source_finalize(GObject *object)
{
    OwrURISource *source = OWR_URI_SOURCE(object);

    owr_message_origin_bus_set_free(source->priv->message_origin_bus_set);
    source->priv->message_origin_bus_set = NULL;
}

static void owr_uri_source_class_init(OwrURISourceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrURISourcePrivate));

    gobject_class->finalize = owr_uri_source_finalize;
}

static gpointer owr_uri_source_get_bus_set(OwrMessageOrigin *origin)
{
    return OWR_URI_SOURCE(origin)->priv->message_origin_bus_set;
}

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface)
{
    interface->get_bus_set = owr_uri_source_get_bus_set;
}

static void owr_uri_source_init(OwrURISource *source)
{
    source->priv = OWR_URI_SOURCE_GET_PRIVATE(source);

    source->priv->stream_id = 0;
    source->priv->message_origin_bus_set = owr_message_origin_bus_set_new();
}

#define LINK_ELEMENTS(a, b) \
    if (!gst_element_link(a, b)) \
        GST_ERROR("Failed to link " #a " -> " #b);

OwrMediaSource *_owr_uri_source_new(OwrMediaType media_type,
    guint stream_id, OwrCodecType codec_type, gpointer uridecodebin_ptr)
{
    GstElement *uridecodebin = GST_ELEMENT(uridecodebin_ptr);
    OwrURISource *source;
    OwrURISourcePrivate *priv;
    GEnumClass *enum_class;
    GEnumValue *enum_value;
    gchar *name;
    GstElement *uri_pipeline;
    GstElement *identity;
    GstElement *source_bin, *tee;
    GstElement *fakesink, *queue;
    GstPad *srcpad, *sinkpad, *ghostpad;
    gchar *pad_name, *bin_name;

    enum_class = G_ENUM_CLASS(g_type_class_ref(OWR_TYPE_MEDIA_TYPE));
    enum_value = g_enum_get_value(enum_class, media_type);
    name = g_strdup_printf("%s stream, id: %u", enum_value ? enum_value->value_nick : "unknown", stream_id);
    g_type_class_unref(enum_class);

    source = g_object_new(OWR_TYPE_URI_SOURCE,
        "name", name,
        "media-type", media_type,
        NULL);

    priv = source->priv;
    priv->stream_id = stream_id;
    g_free(name);

    _owr_media_source_set_codec(OWR_MEDIA_SOURCE(source), codec_type);

    /* create source tee and everything */
    if (media_type == OWR_MEDIA_TYPE_VIDEO)
        bin_name = g_strdup_printf("video-src-%u-%u", codec_type, stream_id);
    else if (media_type == OWR_MEDIA_TYPE_AUDIO)
        bin_name = g_strdup_printf("audio-src-%u-%u", codec_type, stream_id);
    else
        g_assert_not_reached();
    pad_name = g_strdup_printf("src_%u", stream_id);

    source_bin = gst_bin_new(bin_name);
    _owr_media_source_set_source_bin(OWR_MEDIA_SOURCE(source), source_bin);
    identity = gst_element_factory_make("identity", "identity");
    g_object_set(identity, "sync", TRUE, NULL);
    tee = gst_element_factory_make("tee", "tee");
    _owr_media_source_set_source_tee(OWR_MEDIA_SOURCE(source), tee);
    fakesink = gst_element_factory_make("fakesink", "fakesink");
    g_object_set(fakesink, "async", FALSE, NULL);
    g_object_set(fakesink, "enable-last-sample", FALSE, NULL);
    queue = gst_element_factory_make("queue", "queue");
    g_free(bin_name);

    uri_pipeline = GST_ELEMENT(gst_element_get_parent(uridecodebin));
    gst_bin_add_many(GST_BIN(source_bin), identity, tee, queue, fakesink, NULL);
    LINK_ELEMENTS(identity, tee);
    LINK_ELEMENTS(tee, queue);
    LINK_ELEMENTS(queue, fakesink);
    sinkpad = gst_element_get_static_pad(identity, "sink");
    ghostpad = gst_ghost_pad_new("sink", sinkpad);
    gst_object_unref(sinkpad);
    gst_element_add_pad(source_bin, ghostpad);
    gst_bin_add(GST_BIN(uri_pipeline), source_bin);
    gst_element_sync_state_with_parent(source_bin);
    gst_object_unref(uri_pipeline);

    /* Link the uridecodebin to our tee */
    srcpad = gst_element_get_static_pad(uridecodebin, pad_name);
    g_free(pad_name);
    if (gst_pad_link(srcpad, ghostpad) != GST_PAD_LINK_OK)
        GST_ERROR("Failed to link source bin to the outside");

    return OWR_MEDIA_SOURCE(source);
}
