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
\*\ OwrVideoRenderer
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_video_renderer.h"

#include "owr_private.h"
#include "owr_window_registry.h"
#include "owr_window_registry_private.h"

#include <gst/video/videooverlay.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#define VIDEO_SINK "glimagesink"

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR)
#define VIDEO_CONVERT "ercolorspace"
#else
#define VIDEO_CONVERT "videoconvert"
#endif

#define DEFAULT_WIDTH 0
#define DEFAULT_HEIGHT 0
#define DEFAULT_MAX_FRAMERATE 0.0
#define DEFAULT_TAG NULL

#define OWR_VIDEO_RENDERER_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_VIDEO_RENDERER, OwrVideoRendererPrivate))

G_DEFINE_TYPE(OwrVideoRenderer, owr_video_renderer, OWR_TYPE_MEDIA_RENDERER)

static guint unique_bin_id = 0;

enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_MAX_FRAMERATE,
    PROP_TAG,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static void owr_video_renderer_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_video_renderer_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static GstElement *owr_video_renderer_get_element(OwrMediaRenderer *renderer);
static GstCaps *owr_video_renderer_get_caps(OwrMediaRenderer *renderer);

struct _OwrVideoRendererPrivate {
    GMutex video_renderer_lock;
    guint width;
    guint height;
    gdouble max_framerate;
    GstElement *renderer_bin;
    gchar *tag;
};

static void owr_video_renderer_finalize(GObject *object)
{
    OwrVideoRenderer *renderer = OWR_VIDEO_RENDERER(object);
    OwrVideoRendererPrivate *priv = renderer->priv;

    g_mutex_clear(&priv->video_renderer_lock);

    g_free(priv->tag);

    G_OBJECT_CLASS(owr_video_renderer_parent_class)->finalize(object);
}

static void owr_video_renderer_class_init(OwrVideoRendererClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    OwrMediaRendererClass *media_renderer_class = OWR_MEDIA_RENDERER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrVideoRendererPrivate));

    obj_properties[PROP_WIDTH] = g_param_spec_uint("width", "width",
        "Video width in pixels", 0, G_MAXUINT, DEFAULT_WIDTH,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_HEIGHT] = g_param_spec_uint("height", "height",
        "Video height in pixels", 0, G_MAXUINT, DEFAULT_HEIGHT,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_MAX_FRAMERATE] = g_param_spec_double("max-framerate", "max-framerate",
        "Maximum video frames per second", 0.0, G_MAXDOUBLE,
        DEFAULT_MAX_FRAMERATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_TAG] = g_param_spec_string("tag", "tag",
        "Tag referencing the window widget into which to draw video (default: NULL, create a new window)",
        DEFAULT_TAG, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = owr_video_renderer_set_property;
    gobject_class->get_property = owr_video_renderer_get_property;

    gobject_class->finalize = owr_video_renderer_finalize;

    media_renderer_class->get_element = (void *(*)(OwrMediaRenderer *))owr_video_renderer_get_element;
    media_renderer_class->get_caps = (void *(*)(OwrMediaRenderer *))owr_video_renderer_get_caps;

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_video_renderer_init(OwrVideoRenderer *renderer)
{
    OwrVideoRendererPrivate *priv;
    renderer->priv = priv = OWR_VIDEO_RENDERER_GET_PRIVATE(renderer);

    priv->width = DEFAULT_WIDTH;
    priv->height = DEFAULT_HEIGHT;
    priv->max_framerate = DEFAULT_MAX_FRAMERATE;
    priv->tag = DEFAULT_TAG;

    priv->renderer_bin = NULL;

    g_mutex_init(&priv->video_renderer_lock);
}

static void owr_video_renderer_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrVideoRendererPrivate *priv;

    g_return_if_fail(object);
    priv = OWR_VIDEO_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint(value);
        break;
    case PROP_MAX_FRAMERATE:
        priv->max_framerate = g_value_get_double(value);
        break;
    case PROP_TAG:
        g_free(priv->tag);
        priv->tag = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_video_renderer_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrVideoRendererPrivate *priv;

    g_return_if_fail(object);
    priv = OWR_VIDEO_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
    /* FIXME - make changing properties cause reconfiguration */
    case PROP_WIDTH:
        g_value_set_uint(value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, priv->height);
        break;
    case PROP_MAX_FRAMERATE:
        g_value_set_double(value, priv->max_framerate);
        break;
    case PROP_TAG:
        g_value_set_string(value, priv->tag);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}


/**
 * owr_video_renderer_new: (constructor)
 * @tag:
 *
 * Returns: The new #OwrVideoRenderer
 */
OwrVideoRenderer *owr_video_renderer_new(const gchar *tag)
{
    return g_object_new(OWR_TYPE_VIDEO_RENDERER,
        "media-type", OWR_MEDIA_TYPE_VIDEO,
        "tag", tag,
        NULL);
}


