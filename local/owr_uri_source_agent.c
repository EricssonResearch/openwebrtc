/*
 * Copyright (c) 2014-2015, Ericsson AB. All rights reserved.
 * Copyright (c) 2014, Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
 *     Author: Arun Raghavan <arun@centricular.com>
 * Copyright (c) 2015, Collabora Ltd.
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
\*\ OwrURISourceAgent
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_uri_source_agent.h"

#include "owr_private.h"
#include "owr_uri_source.h"
#include "owr_uri_source_private.h"
#include "owr_utils.h"

#include <gst/audio/audio.h>
#include <gst/video/video.h>
#include <gst/gst.h>
#include <stdio.h>

#define DEFAULT_URI NULL

enum {
    PROP_0,
    PROP_URI,
    N_PROPERTIES
};

enum {
    SIGNAL_ON_NEW_SOURCE,

    LAST_SIGNAL
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };
static guint uri_source_agent_signals[LAST_SIGNAL] = { 0 };
static guint next_uri_source_agent_id = 1;

#define OWR_URI_SOURCE_AGENT_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_URI_SOURCE_AGENT, OwrURISourceAgentPrivate))

G_DEFINE_TYPE(OwrURISourceAgent, owr_uri_source_agent, G_TYPE_OBJECT)

struct _OwrURISourceAgentPrivate {
    gchar *uri;
    guint agent_id;
    GstElement *pipeline, *uridecodebin;
    GstClockTime offset;
};

static void owr_uri_source_agent_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_uri_source_agent_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);


static void on_uridecodebin_pad_added(GstElement *uri_bin, GstPad *new_pad, OwrURISourceAgent *uri_source_agent);

static void owr_uri_source_agent_finalize(GObject *object)
{
    OwrURISourceAgent *uri_source_agent = NULL;
    OwrURISourceAgentPrivate *priv = NULL;

    g_return_if_fail(_owr_is_initialized());

    uri_source_agent = OWR_URI_SOURCE_AGENT(object);
    priv = uri_source_agent->priv;

    gst_element_set_state(priv->pipeline, GST_STATE_NULL);
    gst_object_unref(priv->pipeline);

    g_free(priv->uri);

    G_OBJECT_CLASS(owr_uri_source_agent_parent_class)->finalize(object);
}

static void owr_uri_source_agent_class_init(OwrURISourceAgentClass *klass)
{
    GObjectClass *gobject_class;
    g_type_class_add_private(klass, sizeof(OwrURISourceAgentPrivate));

    gobject_class = G_OBJECT_CLASS(klass);

    obj_properties[PROP_URI] = g_param_spec_string("uri",
        "URI", "A URI pointing to media support by GStreamer's uridecodebin",
        DEFAULT_URI, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
    * OwrURISourceAgent::on-new-source:
    * @uri_source_agent: the object which received the signal
    * @source: (transfer none): the new source
    *
    * Notify of a new source for a #OwrURISourceAgent.
    */
    uri_source_agent_signals[SIGNAL_ON_NEW_SOURCE] = g_signal_new("on-new-source",
        G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_CLEANUP,
        G_STRUCT_OFFSET(OwrURISourceAgentClass, on_new_source), NULL, NULL,
        NULL, G_TYPE_NONE, 1, OWR_TYPE_MEDIA_SOURCE);


    gobject_class->set_property = owr_uri_source_agent_set_property;
    gobject_class->get_property = owr_uri_source_agent_get_property;
    gobject_class->finalize = owr_uri_source_agent_finalize;

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

/* FIXME: Copy from owr/owr.c without any error handling whatsoever */
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
        /* FIXME - implement looping */
        g_print("EOS\n");
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

static void owr_uri_source_agent_init(OwrURISourceAgent *uri_source_agent)
{
    OwrURISourceAgentPrivate *priv;
    GstBus *bus;
    GSource *bus_source;
    gchar *pipeline_name, *uridecodebin_name;

    uri_source_agent->priv = priv = OWR_URI_SOURCE_AGENT_GET_PRIVATE(uri_source_agent);

    priv->uri = DEFAULT_URI;
    priv->agent_id = next_uri_source_agent_id++;
    priv->offset = GST_CLOCK_TIME_NONE;

    g_return_if_fail(_owr_is_initialized());

    pipeline_name = g_strdup_printf("uri-source-agent-%u", priv->agent_id);
    priv->pipeline = gst_pipeline_new(pipeline_name);
    gst_pipeline_use_clock(GST_PIPELINE(priv->pipeline), gst_system_clock_obtain());
    g_free(pipeline_name);

#ifdef OWR_DEBUG
    g_signal_connect(priv->pipeline, "deep-notify", G_CALLBACK(_owr_deep_notify), NULL);
#endif

    bus = gst_pipeline_get_bus(GST_PIPELINE(priv->pipeline));
    bus_source = gst_bus_create_watch(bus);
    g_source_set_callback(bus_source, (GSourceFunc) bus_call, priv->pipeline, NULL);
    g_source_attach(bus_source, _owr_get_main_context());
    g_source_unref(bus_source);

    uridecodebin_name = g_strdup_printf("uridecodebin-%u", priv->agent_id);
    priv->uridecodebin = gst_element_factory_make("uridecodebin", uridecodebin_name);
    g_free(uridecodebin_name);
    g_signal_connect(priv->uridecodebin, "pad-added", G_CALLBACK(on_uridecodebin_pad_added), uri_source_agent);

    gst_bin_add(GST_BIN(priv->pipeline), priv->uridecodebin);
}

