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

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && !TARGET_IPHONE_SIMULATOR
#if TARGET_OS_IPHONE
#define VIDEO_CONVERT "ercolorspace"
#else
#define VIDEO_CONVERT "videoconvert"
#endif

#elif defined(__ANDROID__)
#define VIDEO_CONVERT "ercolorspace"

#elif defined(__linux__)
#define VIDEO_CONVERT "videoconvert"

#else
#define VIDEO_CONVERT "videoconvert"
#endif

#define OWR_REMOTE_MEDIA_SOURCE_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_REMOTE_MEDIA_SOURCE, OwrRemoteMediaSourcePrivate))

G_DEFINE_TYPE(OwrRemoteMediaSource, owr_remote_media_source, OWR_TYPE_MEDIA_SOURCE)

struct _OwrRemoteMediaSourcePrivate {
    guint stream_id;
};

static guint unique_pad_id = 0;

static GstElement *owr_remote_media_source_request_source(OwrMediaSource *media_source, GstCaps *caps);
static void owr_remote_media_source_release_source(OwrMediaSource *media_source, GstElement *source);

static void owr_remote_media_source_class_init(OwrRemoteMediaSourceClass *klass)
{
    OwrMediaSourceClass *media_source_class = OWR_MEDIA_SOURCE_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrRemoteMediaSourcePrivate));

    media_source_class->request_source = (void *(*)(OwrMediaSource *, void *))owr_remote_media_source_request_source;
    media_source_class->release_source = (void (*)(OwrMediaSource *, void *))owr_remote_media_source_release_source;
}

static void owr_remote_media_source_init(OwrRemoteMediaSource *source)
{
    source->priv = OWR_REMOTE_MEDIA_SOURCE_GET_PRIVATE(source);

    source->priv->stream_id = 0;
}

