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
\*\ OwrMediaSource
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_media_source.h"

#include "owr_inter_sink.h"
#include "owr_inter_src.h"
#include "owr_media_source_private.h"
#include "owr_private.h"
#include "owr_types.h"
#include "owr_utils.h"

#include <gst/gst.h>
#define GST_USE_UNSTABLE_API
#include <gst/gl/gl.h>
#include <stdio.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

GST_DEBUG_CATEGORY_EXTERN(_owrmediasource_debug);
#define GST_CAT_DEFAULT _owrmediasource_debug

#if defined(__APPLE__) && !TARGET_IPHONE_SIMULATOR
#if TARGET_OS_IPHONE
#define VIDEO_CONVERT "ercolorspace"
#else
#define VIDEO_CONVERT "videoconvert"
#endif

#elif defined(__ANDROID__)
#define VIDEO_CONVERT "videoconvert"

#elif defined(__linux__)
#define VIDEO_CONVERT "videoconvert"

#else
#define VIDEO_CONVERT "videoconvert"
#endif

#define DEFAULT_NAME NULL
#define DEFAULT_MEDIA_TYPE OWR_MEDIA_TYPE_UNKNOWN
#define DEFAULT_TYPE OWR_SOURCE_TYPE_UNKNOWN

enum {
    PROP_0,
    PROP_NAME,
    PROP_MEDIA_TYPE,
    PROP_TYPE,
    N_PROPERTIES
};

static guint unique_bin_id = 0;
static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

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

#define OWR_MEDIA_SOURCE_GET_PRIVATE(obj) \
        (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_MEDIA_SOURCE, OwrMediaSourcePrivate))

G_DEFINE_TYPE(OwrMediaSource, owr_media_source, G_TYPE_OBJECT)

struct _OwrMediaSourcePrivate {
    gchar *name;
    OwrMediaType media_type;

    OwrSourceType type;
    OwrCodecType codec_type;

    /* The bin or pipeline that contains the data producers */
    GstElement *source_bin;
    /* Tee element from which we can tap the source for multiple consumers */
    GstElement *source_tee;
};

static void owr_media_source_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_media_source_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static GstElement *owr_media_source_request_source_default(OwrMediaSource *media_source, GstCaps *caps);
static void owr_media_source_release_source_default(OwrMediaSource *media_source, GstElement *source);

static void owr_media_source_finalize(GObject *object)
{
    OwrMediaSource *source = OWR_MEDIA_SOURCE(object);
    OwrMediaSourcePrivate *priv = source->priv;

    g_free(priv->name);
    priv->name = NULL;

    if (priv->source_bin) {
        GstElement *source_bin = priv->source_bin;
        priv->source_bin = NULL;
        gst_element_set_state(source_bin, GST_STATE_NULL);
        gst_object_unref(source_bin);
    }
    if (priv->source_tee) {
        gst_object_unref(priv->source_tee);
        priv->source_tee = NULL;
    }

    g_mutex_clear(&source->lock);

    G_OBJECT_CLASS(owr_media_source_parent_class)->finalize(object);
}

static void owr_media_source_class_init(OwrMediaSourceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrMediaSourcePrivate));

    obj_properties[PROP_NAME] = g_param_spec_string("name", "name",
        "Human readable and meaningful source name", DEFAULT_NAME,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_MEDIA_TYPE] = g_param_spec_enum("media-type", "media-type",
        "The type of media provided by this source",
        OWR_TYPE_MEDIA_TYPE, DEFAULT_MEDIA_TYPE,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_TYPE] = g_param_spec_enum("type", "type",
        "The type of source in terms of how it generates media",
        OWR_TYPE_SOURCE_TYPE, DEFAULT_TYPE,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = owr_media_source_set_property;
    gobject_class->get_property = owr_media_source_get_property;

    gobject_class->finalize = owr_media_source_finalize;

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);

    klass->request_source = (void *(*)(OwrMediaSource *, void *))owr_media_source_request_source_default;
    klass->release_source = (void (*)(OwrMediaSource *, void *))owr_media_source_release_source_default;
}

static void owr_media_source_init(OwrMediaSource *source)
{
    OwrMediaSourcePrivate *priv;
    source->priv = priv = OWR_MEDIA_SOURCE_GET_PRIVATE(source);

    priv->name = DEFAULT_NAME;
    priv->media_type = DEFAULT_MEDIA_TYPE;
    priv->type = DEFAULT_TYPE;

    priv->source_bin = NULL;
    priv->source_tee = NULL;

    g_mutex_init(&source->lock);
}