static void owr_uri_source_agent_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrURISourceAgent *uri_source_agent;
    OwrURISourceAgentPrivate *priv;

    g_return_if_fail(object);
    uri_source_agent = OWR_URI_SOURCE_AGENT(object);
    priv = uri_source_agent->priv;

    switch (property_id) {
    case PROP_URI:
        g_free(priv->uri);
        priv->uri = g_value_dup_string(value);
        g_object_set(priv->uridecodebin, "uri", priv->uri, NULL);
        gst_element_set_state(priv->pipeline, GST_STATE_PAUSED);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_uri_source_agent_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrURISourceAgent *uri_source_agent;
    OwrURISourceAgentPrivate *priv;

    g_return_if_fail(object);
    uri_source_agent = OWR_URI_SOURCE_AGENT(object);
    priv = uri_source_agent->priv;

    switch (property_id) {
    case PROP_URI:
        g_value_set_string(value, priv->uri);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

OwrURISourceAgent * owr_uri_source_agent_new(const gchar *uri)
{
    gchar *scheme;

    scheme = g_uri_parse_scheme(uri);
    if (!scheme) {
        GST_WARNING("Invalid URI: %s", uri ? uri : "");
        return NULL;
    }

    g_free(scheme);

    return g_object_new(OWR_TYPE_URI_SOURCE_AGENT, "uri", uri,
        NULL);
}

static gboolean emit_on_new_source(GHashTable *args)
{
    OwrURISourceAgent *uri_source_agent;
    OwrMediaSource *source;

    uri_source_agent = g_hash_table_lookup(args, "uri-source-agent");
    source = g_hash_table_lookup(args, "source");

    g_signal_emit_by_name(uri_source_agent, "on-new-source", source);

    g_hash_table_unref(args);
    return FALSE;
}

static void signal_new_source(OwrMediaType type, OwrURISourceAgent *uri_source_agent,
    guint stream_id, OwrCodecType codec_type)
{
    OwrMediaSource *source;
    GHashTable *args;

    g_return_if_fail(OWR_IS_URI_SOURCE_AGENT(uri_source_agent));

    source = _owr_uri_source_new(type, stream_id, codec_type,
        uri_source_agent->priv->uridecodebin);

    g_return_if_fail(source);

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "uri-source-agent", uri_source_agent);
    g_hash_table_insert(args, "source", source);

    _owr_schedule_with_hash_table((GSourceFunc)emit_on_new_source, args);
}

static void on_uridecodebin_pad_added(GstElement *uridecodebin, GstPad *new_pad, OwrURISourceAgent *uri_source_agent)
{
    gchar *new_pad_name;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
    guint stream_id = 0;
    GstCaps *caps, *audio_raw_caps, *video_raw_caps;

    g_return_if_fail(GST_IS_BIN(uridecodebin));
    g_return_if_fail(GST_IS_PAD(new_pad));
    g_return_if_fail(OWR_IS_URI_SOURCE_AGENT(uri_source_agent));

    new_pad_name = gst_pad_get_name(new_pad);

    sscanf(new_pad_name, "src_%u", &stream_id);
    caps = gst_pad_get_current_caps(new_pad);

    audio_raw_caps = gst_caps_from_string("audio/x-raw");
    video_raw_caps = gst_caps_from_string("video/x-raw");

    if (gst_caps_can_intersect(caps, audio_raw_caps))
        media_type = OWR_MEDIA_TYPE_AUDIO;
    else if (gst_caps_can_intersect(caps, video_raw_caps))
        media_type = OWR_MEDIA_TYPE_VIDEO;

    gst_caps_unref(audio_raw_caps);
    gst_caps_unref(video_raw_caps);

    if (media_type != OWR_MEDIA_TYPE_UNKNOWN) {
        if (uri_source_agent->priv->offset == GST_CLOCK_TIME_NONE) {
            /* Offset our buffers by the running time so that the first buffer
             * we push out corresponds to whatever the current running time is.
             * This is useful to make sure no buffers are lost in general, but
             * specifically required if the source is added some time after the
             * pipeline is running. */
            GstClock *clock;

            clock = gst_pipeline_get_clock(GST_PIPELINE(uri_source_agent->priv->pipeline));
            uri_source_agent->priv->offset = gst_clock_get_time(clock) - _owr_get_base_time();
            gst_object_unref(clock);
        }

        gst_pad_set_offset(new_pad, uri_source_agent->priv->offset);

        signal_new_source(media_type, uri_source_agent, stream_id, OWR_CODEC_TYPE_NONE);
    }

    g_free(new_pad_name);
    gst_caps_unref(caps);
}

gchar * owr_uri_source_agent_get_dot_data(OwrURISourceAgent *uri_source_agent)
{
    g_return_val_if_fail(OWR_IS_URI_SOURCE_AGENT(uri_source_agent), NULL);
    g_return_val_if_fail(uri_source_agent->priv->pipeline, NULL);

#if GST_CHECK_VERSION(1, 5, 0)
    return gst_debug_bin_to_dot_data(GST_BIN(uri_source_agent->priv->pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
#else
    return g_strdup("");
#endif
}

gboolean owr_uri_source_agent_play(OwrURISourceAgent *uri_source_agent)
{
    return gst_element_set_state(uri_source_agent->priv->pipeline,
        GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE;
}


gboolean owr_uri_source_agent_pause(OwrURISourceAgent *uri_source_agent)
{
    return gst_element_set_state(uri_source_agent->priv->pipeline,
        GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE;
}