#define LINK_ELEMENTS(a, b) \
    if (!gst_element_link(a, b)) \
        GST_ERROR("Failed to link " #a " -> " #b);

#define CREATE_ELEMENT(elem, factory, name) \
    elem = gst_element_factory_make(factory, name); \
    if (!elem) \
        GST_ERROR("Could not create " name " from factory " factory); \
    g_assert(elem);

#define CREATE_ELEMENT_WITH_ID(elem, factory, name, id) \
{ \
    gchar *temp_str = g_strdup_printf(name"-%u", id); \
    elem = gst_element_factory_make(factory, temp_str); \
    if (!elem) \
        GST_ERROR("Could not create %s from factory " factory, temp_str); \
    g_assert(elem); \
    g_free(temp_str); \
}

/*
 * create_post_tee_bin
 *
 * The following chain is created after the tee for each output from the
 * source:
 *
 * +-----------+   +-------------------------------+   +----------+
 * | inter*src +---+ converters/queues/capsfilters +---+ ghostpad |
 * +-----------+   +-------------------------------+   +----------+
 *
 * TODO: Share this with local media sources
 *
 */
static GstElement *create_source_bin(OwrMediaSource *media_source, GstElement *source_pipeline,
    GstElement *tee, GstCaps *caps)
{
    OwrMediaType media_type;
    GstElement *source_bin, *source = NULL, *queue_pre, *queue_post;
    GstElement *capsfilter;
    GstElement *sink, *sink_queue, *sink_bin;
    GstPad *bin_pad = NULL, *srcpad, *sinkpad;
    gchar *bin_name;
    guint source_id;
    gchar *channel_name;

    source_id = g_atomic_int_add(&unique_pad_id, 1);

    bin_name = g_strdup_printf("source-bin-%u", source_id);
    source_bin = gst_bin_new(bin_name);
    g_free(bin_name);

    CREATE_ELEMENT_WITH_ID(queue_pre, "queue", "source-queue", source_id);
    CREATE_ELEMENT_WITH_ID(capsfilter, "capsfilter", "source-output-capsfilter", source_id);
    CREATE_ELEMENT_WITH_ID(queue_post, "queue", "source-output-queue", source_id);

    CREATE_ELEMENT_WITH_ID(sink_queue, "queue", "sink-queue", source_id);

    g_object_get(media_source, "media-type", &media_type, NULL);
    switch (media_type) {
    case OWR_MEDIA_TYPE_AUDIO:
        {
        GstElement *audioresample, *audioconvert;

        CREATE_ELEMENT_WITH_ID(source, "interaudiosrc", "source", source_id);
        CREATE_ELEMENT_WITH_ID(sink, "interaudiosink", "sink", source_id);

        g_object_set(capsfilter, "caps", caps, NULL);

        CREATE_ELEMENT_WITH_ID(audioresample, "audioresample", "source-audio-resample", source_id);
        CREATE_ELEMENT_WITH_ID(audioconvert, "audioconvert", "source-audio-convert", source_id);

        gst_bin_add_many(GST_BIN(source_bin),
            queue_pre, audioconvert, audioresample, capsfilter, queue_post, NULL);
        LINK_ELEMENTS(capsfilter, queue_post);
        LINK_ELEMENTS(audioresample, capsfilter);
        LINK_ELEMENTS(audioconvert, audioresample);
        LINK_ELEMENTS(queue_pre, audioconvert);

        break;
        }
    case OWR_MEDIA_TYPE_VIDEO:
        {
        GstElement *videorate = NULL, *videoscale, *videoconvert;
        GstCaps *source_caps;
        GstStructure *source_structure;
        gint fps_n = 0, fps_d = 1;

        CREATE_ELEMENT_WITH_ID(source, "intervideosrc", "source", source_id);
        CREATE_ELEMENT_WITH_ID(sink, "intervideosink", "sink", source_id);

        source_caps = gst_caps_copy(caps);
        source_structure = gst_caps_get_structure(source_caps, 0);
        if (gst_structure_get_fraction(source_structure, "framerate", &fps_n, &fps_d))
            gst_structure_remove_field(source_structure, "framerate");
        g_object_set(capsfilter, "caps", source_caps, NULL);
        gst_caps_unref(source_caps);

        CREATE_ELEMENT_WITH_ID(videoconvert, VIDEO_CONVERT, "source-video-convert", source_id);
        CREATE_ELEMENT_WITH_ID(videoscale, "videoscale", "source-video-scale", source_id);
        if (fps_n > 0) {
            CREATE_ELEMENT_WITH_ID(videorate, "videorate", "source-video-rate", source_id);
            g_object_set(videorate, "drop-only", TRUE, "max-rate", fps_n / fps_d, NULL);
            gst_bin_add(GST_BIN(source_bin), videorate);
        }

        gst_bin_add_many(GST_BIN(source_bin),
            queue_pre, videoscale, videoconvert, capsfilter, queue_post, NULL);
        LINK_ELEMENTS(capsfilter, queue_post);
        LINK_ELEMENTS(videoconvert, capsfilter);
        LINK_ELEMENTS(videoscale, videoconvert);
        if (videorate) {
            LINK_ELEMENTS(videorate, videoscale);
            LINK_ELEMENTS(queue_pre, videorate);
        } else {
            LINK_ELEMENTS(queue_pre, videoscale);
        }

        break;
        }
    case OWR_MEDIA_TYPE_UNKNOWN:
    default:
        g_assert_not_reached();
        goto done;
    }

    channel_name = g_strdup_printf("source-%u", source_id);
    g_object_set(source, "channel", channel_name, NULL);
    g_object_set(sink, "channel", channel_name, NULL);
    g_free(channel_name);

    /* Add and link the inter*sink to the actual source pipeline */
    bin_name = g_strdup_printf("source-sink-bin-%u", source_id);
    sink_bin = gst_bin_new(bin_name);
    gst_bin_add_many(GST_BIN(sink_bin), sink, sink_queue, NULL);
    gst_element_sync_state_with_parent(sink);
    gst_element_sync_state_with_parent(sink_queue);
    LINK_ELEMENTS(sink_queue, sink);
    sinkpad = gst_element_get_static_pad(sink_queue, "sink");
    bin_pad = gst_ghost_pad_new("sink", sinkpad);
    gst_object_unref(sinkpad);
    gst_pad_set_active(bin_pad, TRUE);
    gst_element_add_pad(sink_bin, bin_pad);
    bin_pad = NULL;
    gst_bin_add(GST_BIN(source_pipeline), sink_bin);
    gst_element_sync_state_with_parent(sink_bin);
    LINK_ELEMENTS(tee, sink_bin);

    /* Start up our new bin and link it all */
    srcpad = gst_element_get_static_pad(queue_post, "src");
    g_assert(srcpad);

    bin_pad = gst_ghost_pad_new("src", srcpad);
    gst_object_unref(srcpad);
    gst_pad_set_active(bin_pad, TRUE);
    gst_element_add_pad(source_bin, bin_pad);

    gst_bin_add(GST_BIN(source_bin), source);
    LINK_ELEMENTS(source, queue_pre);

done:

    return source_bin;
}

/* FIXME: Share this with local media sources */
static GstElement *owr_remote_media_source_request_source(OwrMediaSource *media_source, GstCaps *caps)
{
    OwrRemoteMediaSource *remote_source;
    OwrRemoteMediaSourcePrivate *priv;
    GstElement *tee, *transport_bin, *source_element;
    OwrMediaType media_type;
    OwrCodecType codec_type;

    g_assert(media_source);
    remote_source = OWR_REMOTE_MEDIA_SOURCE(media_source);
    priv = remote_source->priv;

    codec_type = _owr_media_source_get_codec(media_source);
    g_object_get(media_source, "media-type", &media_type, NULL);

    transport_bin = _owr_media_source_get_source_bin(media_source);

    if (!(tee = _owr_media_source_get_source_tee(media_source))) {
        GstElement *fakesink, *queue;
        GstPad *srcpad, *sinkpad;
        gchar *pad_name, *tee_name, *queue_name, *fakesink_name;

        if (media_type == OWR_MEDIA_TYPE_VIDEO) {
            pad_name = g_strdup_printf("video_src_%u_%u", codec_type, remote_source->priv->stream_id);
            tee_name = g_strdup_printf("video-src-tee-%u-%u", codec_type, remote_source->priv->stream_id);
            queue_name = g_strdup_printf("video-src-tee-fakesink-queue-%u-%u", codec_type, remote_source->priv->stream_id);
            fakesink_name = g_strdup_printf("video-src-tee-fakesink-%u-%u", codec_type, remote_source->priv->stream_id);
        } else if (media_type == OWR_MEDIA_TYPE_AUDIO) {
            pad_name = g_strdup_printf("audio_raw_src_%u", remote_source->priv->stream_id);
            tee_name = g_strdup_printf("audio-src-tee-%u-%u", codec_type, remote_source->priv->stream_id);
            queue_name = g_strdup_printf("audio-src-tee-fakesink-queue-%u-%u", codec_type, remote_source->priv->stream_id);
            fakesink_name = g_strdup_printf("audio-src-tee-fakesink-%u-%u", codec_type, remote_source->priv->stream_id);
        } else {
            g_assert_not_reached();
        }

        tee = gst_element_factory_make ("tee", tee_name);
        _owr_media_source_set_source_tee(media_source, tee);
        fakesink = gst_element_factory_make ("fakesink", fakesink_name);
        g_object_set(fakesink, "async", FALSE, NULL);
        queue = gst_element_factory_make ("queue", queue_name);
        g_free(tee_name);
        g_free(queue_name);
        g_free(fakesink_name);

        gst_bin_add_many(GST_BIN(transport_bin), tee, queue, fakesink, NULL);
        gst_element_sync_state_with_parent(tee);
        gst_element_sync_state_with_parent(queue);
        gst_element_sync_state_with_parent(fakesink);
        LINK_ELEMENTS(tee, queue);
        LINK_ELEMENTS(queue, fakesink);

        /* Link the transport bin to our tee */
        srcpad = gst_element_get_static_pad(transport_bin, pad_name);
        g_free(pad_name);
        sinkpad = gst_element_get_request_pad(tee, "src_%u");
        gst_pad_link(srcpad, sinkpad);
        gst_object_unref(sinkpad);
    }

    source_element = create_source_bin(media_source, transport_bin, tee, caps);

    return source_element;
}

static void owr_remote_media_source_release_source(OwrMediaSource *media_source, GstElement *source)
{
    OWR_UNUSED(media_source);
    OWR_UNUSED(source);
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
    _owr_media_source_set_source_bin(OWR_MEDIA_SOURCE(source), transport_bin);

    _owr_media_source_set_codec(OWR_MEDIA_SOURCE(source), codec_type);

    return source;
}