static void owr_media_source_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrMediaSourcePrivate *priv;

    g_return_if_fail(object);
    priv = OWR_MEDIA_SOURCE_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_NAME:
        g_free(priv->name);
        priv->name = g_value_dup_string(value);
        break;
    case PROP_MEDIA_TYPE:
        priv->media_type = g_value_get_enum(value);
        break;
    case PROP_TYPE:
        priv->type = g_value_get_enum(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_media_source_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrMediaSourcePrivate *priv;

    g_return_if_fail(object);
    priv = OWR_MEDIA_SOURCE_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_NAME:
        g_value_set_string(value, priv->name);
        break;
    case PROP_MEDIA_TYPE:
        g_value_set_enum(value, priv->media_type);
        break;
    case PROP_TYPE:
        g_value_set_enum(value, priv->type);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

/*
 * The following chain is created after the tee for each output from the
 * source:
 *
 * +-----------+   +-------------------------------+   +----------+
 * | inter*src +---+ converters/queues/capsfilters +---+ ghostpad |
 * +-----------+   +-------------------------------+   +----------+
 *
 */
static GstElement *owr_media_source_request_source_default(OwrMediaSource *media_source, GstCaps *caps)
{
    OwrMediaType media_type;
    GstElement *source_pipeline, *tee;
    GstElement *source_bin, *source = NULL, *queue_pre, *queue_post;
    GstElement *capsfilter;
    GstElement *sink, *sink_queue, *sink_bin;
    GstPad *bin_pad = NULL, *srcpad, *sinkpad;
    gchar *bin_name;
    guint source_id;
    gchar *sink_name, *source_name;

    g_return_val_if_fail(media_source->priv->source_bin, NULL);
    g_return_val_if_fail(media_source->priv->source_tee, NULL);

    source_pipeline = gst_object_ref(media_source->priv->source_bin);
    tee = gst_object_ref(media_source->priv->source_tee);

    source_id = g_atomic_int_add(&unique_bin_id, 1);

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
        GstElement *videorate = NULL, *videoscale = NULL, *videoconvert;
        GstStructure *s;
        GstCapsFeatures *features;

        s = gst_caps_get_structure(caps, 0);
        if (gst_structure_has_field(s, "framerate")) {
            gint fps_n = 0, fps_d = 0;

            gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d);
            g_assert(fps_d);

            CREATE_ELEMENT_WITH_ID(videorate, "videorate", "source-video-rate", source_id);
            g_object_set(videorate, "drop-only", TRUE, "max-rate", fps_n / fps_d, NULL);

            gst_structure_remove_field(s, "framerate");
            gst_bin_add(GST_BIN(source_bin), videorate);
        }
        g_object_set(capsfilter, "caps", caps, NULL);

        features = gst_caps_get_features(caps, 0);
        if (gst_caps_features_contains(features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
            GstElement *glupload;

            CREATE_ELEMENT_WITH_ID(glupload, "glupload", "source-glupload", source_id);
            CREATE_ELEMENT_WITH_ID(videoconvert, "glcolorconvert", "source-glcolorconvert", source_id);
            gst_bin_add_many(GST_BIN(source_bin),
                    queue_pre, glupload, videoconvert, capsfilter, queue_post, NULL);

            if (videorate) {
                LINK_ELEMENTS(queue_pre, videorate);
                LINK_ELEMENTS(videorate, glupload);
            } else {
                LINK_ELEMENTS(queue_pre, glupload);
            }
            LINK_ELEMENTS(glupload, videoconvert);
        } else {
            GstElement *gldownload;

            CREATE_ELEMENT_WITH_ID(gldownload, "gldownload", "source-gldownload", source_id);
            CREATE_ELEMENT_WITH_ID(videoscale,  "videoscale", "source-video-scale", source_id);
            CREATE_ELEMENT_WITH_ID(videoconvert, VIDEO_CONVERT, "source-video-convert", source_id);
            gst_bin_add_many(GST_BIN(source_bin),
                    queue_pre, gldownload, videoscale, videoconvert, capsfilter, queue_post, NULL);
            if (videorate) {
                LINK_ELEMENTS(queue_pre, videorate);
                LINK_ELEMENTS(videorate, gldownload);
            } else {
                LINK_ELEMENTS(queue_pre, gldownload);
            }
            LINK_ELEMENTS(gldownload, videoscale);
            LINK_ELEMENTS(videoscale, videoconvert);
        }
        LINK_ELEMENTS(videoconvert, capsfilter);
        LINK_ELEMENTS(capsfilter, queue_post);

        break;
        }
    case OWR_MEDIA_TYPE_UNKNOWN:
    default:
        g_assert_not_reached();
        goto done;
    }

    source_name = g_strdup_printf("source-%u", source_id);
    source = g_object_new(OWR_TYPE_INTER_SRC, "name", source_name, NULL);
    g_free(source_name);

    sink_name = g_strdup_printf("sink-%u", source_id);
    sink = g_object_new(OWR_TYPE_INTER_SINK, "name", sink_name, NULL);
    g_free(sink_name);

    g_weak_ref_set(&OWR_INTER_SRC(source)->sink_sinkpad, OWR_INTER_SINK(sink)->sinkpad);
    g_weak_ref_set(&OWR_INTER_SINK(sink)->src_srcpad, OWR_INTER_SRC(source)->internal_srcpad);

    /* Add and link the inter*sink to the actual source pipeline */
    bin_name = g_strdup_printf("source-sink-bin-%u", source_id);
    sink_bin = gst_bin_new(bin_name);
    g_free(bin_name);
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

    gst_object_unref(source_pipeline);
    gst_object_unref(tee);

    return source_bin;
}

