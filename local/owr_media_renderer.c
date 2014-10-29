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
\*\ OwrMediaRenderer
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_media_renderer.h"

#include "owr_media_renderer_private.h"
#include "owr_media_source.h"
#include "owr_media_source_private.h"
#include "owr_private.h"
#include "owr_types.h"
#include "owr_utils.h"

#include <gst/gst.h>

#include <stdio.h>

#define DEFAULT_MEDIA_TYPE OWR_MEDIA_TYPE_UNKNOWN
#define DEFAULT_SOURCE NULL
#define DEFAULT_DISABLED FALSE

enum {
    PROP_0,
    PROP_MEDIA_TYPE,
    PROP_DISABLED,
    N_PROPERTIES
};

static guint unique_bin_id = 0;
static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

#define OWR_MEDIA_RENDERER_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_MEDIA_RENDERER, OwrMediaRendererPrivate))

G_DEFINE_TYPE(OwrMediaRenderer, owr_media_renderer, G_TYPE_OBJECT)

struct _OwrMediaRendererPrivate {
    GMutex media_renderer_lock;
    OwrMediaType media_type;
    OwrMediaSource *source;
    gboolean disabled;

    GstElement *pipeline;
    GstElement *src, *sink;
};

static void owr_media_renderer_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_media_renderer_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static void owr_media_renderer_finalize(GObject *object)
{
    OwrMediaRenderer *renderer = OWR_MEDIA_RENDERER(object);
    OwrMediaRendererPrivate *priv = renderer->priv;

    if (priv->source) {
        _owr_media_source_release_source(priv->source, priv->src);
        gst_element_set_state(priv->src, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(priv->pipeline), priv->src);
        g_object_unref(priv->source);
        priv->source = NULL;
    }

    if (priv->pipeline) {
        gst_element_set_state(priv->pipeline, GST_STATE_NULL);
        gst_object_unref(priv->pipeline);
        priv->pipeline = NULL;
        priv->sink = NULL;
    }

    g_mutex_clear(&priv->media_renderer_lock);

    G_OBJECT_CLASS(owr_media_renderer_parent_class)->finalize(object);
}

static void owr_media_renderer_class_init(OwrMediaRendererClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrMediaRendererPrivate));

    obj_properties[PROP_MEDIA_TYPE] = g_param_spec_enum("media-type", "media-type",
        "The type of media provided by this renderer",
        OWR_TYPE_MEDIA_TYPE, DEFAULT_MEDIA_TYPE,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_DISABLED] = g_param_spec_boolean("disabled", "Disabled",
        "Whether this renderer is disabled or not", DEFAULT_DISABLED,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = owr_media_renderer_set_property;
    gobject_class->get_property = owr_media_renderer_get_property;

    gobject_class->finalize = owr_media_renderer_finalize;
    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

/* FIXME: Copy from owr/orw.c without any error handling whatsoever */
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer user_data)
{
    gboolean ret, is_warning = FALSE;
    GstStateChangeReturn change_status;
    gchar *message_type, *debug;
    GError *error;
    GstPipeline *pipeline = user_data;

    g_return_val_if_fail(GST_IS_BUS(bus), TRUE);

    (void)user_data;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_LATENCY:
        ret = gst_bin_recalculate_latency(GST_BIN(pipeline));
        g_warn_if_fail(ret);
        break;

    case GST_MESSAGE_CLOCK_LOST:
        change_status = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
        g_warn_if_fail(change_status != GST_STATE_CHANGE_FAILURE);
        change_status = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
        g_warn_if_fail(change_status != GST_STATE_CHANGE_FAILURE);
        break;

    case GST_MESSAGE_EOS:
        g_print("End of stream\n");
        break;

    case GST_MESSAGE_WARNING:
        is_warning = TRUE;

    case GST_MESSAGE_ERROR:
        if (is_warning) {
            message_type = "Warning";
            gst_message_parse_warning(msg, &error, &debug);
        } else {
            message_type = "Error";
            gst_message_parse_error(msg, &error, &debug);
        }

        g_printerr("==== %s message start ====\n", message_type);
        g_printerr("%s in element %s.\n", message_type, GST_OBJECT_NAME(msg->src));
        g_printerr("%s: %s\n", message_type, error->message);
        g_printerr("Debugging info: %s\n", (debug) ? debug : "none");

        g_printerr("==== %s message stop ====\n", message_type);
        /*GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline.dot");*/

        g_error_free(error);
        g_free(debug);
        break;

    default:
        break;
    }

    return TRUE;
}