#define LINK_ELEMENTS(a, b) \
    if (!gst_element_link(a, b)) \
        GST_ERROR("Failed to link " #a " -> " #b);

static void renderer_disabled(OwrMediaRenderer *renderer, GParamSpec *pspec, GstElement *balance)
{
    gboolean disabled = FALSE;

    g_return_if_fail(OWR_IS_MEDIA_RENDERER(renderer));
    g_return_if_fail(G_IS_PARAM_SPEC(pspec) || !pspec);
    g_return_if_fail(GST_IS_ELEMENT(balance));

    g_object_get(renderer, "disabled", &disabled, NULL);
    g_object_set(balance, "saturation", (gdouble)!disabled, "brightness", (gdouble)-disabled, NULL);
}

static GstElement *owr_video_renderer_get_element(OwrMediaRenderer *renderer)
{
    OwrVideoRenderer *video_renderer;
    OwrVideoRendererPrivate *priv;
    GstElement *balance, *queue, *sink;
    GstPad *ghostpad, *sinkpad;
    gchar *bin_name;

    g_assert(renderer);
    video_renderer = OWR_VIDEO_RENDERER(renderer);
    priv = video_renderer->priv;

    g_mutex_lock(&priv->video_renderer_lock);

    if (priv->renderer_bin)
        goto done;

    bin_name = g_strdup_printf("video-renderer-bin-%u", g_atomic_int_add(&unique_bin_id, 1));
    priv->renderer_bin = gst_bin_new(bin_name);
    g_free(bin_name);

    gst_bin_add(GST_BIN(_owr_get_pipeline()), priv->renderer_bin);
    gst_element_sync_state_with_parent(GST_ELEMENT(priv->renderer_bin));

    balance = gst_element_factory_make("videobalance", "video-renderer-balance");
    g_signal_connect_object(renderer, "notify::disabled", G_CALLBACK(renderer_disabled),
        balance, 0);
    renderer_disabled(renderer, NULL, balance);

    queue = gst_element_factory_make("queue", "video-renderer-queue");
    g_assert(queue);
    g_object_set(queue, "max-size-buffers", 3, "max-size-bytes", 0, "max-size-time", G_GUINT64_CONSTANT(0),
        "leaky", 2 /* downstream leaky */, NULL);

    sink = gst_element_factory_make(VIDEO_SINK, "video-renderer-sink");
    g_assert(sink);
    if (priv->tag) {
        GstElement *sink_element = GST_IS_BIN(sink) ?
            gst_bin_get_by_interface(GST_BIN(sink), GST_TYPE_VIDEO_OVERLAY) : sink;
        if (GST_IS_ELEMENT(sink_element) && GST_IS_VIDEO_OVERLAY(sink)) {
            guintptr handle = _owr_window_registry_lookup(owr_window_registry_get(), priv->tag);
            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink), handle);
        }
        if (GST_IS_BIN(sink))
            g_object_unref(sink_element);
    }

    /* async false is needed when using live sources to not require prerolling
     * as prerolling is not possible from live sources in GStreamer */
    g_object_set(sink, "async", FALSE, NULL);

    gst_bin_add_many(GST_BIN(priv->renderer_bin), balance, queue, sink, NULL);

    LINK_ELEMENTS(queue, sink);
    LINK_ELEMENTS(balance, queue);

    sinkpad = gst_element_get_static_pad(balance, "sink");
    g_assert(sinkpad);
    ghostpad = gst_ghost_pad_new("sink", sinkpad);
    gst_pad_set_active(ghostpad, TRUE);
    gst_element_add_pad(priv->renderer_bin, ghostpad);
    gst_object_unref(sinkpad);

    gst_element_sync_state_with_parent(sink);
    gst_element_sync_state_with_parent(queue);
    gst_element_sync_state_with_parent(balance);
done:
    g_mutex_unlock(&priv->video_renderer_lock);
    return priv->renderer_bin;
}

static GstCaps *owr_video_renderer_get_caps(OwrMediaRenderer *renderer)
{
    GstCaps *caps;
    guint width = 0, height = 0;
    gdouble max_framerate = 0.0;

    g_object_get(OWR_VIDEO_RENDERER(renderer),
        "width", &width,
        "height", &height,
        "max-framerate", &max_framerate,
        NULL);

    caps = gst_caps_new_empty_simple("video/x-raw");
    if (width > 0)
        gst_caps_set_simple(caps, "width", G_TYPE_INT, width, NULL);
    if (height > 0)
        gst_caps_set_simple(caps, "height", G_TYPE_INT, height, NULL);
    if (max_framerate > 0.0) {
        gint fps_n = 0, fps_d = 1;
        gst_util_double_to_fraction(max_framerate, &fps_n, &fps_d);
        GST_DEBUG_OBJECT(renderer, "Setting the framerate to %d/%d", fps_n, fps_d);
        gst_caps_set_simple(caps, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
    }

    return caps;
}