static GstPadProbeReturn tee_idle_probe_cb(GstPad *teepad, GstPadProbeInfo *info, gpointer user_data)
{
    OwrMediaSource * media_source = user_data;
    GstElement *sink_bin;
    GstPad *sinkpad;
    GstObject *parent;
    GstElement *tee;

    gst_pad_remove_probe(teepad, GST_PAD_PROBE_INFO_ID(info));

    sinkpad = gst_pad_get_peer(teepad);
    g_assert(sinkpad);
    sink_bin = GST_ELEMENT(gst_object_get_parent(GST_OBJECT(sinkpad)));

    g_warn_if_fail(gst_pad_unlink(teepad, sinkpad));
    parent = gst_pad_get_parent(teepad);
    tee = GST_ELEMENT(parent);
    gst_element_release_request_pad(tee, teepad);
    gst_object_unref(parent);
    gst_object_unref(sinkpad);

    parent = gst_object_get_parent(GST_OBJECT(sink_bin));
    g_assert(parent);

    gst_bin_remove(GST_BIN(parent), sink_bin);
    gst_element_set_state(sink_bin, GST_STATE_NULL);
    gst_object_unref(sink_bin);
    gst_object_unref(parent);

    GST_DEBUG_OBJECT(media_source, "Source successfully unlinked");

    return GST_PAD_PROBE_OK;
}

static void owr_media_source_release_source_default(OwrMediaSource *media_source, GstElement *source)
{
    GstPad *srcpad, *sinkpad;
    gchar *bin_name, *source_name;
    guint source_id = -1;
    GstElement *sink_bin, *source_pipeline;

    g_return_if_fail(media_source->priv->source_bin);
    g_return_if_fail(media_source->priv->source_tee);

    source_name = gst_object_get_name(GST_OBJECT(source));

    if (!source_name || sscanf(source_name, "source-bin-%u", &source_id) != 1) {
        GST_WARNING_OBJECT(media_source,
                "Failed to get %s for clean up", source_name);
        g_free(source_name);
        return;
    }
    g_free(source_name);

    GST_DEBUG_OBJECT(media_source, "Unlinking source %u", source_id);

    /* Unlink parts from the source pipeline */
    source_pipeline = gst_object_ref(media_source->priv->source_bin);
    g_assert(source_pipeline);
    bin_name = g_strdup_printf("source-sink-bin-%u", source_id);
    sink_bin = gst_bin_get_by_name(GST_BIN(source_pipeline), bin_name);
    if (!sink_bin) {
        GST_WARNING_OBJECT(media_source,
                "Failed to get %s from source pipeline", bin_name);
        g_free(bin_name);
        gst_object_unref(source_pipeline);
        return;
    }
    g_free(bin_name);
    gst_object_unref(source_pipeline);

    sinkpad = gst_element_get_static_pad(sink_bin, "sink");
    /* The pad on the tee */
    srcpad = gst_pad_get_peer(sinkpad);
    gst_object_unref(sinkpad);

    gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_IDLE, tee_idle_probe_cb, media_source, NULL);
    gst_object_unref(srcpad);
    gst_object_unref(sink_bin);
}