static void owr_media_renderer_init(OwrMediaRenderer *renderer)
{
    OwrMediaRendererPrivate *priv;
    GstBus *bus;
    gchar *bin_name;

    renderer->priv = priv = OWR_MEDIA_RENDERER_GET_PRIVATE(renderer);

    priv->media_type = DEFAULT_MEDIA_TYPE;
    priv->source = DEFAULT_SOURCE;
    priv->disabled = DEFAULT_DISABLED;

    bin_name = g_strdup_printf("media-renderer-%u", g_atomic_int_add(&unique_bin_id, 1));
    priv->pipeline = gst_pipeline_new(bin_name);
    g_free(bin_name);

#ifdef OWR_DEBUG
    g_signal_connect(priv->pipeline, "deep-notify", G_CALLBACK(gst_object_default_deep_notify), NULL);
#endif

    priv->sink = NULL;
    priv->src = NULL;

    bus = gst_pipeline_get_bus(GST_PIPELINE(priv->pipeline));
    g_main_context_push_thread_default(_owr_get_main_context());
    gst_bus_add_watch(bus, (GstBusFunc)bus_call, priv->pipeline);
    g_main_context_pop_thread_default(_owr_get_main_context());
    gst_object_unref(bus);

    g_mutex_init(&priv->media_renderer_lock);
}

static void owr_media_renderer_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrMediaRendererPrivate *priv;

    g_return_if_fail(object);
    priv = OWR_MEDIA_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_MEDIA_TYPE:
        priv->media_type = g_value_get_enum(value);
        break;

    case PROP_DISABLED:
        priv->disabled = g_value_get_boolean(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_media_renderer_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrMediaRendererPrivate *priv;

    g_return_if_fail(object);
    priv = OWR_MEDIA_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_MEDIA_TYPE:
        g_value_set_enum(value, priv->media_type);
        break;

    case PROP_DISABLED:
        g_value_set_boolean(value, priv->disabled);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}


static GstPad *_owr_media_renderer_get_pad(OwrMediaRenderer *renderer)
{
    GstElement *sink = NULL;
    OwrMediaRendererPrivate *priv;

    g_assert(renderer);
    priv = renderer->priv;

    g_mutex_lock(&priv->media_renderer_lock);

    if (priv->sink) {
        sink = priv->sink;
        goto done;
    }

    sink = OWR_MEDIA_RENDERER_GET_CLASS(renderer)->get_element(renderer);
    g_assert(sink);

    priv->sink = sink;
    gst_bin_add(GST_BIN(priv->pipeline), sink);
    gst_element_set_state(priv->pipeline, GST_STATE_PLAYING);

done:
    g_mutex_unlock(&priv->media_renderer_lock);
    return gst_element_get_static_pad(sink, "sink");
}

/**
 * owr_media_renderer_set_source:
 * @renderer:
 * @source: (transfer none) (allow-none):
 *
 * Returns:
 */
void owr_media_renderer_set_source(OwrMediaRenderer *renderer, OwrMediaSource *source)
{
    OwrMediaRendererPrivate *priv;
    gboolean ret = TRUE;
    GstElement *src;
    GstPad *srcpad, *sinkpad;
    GstCaps *caps;
    GstPadLinkReturn pad_link_return;

    g_return_if_fail(renderer);
    g_return_if_fail(!source || OWR_IS_MEDIA_SOURCE(source));

    priv = renderer->priv;

    g_mutex_lock(&priv->media_renderer_lock);

    if (source == priv->source) {
        g_mutex_unlock(&priv->media_renderer_lock);
        return;
    }

    if (priv->source) {
        _owr_media_source_release_source(priv->source, priv->src);
        gst_element_set_state(priv->src, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(priv->pipeline), priv->src);
        priv->src = NULL;
        g_object_unref(priv->source);
        priv->source = NULL;
    }

    g_mutex_unlock(&priv->media_renderer_lock);
    /* FIXME - too much locking/unlocking of the same lock across private API? */

    if (!source) {
        /* Shut down sink if we have no source */
        if (priv->sink) {
            gst_element_set_state(priv->pipeline, GST_STATE_NULL);
            gst_bin_remove(GST_BIN(priv->pipeline), priv->sink);
            priv->sink = NULL;
        }
        return;
    }

    sinkpad = _owr_media_renderer_get_pad(renderer);
    g_assert(sinkpad);
    caps = OWR_MEDIA_RENDERER_GET_CLASS(renderer)->get_caps(renderer);
    src = _owr_media_source_request_source(source, caps);
    gst_caps_unref(caps);
    g_assert(src);
    srcpad = gst_element_get_static_pad(src, "src");
    g_assert(srcpad);

    g_mutex_lock(&priv->media_renderer_lock);

    gst_bin_add(GST_BIN(priv->pipeline), src);
    pad_link_return = gst_pad_link(srcpad, sinkpad);
    gst_object_unref(sinkpad);
    gst_object_unref(srcpad);
    if (pad_link_return != GST_PAD_LINK_OK) {
        GST_ERROR("Failed to link source with renderer (%d)", pad_link_return);
        ret = FALSE;
        goto done;
    }
    gst_element_sync_state_with_parent(src);

    gst_element_post_message(priv->pipeline, gst_message_new_latency(GST_OBJECT(priv->pipeline)));

    priv->source = g_object_ref(source);

done:
    priv->src = src;
    g_mutex_unlock(&priv->media_renderer_lock);
}
