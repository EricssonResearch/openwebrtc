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
\*\ OwrMediaRenderer
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_media_renderer.h"

#include "owr_media_renderer_private.h"

#include "owr_media_source.h"
#include "owr_media_source_private.h"
#include "owr_message_origin_private.h"
#include "owr_private.h"
#include "owr_types.h"
#include "owr_utils.h"

#include <gst/gst.h>

#include <stdio.h>

GST_DEBUG_CATEGORY_EXTERN(_owrmediarenderer_debug);
#define GST_CAT_DEFAULT _owrmediarenderer_debug

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

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface);

G_DEFINE_TYPE_WITH_CODE(OwrMediaRenderer, owr_media_renderer, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(OWR_TYPE_MESSAGE_ORIGIN, owr_message_origin_interface_init))

struct _OwrMediaRendererPrivate {
    GMutex media_renderer_lock;
    OwrMediaType media_type;
    OwrMediaSource *source;
    gboolean disabled;

    GstElement *pipeline;
    GstElement *src, *sink;
    OwrMessageOriginBusSet *message_origin_bus_set;
};

static void owr_media_renderer_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_media_renderer_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static void owr_media_renderer_finalize(GObject *object)
{
    OwrMediaRenderer *renderer = OWR_MEDIA_RENDERER(object);
    OwrMediaRendererPrivate *priv = renderer->priv;

    owr_message_origin_bus_set_free(priv->message_origin_bus_set);
    priv->message_origin_bus_set = NULL;

    if (priv->pipeline) {
        gst_element_set_state(priv->pipeline, GST_STATE_NULL);
        gst_object_unref(priv->pipeline);
        priv->pipeline = NULL;
        priv->sink = NULL;
    }

    if (priv->source) {
        _owr_media_source_release_source(priv->source, priv->src);
        gst_element_set_state(priv->src, GST_STATE_NULL);
        g_object_unref(priv->source);
        priv->source = NULL;
        priv->src = NULL;
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

static gpointer owr_media_renderer_get_bus_set(OwrMessageOrigin *origin)
{
    return OWR_MEDIA_RENDERER(origin)->priv->message_origin_bus_set;
}

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface)
{
    interface->get_bus_set = owr_media_renderer_get_bus_set;
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
        /* fallthru */
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
    GSource *bus_source;
    gchar *bin_name;

    renderer->priv = priv = OWR_MEDIA_RENDERER_GET_PRIVATE(renderer);

    priv->media_type = DEFAULT_MEDIA_TYPE;
    priv->source = DEFAULT_SOURCE;
    priv->disabled = DEFAULT_DISABLED;

    priv->message_origin_bus_set = owr_message_origin_bus_set_new();

    bin_name = g_strdup_printf("media-renderer-%u", g_atomic_int_add(&unique_bin_id, 1));
    priv->pipeline = gst_pipeline_new(bin_name);
    gst_pipeline_use_clock(GST_PIPELINE(priv->pipeline), gst_system_clock_obtain());
    gst_element_set_base_time(priv->pipeline, _owr_get_base_time());
    gst_element_set_start_time(priv->pipeline, GST_CLOCK_TIME_NONE);
    g_free(bin_name);

#ifdef OWR_DEBUG
    g_signal_connect(priv->pipeline, "deep-notify", G_CALLBACK(_owr_deep_notify), NULL);
#endif

    priv->sink = NULL;
    priv->src = NULL;

    bus = gst_pipeline_get_bus(GST_PIPELINE(priv->pipeline));
    bus_source = gst_bus_create_watch(bus);
    g_source_set_callback(bus_source, (GSourceFunc) bus_call, priv->pipeline, NULL);
    g_source_attach(bus_source, _owr_get_main_context());
    g_source_unref(bus_source);

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

static void on_caps(GstElement *sink, GParamSpec *pspec, OwrMediaRenderer *media_renderer)
{
    GstCaps *caps;

    OWR_UNUSED(pspec);

    g_object_get(sink, "caps", &caps, NULL);

    if (GST_IS_CAPS(caps)) {
        GST_INFO_OBJECT(media_renderer, "%s renderer - configured with caps: %" GST_PTR_FORMAT,
            media_renderer->priv->media_type == OWR_MEDIA_TYPE_AUDIO ? "Audio" :
            media_renderer->priv->media_type == OWR_MEDIA_TYPE_VIDEO ? "Video" :
            "Unknown", caps);
    }
}

static void maybe_start_renderer(OwrMediaRenderer *renderer)
{
    OwrMediaRendererPrivate *priv;
    GstPad *sinkpad, *srcpad;
    GstElement *src;
    GstCaps *caps;
    GstPadLinkReturn pad_link_return;

    priv = renderer->priv;

    if (!priv->sink || !priv->source)
        return;

    sinkpad = gst_element_get_static_pad(priv->sink, "sink");
    g_assert(sinkpad);

    g_signal_connect(sinkpad, "notify::caps", G_CALLBACK(on_caps), renderer);

    caps = OWR_MEDIA_RENDERER_GET_CLASS(renderer)->get_caps(renderer);
    src = _owr_media_source_request_source(priv->source, caps);
    gst_caps_unref(caps);
    g_assert(src);
    srcpad = gst_element_get_static_pad(src, "src");
    g_assert(srcpad);
    priv->src = src;

    /* The sink is always inside the bin already */
    gst_bin_add_many(GST_BIN(priv->pipeline), priv->src, NULL);
    pad_link_return = gst_pad_link(srcpad, sinkpad);
    gst_object_unref(sinkpad);
    gst_object_unref(srcpad);
    if (pad_link_return != GST_PAD_LINK_OK) {
        GST_ERROR("Failed to link source with renderer (%d)", pad_link_return);
        return;
    }
    gst_element_set_state(priv->pipeline, GST_STATE_PLAYING);
    OWR_POST_EVENT(renderer, RENDERER_STARTED, NULL);
}

static gboolean set_source(GHashTable *args)
{
    OwrMediaRenderer *renderer;
    OwrMediaSource *source;
    OwrMediaRendererPrivate *priv;

    g_return_val_if_fail(args, G_SOURCE_REMOVE);

    renderer = g_hash_table_lookup(args, "renderer");
    source = g_hash_table_lookup(args, "source");

    g_return_val_if_fail(OWR_IS_MEDIA_RENDERER(renderer), G_SOURCE_REMOVE);
    g_return_val_if_fail(!source || OWR_IS_MEDIA_SOURCE(source), G_SOURCE_REMOVE);

    priv = renderer->priv;

    g_mutex_lock(&priv->media_renderer_lock);

    if (source == priv->source) {
        g_mutex_unlock(&priv->media_renderer_lock);
        goto end;
    }

    if (priv->source) {
        _owr_media_source_release_source(priv->source, priv->src);
        gst_element_set_state(priv->src, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(priv->pipeline), priv->src);
        priv->src = NULL;
        g_object_unref(priv->source);
        priv->source = NULL;
    }

    if (!source) {
        /* Shut down the pipeline if we have no source */
        gst_element_set_state(priv->pipeline, GST_STATE_NULL);
        OWR_POST_EVENT(renderer, RENDERER_STOPPED, NULL);
        g_mutex_unlock(&priv->media_renderer_lock);
        goto end;
    }

    priv->source = g_object_ref(source);

    maybe_start_renderer(renderer);

    g_mutex_unlock(&priv->media_renderer_lock);

end:
    g_object_unref(renderer);
    if (source)
        g_object_unref(source);
    g_hash_table_unref(args);
    return G_SOURCE_REMOVE;
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
    GHashTable *args;

    g_return_if_fail(OWR_IS_MEDIA_RENDERER(renderer));
    g_return_if_fail(!source || OWR_IS_MEDIA_SOURCE(source));

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(renderer));
    g_hash_table_insert(args, "renderer", renderer);
    g_hash_table_insert(args, "source", source);

    g_object_ref(renderer);
    if (source)
        g_object_ref(source);

    _owr_schedule_with_hash_table((GSourceFunc)set_source, args);
}

/**
 * _owr_media_renderer_set_sink:
 * @renderer:
 * @sink: (transfer full) (allow-none):
 *
 * Returns:
 */
void _owr_media_renderer_set_sink(OwrMediaRenderer *renderer, gpointer sink_ptr)
{
    OwrMediaRendererPrivate *priv;
    GstElement *sink = sink_ptr;

    g_return_if_fail(renderer);
    g_return_if_fail(!sink || GST_IS_ELEMENT(sink));

    priv = renderer->priv;

    g_mutex_lock(&priv->media_renderer_lock);

    if (priv->sink) {
        gst_element_set_state(priv->pipeline, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(priv->pipeline), priv->sink);
        priv->sink = NULL;
    }

    if (!sink) {
        if (priv->src) {
            _owr_media_source_release_source(priv->source, priv->src);
            gst_bin_remove(GST_BIN(priv->pipeline), priv->src);
            priv->src = NULL;
        }
        g_mutex_unlock(&priv->media_renderer_lock);
        return;
    }

    gst_bin_add(GST_BIN(priv->pipeline), sink);
    priv->sink = sink;

    maybe_start_renderer(renderer);

    g_mutex_unlock(&priv->media_renderer_lock);
}

gchar * owr_media_renderer_get_dot_data(OwrMediaRenderer *renderer)
{
    g_return_val_if_fail(OWR_IS_MEDIA_RENDERER(renderer), NULL);
    g_return_val_if_fail(renderer->priv->pipeline, NULL);

#if GST_CHECK_VERSION(1, 5, 0)
    return gst_debug_bin_to_dot_data(GST_BIN(renderer->priv->pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
#else
    return g_strdup("");
#endif
}

GstPipeline * _owr_media_renderer_get_pipeline(OwrMediaRenderer *renderer)
{
    g_return_val_if_fail(OWR_IS_MEDIA_RENDERER(renderer), NULL);
    g_return_val_if_fail(renderer->priv->pipeline, NULL);

    return gst_object_ref(GST_PIPELINE(renderer->priv->pipeline));
}