/* call with the media_source lock */
/**
 * _owr_media_source_get_source_bin:
 * @media_source:
 *
 * Returns: (transfer full):
 *
 */
GstElement *_owr_media_source_get_source_bin(OwrMediaSource *media_source)
{
    g_return_val_if_fail(OWR_IS_MEDIA_SOURCE(media_source), NULL);

    return media_source->priv->source_bin ? gst_object_ref(media_source->priv->source_bin) : NULL;
}

/* call with the media_source lock */
/**
 * _owr_media_source_set_source_bin:
 * @media_source:
 * @bin: (transfer none):
 *
 */
void _owr_media_source_set_source_bin(OwrMediaSource *media_source, GstElement *bin)
{
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(media_source));
    g_return_if_fail(!bin || GST_IS_ELEMENT(bin));

    if (media_source->priv->source_bin) {
        gst_element_set_state(media_source->priv->source_bin, GST_STATE_NULL);
        gst_object_unref(media_source->priv->source_bin);
    }
    media_source->priv->source_bin = bin ? gst_object_ref(bin) : NULL;
}

/* call with the media_source lock */
/**
 * _owr_media_source_get_source_tee:
 * @media_source:
 *
 * Returns: (transfer full):
 *
 */
GstElement *_owr_media_source_get_source_tee(OwrMediaSource *media_source)
{
    g_return_val_if_fail(OWR_IS_MEDIA_SOURCE(media_source), NULL);

    return media_source->priv->source_tee ? gst_object_ref(media_source->priv->source_tee) : NULL;
}

/* call with the media_source lock */
/**
 * _owr_media_source_set_source_tee:
 * @media_source:
 * @tee: (transfer none):
 *
 */
void _owr_media_source_set_source_tee(OwrMediaSource *media_source, GstElement *tee)
{
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(media_source));
    g_return_if_fail(!tee || GST_IS_ELEMENT(tee));

    if (media_source->priv->source_tee) {
        gst_object_unref(media_source->priv->source_tee);
    }
    media_source->priv->source_tee = tee ? gst_object_ref(tee) : NULL;
}

/**
 * _owr_media_source_request_source:
 * @media_source:
 * @caps: (transfer none):
 *
 * Returns: (transfer full):
 *
 */
GstElement *_owr_media_source_request_source(OwrMediaSource *media_source, GstCaps *caps)
{
    GstElement *source;

    g_return_val_if_fail(OWR_IS_MEDIA_SOURCE(media_source), NULL);

    g_mutex_lock(&media_source->lock);
    source = OWR_MEDIA_SOURCE_GET_CLASS(media_source)->request_source(media_source, caps);
    g_mutex_unlock(&media_source->lock);

    return source;
}

/**
 * _owr_media_source_release_source:
 * @media_source:
 * @source: (transfer none):
 *
 */
void _owr_media_source_release_source(OwrMediaSource *media_source, GstElement *source)
{
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(media_source));
    g_return_if_fail(GST_IS_ELEMENT(source));

    g_mutex_lock(&media_source->lock);
    OWR_MEDIA_SOURCE_GET_CLASS(media_source)->release_source(media_source, source);
    g_mutex_unlock(&media_source->lock);
}

void _owr_media_source_set_type(OwrMediaSource *media_source, OwrSourceType type)
{
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(media_source));
    /* an enum is an int type so we can use atomic assignment */
    g_atomic_int_set(&media_source->priv->type, type);
}

OwrCodecType _owr_media_source_get_codec(OwrMediaSource *media_source)
{
    g_return_val_if_fail(OWR_IS_MEDIA_SOURCE(media_source), OWR_CODEC_TYPE_NONE);
    return media_source->priv->codec_type;
}

void _owr_media_source_set_codec(OwrMediaSource *media_source, OwrCodecType codec_type)
{
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(media_source));
    /* an enum is an int type so we can use atomic assignment */
    g_atomic_int_set(&media_source->priv->codec_type, codec_type);
}

gchar * owr_media_source_get_dot_data(OwrMediaSource *source)
{
    g_return_val_if_fail(OWR_IS_MEDIA_SOURCE(source), NULL);

    if (!source->priv->source_bin)
        return g_strdup("");

#if GST_CHECK_VERSION(1, 5, 0)
    return gst_debug_bin_to_dot_data(GST_BIN(source->priv->source_bin), GST_DEBUG_GRAPH_SHOW_ALL);
#else
    return g_strdup("");
#endif
}
