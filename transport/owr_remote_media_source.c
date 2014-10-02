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

#include <gst/gst.h>


#define OWR_REMOTE_MEDIA_SOURCE_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_REMOTE_MEDIA_SOURCE, OwrRemoteMediaSourcePrivate))

G_DEFINE_TYPE(OwrRemoteMediaSource, owr_remote_media_source, OWR_TYPE_MEDIA_SOURCE)

struct _OwrRemoteMediaSourcePrivate {
    guint stream_id;
};

static GstPad *owr_remote_media_source_get_pad(OwrMediaSource *media_source, GstCaps *caps);


static void owr_remote_media_source_class_init(OwrRemoteMediaSourceClass *klass)
{
    OwrMediaSourceClass *media_source_class = OWR_MEDIA_SOURCE_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrRemoteMediaSourcePrivate));

    media_source_class->get_pad = (void *(*)(OwrMediaSource *, void *))owr_remote_media_source_get_pad;
}

static void owr_remote_media_source_init(OwrRemoteMediaSource *source)
{
    source->priv = OWR_REMOTE_MEDIA_SOURCE_GET_PRIVATE(source);

    source->priv->stream_id = 0;
}


static GstPad *owr_remote_media_source_get_pad(OwrMediaSource *media_source, GstCaps *caps)
{
    OwrRemoteMediaSource *remote_source;
    GstPad *ghostpad = NULL;
    gchar *pad_name = NULL;
    OwrMediaType media_type;
    OwrCodecType codec_type;

    OWR_UNUSED(caps);

    g_assert(media_source);
    remote_source = OWR_REMOTE_MEDIA_SOURCE(media_source);

    codec_type = _owr_media_source_get_codec(media_source);
    g_object_get(media_source, "media-type", &media_type, NULL);

    if (media_type == OWR_MEDIA_TYPE_VIDEO)
        pad_name = g_strdup_printf("video_src_%u_%u", codec_type, remote_source->priv->stream_id);
    else if (media_type == OWR_MEDIA_TYPE_AUDIO)
        pad_name = g_strdup_printf("audio_raw_src_%u", remote_source->priv->stream_id);
    ghostpad = gst_element_get_static_pad(
        _owr_media_source_get_element(media_source), pad_name);
    g_free(pad_name);

    return ghostpad;
}

OwrRemoteMediaSource *_owr_remote_media_source_new(OwrMediaType media_type,
    guint stream_id, OwrCodecType codec_type, GstElement *transport_bin)
{
    OwrRemoteMediaSource *source;
    GEnumClass *enum_class;
    GEnumValue *enum_value;
    gchar *name;

    enum_class = G_ENUM_CLASS(g_type_class_ref(OWR_TYPE_MEDIA_TYPE));
    enum_value = g_enum_get_value(enum_class, media_type);
    name = g_strdup_printf("Remote %s stream", enum_value ? enum_value->value_nick : "unknown");
    g_type_class_unref(enum_class);

    source = g_object_new(OWR_TYPE_REMOTE_MEDIA_SOURCE,
        "name", name,
        "media-type", media_type,
        NULL);

    source->priv->stream_id = stream_id;

    g_free(name);

    /* take a ref on the transport bin as the media source element is
     * unreffed on finalization */
    g_object_ref(transport_bin);
    _owr_media_source_set_element(OWR_MEDIA_SOURCE(source), transport_bin);

    _owr_media_source_set_codec(OWR_MEDIA_SOURCE(source), codec_type);

    return source;
}
