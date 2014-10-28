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
\*\ OwrTransportAgent
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_transport_agent.h"

#include "owr_audio_payload.h"
#include "owr_candidate_private.h"
#include "owr_media_session.h"
#include "owr_media_session_private.h"
#include "owr_media_source.h"
#include "owr_media_source_private.h"
#include "owr_payload_private.h"
#include "owr_private.h"
#include "owr_remote_media_source.h"
#include "owr_remote_media_source_private.h"
#include "owr_session.h"
#include "owr_session_private.h"
#include "owr_types.h"
#include "owr_utils.h"
#include "owr_video_payload.h"

#include <agent.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/rtp/gstrtpbuffer.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_ICE_CONTROLLING_MODE TRUE

enum {
    PROP_0,
    PROP_ICE_CONTROLLING_MODE,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };
static guint next_transport_agent_id = 1;

#define FILL_DECODED_VALVE_NAME(array, stream_id) \
    g_snprintf(array, OWR_OBJECT_NAME_LENGTH_MAX, "decoded_valve_%u", stream_id);


#define OWR_TRANSPORT_AGENT_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_TRANSPORT_AGENT, OwrTransportAgentPrivate))

G_DEFINE_TYPE(OwrTransportAgent, owr_transport_agent, G_TYPE_OBJECT)

struct _OwrTransportAgentPrivate {
    NiceAgent *nice_agent;
    gboolean ice_controlling_mode;

    GHashTable *sessions;
    guint next_stream_id;
    guint agent_id;
    gchar *transport_bin_name;
    GstElement *pipeline, *transport_bin;
    GstElement *rtpbin;

    GHashTable *streams;


    guint local_min_port;
    guint local_max_port;

    GList *helper_server_infos;
};

typedef struct {
    OwrTransportAgent *transport_agent;
    guint session_id;
} AgentAndSessionIdPair;

#define GEN_HASH_KEY(seq, ssrc) (seq ^ ssrc)

static void owr_transport_agent_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_transport_agent_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);


static void add_helper_server_info(GResolver *resolver, GAsyncResult *result, GHashTable *info);
static void update_helper_servers(OwrTransportAgent *transport_agent, guint stream_id);
static gboolean add_media_session(GHashTable *args);
static guint get_stream_id(OwrTransportAgent *transport_agent, OwrSession *session);
static OwrMediaSession * get_media_session(OwrTransportAgent *transport_agent, guint stream_id);
static void prepare_transport_bin_send_elements(OwrTransportAgent *transport_agent, guint stream_id, gboolean rtcp_mux);
static void prepare_transport_bin_receive_elements(OwrTransportAgent *transport_agent, guint stream_id, gboolean rtcp_mux);
static void set_send_ssrc_and_cname(OwrTransportAgent *agent, OwrMediaSession *media_session);
static void on_new_candidate(NiceAgent *nice_agent, guint stream_id, guint component_id, gchar *foundation, OwrTransportAgent *transport_agent);
static void on_candidate_gathering_done(NiceAgent *nice_agent, guint stream_id, OwrTransportAgent *transport_agent);
static void handle_new_send_payload(OwrTransportAgent *transport_agent, OwrMediaSession *media_session);
static void on_new_remote_candidate(OwrTransportAgent *transport_agent, gboolean forced, OwrSession *session);

static void on_transport_bin_pad_added(GstElement *transport_bin, GstPad *new_pad, OwrTransportAgent *transport_agent);
static void on_rtpbin_pad_added(GstElement *rtpbin, GstPad *new_pad, OwrTransportAgent *agent);
static void setup_video_receive_elements(GstPad *new_pad, guint32 session_id, OwrPayload *payload, OwrTransportAgent *transport_agent);
static void setup_audio_receive_elements(GstPad *new_pad, guint32 session_id, OwrPayload *payload, OwrTransportAgent *transport_agent);
static GstCaps * on_rtpbin_request_pt_map(GstElement *rtpbin, guint session_id, guint pt, OwrTransportAgent *agent);

static gboolean on_sending_rtcp(GObject *session, GstBuffer *buffer, gboolean early, OwrTransportAgent *agent);
static void on_feedback_rtcp(GObject *session, guint type, guint fbtype, guint sender_ssrc, guint media_ssrc, GstBuffer *fci, OwrTransportAgent *transport_agent);
static void on_ssrc_active(GstElement *rtpbin, guint session_id, guint ssrc, OwrTransportAgent *transport_agent);
static void prepare_rtcp_stats(OwrMediaSession *media_session, GObject *rtp_source);

static void owr_transport_agent_finalize(GObject *object)
{
    OwrTransportAgent *transport_agent = NULL;
    OwrTransportAgentPrivate *priv = NULL;
    OwrMediaSession *media_session = NULL;
    GList *sessions_list = NULL, *item = NULL;

    g_return_if_fail(_owr_is_initialized());

    transport_agent = OWR_TRANSPORT_AGENT(object);
    priv = transport_agent->priv;

    sessions_list = g_hash_table_get_values(priv->sessions);
    for (item = sessions_list; item; item = item->next) {
        media_session = item->data;
        _owr_media_session_clear_closures(media_session);
    }
    g_list_free(sessions_list);

    g_hash_table_destroy(priv->sessions);

    g_object_unref(priv->nice_agent);

    gst_element_set_state(priv->pipeline, GST_STATE_NULL);
    gst_object_unref(priv->pipeline);
    g_free(priv->transport_bin_name);

    gst_object_unref(priv->rtpbin);

    for (item = priv->helper_server_infos; item; item = item->next) {
        g_free(g_hash_table_lookup(item->data, "address"));
        g_free(g_hash_table_lookup(item->data, "username"));
        g_free(g_hash_table_lookup(item->data, "password"));
        g_hash_table_destroy(item->data);
    }
    g_list_free(priv->helper_server_infos);

    G_OBJECT_CLASS(owr_transport_agent_parent_class)->finalize(object);
}

static void owr_transport_agent_class_init(OwrTransportAgentClass *klass)
{
    GObjectClass *gobject_class;
    g_type_class_add_private(klass, sizeof(OwrTransportAgentPrivate));

    gobject_class = G_OBJECT_CLASS(klass);

    obj_properties[PROP_ICE_CONTROLLING_MODE] = g_param_spec_boolean("ice-controlling-mode",
        "Ice controlling mode", "Whether the ice agent is in controlling mode",
        DEFAULT_ICE_CONTROLLING_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = owr_transport_agent_set_property;
    gobject_class->get_property = owr_transport_agent_get_property;
    gobject_class->finalize = owr_transport_agent_finalize;

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

static void owr_transport_agent_init(OwrTransportAgent *transport_agent)
{
    OwrTransportAgentPrivate *priv;
    GstBus *bus;
    GstStateChangeReturn state_change_status;
    gchar *pipeline_name;

    transport_agent->priv = priv = OWR_TRANSPORT_AGENT_GET_PRIVATE(transport_agent);

    priv->ice_controlling_mode = DEFAULT_ICE_CONTROLLING_MODE;
    priv->agent_id = next_transport_agent_id++;
    priv->nice_agent = NULL;

    priv->sessions = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
    priv->next_stream_id = 0;

    g_return_if_fail(_owr_is_initialized());

    priv->nice_agent = nice_agent_new(_owr_get_main_context(), NICE_COMPATIBILITY_RFC5245);
    g_object_bind_property(transport_agent, "ice-controlling-mode", priv->nice_agent,
        "controlling-mode", G_BINDING_SYNC_CREATE);
    g_signal_connect(G_OBJECT(priv->nice_agent), "new-candidate",
        G_CALLBACK(on_new_candidate), transport_agent);
    g_signal_connect(G_OBJECT(priv->nice_agent), "candidate-gathering-done",
        G_CALLBACK(on_candidate_gathering_done), transport_agent);

    pipeline_name = g_strdup_printf("transport-agent-%u", priv->agent_id);
    priv->pipeline = gst_pipeline_new(pipeline_name);
    g_free(pipeline_name);

    bus = gst_pipeline_get_bus(GST_PIPELINE(priv->pipeline));
    g_main_context_push_thread_default(_owr_get_main_context());
    gst_bus_add_watch(bus, (GstBusFunc)bus_call, priv->pipeline);
    g_main_context_pop_thread_default(_owr_get_main_context());
    gst_object_unref(bus);

    priv->transport_bin_name = g_strdup_printf("transport_bin_%u", priv->agent_id);
    priv->transport_bin = gst_bin_new(priv->transport_bin_name);
    priv->rtpbin = gst_element_factory_make("rtpbin", "rtpbin");
    gst_object_ref(priv->rtpbin);
    g_object_set(priv->rtpbin, "do-lost", TRUE, NULL);
    g_signal_connect(priv->rtpbin, "pad-added", G_CALLBACK(on_rtpbin_pad_added), transport_agent);
    g_signal_connect(priv->rtpbin, "request-pt-map", G_CALLBACK(on_rtpbin_request_pt_map), transport_agent);
    g_signal_connect(priv->rtpbin, "on-ssrc-active", G_CALLBACK(on_ssrc_active), transport_agent);

    g_signal_connect(priv->transport_bin, "pad-added", G_CALLBACK(on_transport_bin_pad_added), transport_agent);

    gst_bin_add(GST_BIN(priv->transport_bin), priv->rtpbin);
    gst_bin_add(GST_BIN(priv->pipeline), priv->transport_bin);
    state_change_status = gst_element_set_state(priv->pipeline, GST_STATE_PLAYING);
    g_warn_if_fail(state_change_status == GST_STATE_CHANGE_SUCCESS);

    priv->local_min_port = 0;
    priv->local_max_port = 0;

    priv->helper_server_infos = NULL;
}


static void owr_transport_agent_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrTransportAgent *transport_agent;
    OwrTransportAgentPrivate *priv;

    g_return_if_fail(object);
    transport_agent = OWR_TRANSPORT_AGENT(object);
    priv = transport_agent->priv;

    switch (property_id) {
    case PROP_ICE_CONTROLLING_MODE:
        priv->ice_controlling_mode = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}


static void owr_transport_agent_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrTransportAgent *transport_agent;
    OwrTransportAgentPrivate *priv;

    g_return_if_fail(object);
    transport_agent = OWR_TRANSPORT_AGENT(object);
    priv = transport_agent->priv;

    switch (property_id) {
    case PROP_ICE_CONTROLLING_MODE:
        g_value_set_boolean(value, priv->ice_controlling_mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

OwrTransportAgent * owr_transport_agent_new(gboolean ice_controlling_mode)
{
    return g_object_new(OWR_TYPE_TRANSPORT_AGENT, "ice-controlling_mode", ice_controlling_mode,
        NULL);
}

/**
 * owr_transport_agent_add_helper_server:
 * @transport_agent: The #OwrTransportAgent that should use a helper server
 * @type: The type of helper server to add
 * @address: The IP address or hostname of the helper server
 * @port: The port on the helper server
 * @username: (allow-none): The helper server username (when applicable)
 * @password: (allow-none): The helper server password (when applicable)
 *
 */
void owr_transport_agent_add_helper_server(OwrTransportAgent *transport_agent,
    OwrHelperServerType type, const gchar *address, guint port,
    const gchar *username, const gchar *password)
{
    GHashTable *helper_server_info;
    GResolver *resolver;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    helper_server_info = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(helper_server_info, "transport_agent", transport_agent);
    g_hash_table_insert(helper_server_info, "type", GUINT_TO_POINTER(type));
    g_hash_table_insert(helper_server_info, "port", GUINT_TO_POINTER(port));
    if (username)
        g_hash_table_insert(helper_server_info, "username", g_strdup(username));
    if (password)
        g_hash_table_insert(helper_server_info, "password", g_strdup(password));

    g_object_ref(transport_agent);
    resolver = g_resolver_get_default();
    g_resolver_lookup_by_name_async(resolver, address, NULL,
        (GAsyncReadyCallback)add_helper_server_info, helper_server_info);
}

void owr_transport_agent_add_local_address(OwrTransportAgent *transport_agent, const gchar *local_address)
{
    NiceAddress addr;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_return_if_fail(local_address);

    if (!nice_address_set_from_string(&addr, local_address))
        g_warning("Supplied local address is not valid.");
    else if (!nice_agent_add_local_address(transport_agent->priv->nice_agent, &addr))
        g_warning("Failed to add local address.");
}

void owr_transport_agent_set_local_port_range(OwrTransportAgent *transport_agent, guint min_port, guint max_port)
{
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_return_if_fail(max_port < 65536);
    g_return_if_fail(min_port <= max_port);

    transport_agent->priv->local_min_port = min_port;
    transport_agent->priv->local_max_port = max_port;
}

void owr_transport_agent_add_session(OwrTransportAgent *agent, OwrSession *session)
{
    GHashTable *args;

    g_return_if_fail(agent);
    g_return_if_fail(OWR_IS_MEDIA_SESSION(session));

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "transport_agent", agent);
    g_hash_table_insert(args, "session", session);

    g_object_ref(agent);
    g_object_ref(session);

    _owr_schedule_with_hash_table((GSourceFunc)add_media_session, args);
}


/* Internal functions */

static void add_helper_server_info(GResolver *resolver, GAsyncResult *result, GHashTable *info)
{
    OwrTransportAgent *transport_agent;
    OwrTransportAgentPrivate *priv;
    GList *stream_ids, *item, *address_list;
    guint stream_id;
    GError *error = NULL;

    transport_agent = OWR_TRANSPORT_AGENT(g_hash_table_lookup(info, "transport_agent"));
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_hash_table_remove(info, "transport_agent");

    address_list = g_resolver_lookup_by_name_finish(resolver, result, &error);
    g_object_unref(resolver);

    if (!address_list) {
        g_printerr("Failed to resolve helper server address: %s\n", error->message);
        g_error_free(error);
        g_free(g_hash_table_lookup(info, "username"));
        g_free(g_hash_table_lookup(info, "password"));
        g_hash_table_unref(info);
        goto out;
    }

    g_hash_table_insert(info, "address", g_inet_address_to_string(address_list->data));
    g_resolver_free_addresses(address_list);

    priv = transport_agent->priv;
    priv->helper_server_infos = g_list_append(priv->helper_server_infos, info);

    stream_ids = g_hash_table_get_keys(priv->sessions);
    for (item = stream_ids; item; item = item->next) {
        stream_id = GPOINTER_TO_UINT(item->data);
        update_helper_servers(transport_agent, stream_id);
    }
    g_list_free(stream_ids);

out:
    g_object_unref(transport_agent);
}

static void update_helper_servers(OwrTransportAgent *transport_agent, guint stream_id)
{
    OwrTransportAgentPrivate *priv;
    GHashTable *helper_server_info;
    GList *item;
    OwrHelperServerType type;
    gchar *address, *username, *password;
    guint port;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    priv = transport_agent->priv;

    nice_agent_forget_relays(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTP);
    nice_agent_forget_relays(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTCP);

    for (item = priv->helper_server_infos; item; item = item->next) {
        helper_server_info = item->data;
        type = GPOINTER_TO_UINT(g_hash_table_lookup(helper_server_info, "type"));
        address = g_hash_table_lookup(helper_server_info, "address");
        port = GPOINTER_TO_UINT(g_hash_table_lookup(helper_server_info, "port"));
        username = g_hash_table_lookup(helper_server_info, "username");
        password = g_hash_table_lookup(helper_server_info, "password");

        switch (type) {
        case OWR_HELPER_SERVER_TYPE_STUN:
            g_object_set(priv->nice_agent, "stun-server", address, NULL);
            g_object_set(priv->nice_agent, "stun-server-port", port, NULL);
            break;

        case OWR_HELPER_SERVER_TYPE_TURN_UDP:
            nice_agent_set_relay_info(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTP,
                address, port, username, password, NICE_RELAY_TYPE_TURN_UDP);
            nice_agent_set_relay_info(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTCP,
                address, port, username, password, NICE_RELAY_TYPE_TURN_UDP);
            break;

        case OWR_HELPER_SERVER_TYPE_TURN_TCP:
            nice_agent_set_relay_info(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTP,
                address, port, username, password, NICE_RELAY_TYPE_TURN_TCP);
            nice_agent_set_relay_info(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTCP,
                address, port, username, password, NICE_RELAY_TYPE_TURN_TCP);
            break;
        }
    }
}

static gboolean link_source_to_transport_bin(GstPad *srcpad, GstElement *pipeline, GstElement *transport_bin,
    OwrMediaType media_type, OwrCodecType codec_type, guint stream_id)
{
    GstPad *sinkpad;
    gchar name[OWR_OBJECT_NAME_LENGTH_MAX] = { 0, };
    gboolean ret = FALSE;

    if (media_type == OWR_MEDIA_TYPE_UNKNOWN)
        return FALSE;

    if (media_type == OWR_MEDIA_TYPE_VIDEO)
        g_snprintf(name, OWR_OBJECT_NAME_LENGTH_MAX, "video_sink_%u_%u", codec_type, stream_id);
    else if (media_type == OWR_MEDIA_TYPE_AUDIO)
        g_snprintf(name, OWR_OBJECT_NAME_LENGTH_MAX, "audio_raw_sink_%u", stream_id);
    sinkpad = gst_element_get_static_pad(transport_bin, name);

    ret = gst_pad_link(srcpad, sinkpad) == GST_PAD_LINK_OK;
    if (ret) {
        gst_element_post_message(pipeline,
            gst_message_new_latency(GST_OBJECT(pipeline)));
    }

    return ret;
}

static void handle_new_send_source(OwrTransportAgent *transport_agent,
    OwrMediaSession *media_session)
{
    OwrMediaSource *send_source;
    OwrPayload *send_payload;
    GstElement *transport_bin, *src;
    GstCaps *caps;
    OwrCodecType codec_type = OWR_CODEC_TYPE_NONE;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
    guint stream_id = 0;
    GstPad *srcpad;

    g_return_if_fail(transport_agent);
    g_return_if_fail(media_session);

    send_source = _owr_media_session_get_send_source(media_session);
    g_assert(send_source);

    send_payload = _owr_media_session_get_send_payload(media_session);
    g_assert(send_payload);

    /* check that the source type matches the payload type */
    g_object_get(send_source, "media-type", &media_type, NULL);
    if (!((OWR_IS_AUDIO_PAYLOAD(send_payload) && media_type == OWR_MEDIA_TYPE_AUDIO)
        || (OWR_IS_VIDEO_PAYLOAD(send_payload) && media_type == OWR_MEDIA_TYPE_VIDEO))) {
        GST_ERROR("Cannot send %s as %s", media_type == OWR_MEDIA_TYPE_AUDIO ? "audio" : "video",
            media_type == OWR_MEDIA_TYPE_AUDIO ? "video" : "audio");
        return;
    }

    /* FIXME - communicate what codec types are supported by the source
     * and if one is reusable, use it, else raw?
    g_object_get(send_payload, "codec-type", &codec_type, NULL);
    */

    caps = _owr_payload_create_raw_caps(send_payload);
    src = _owr_media_source_request_source(send_source, caps);
    g_assert(src);
    gst_caps_unref(caps);
    srcpad = gst_element_get_static_pad(src, "src");
    g_assert(srcpad);
    transport_bin = transport_agent->priv->transport_bin;

    stream_id = get_stream_id(transport_agent, OWR_SESSION(media_session));
    g_return_if_fail(stream_id);

    gst_bin_add(GST_BIN(transport_agent->priv->pipeline), src);
    if (!link_source_to_transport_bin(srcpad, transport_agent->priv->pipeline, transport_bin, media_type, codec_type, stream_id)) {
        gchar *name = "";
        g_object_get(send_source, "name", &name, NULL);
        GST_ERROR("Failed to link \"%s\" with transport bin", name);
    }

    gst_element_sync_state_with_parent(src);
}

static void maybe_handle_new_send_source_with_payload(OwrTransportAgent *transport_agent,
    OwrMediaSession *media_session)
{
    if (_owr_media_session_get_send_payload(media_session) &&
        _owr_media_session_get_send_source(media_session)) {
        handle_new_send_payload(transport_agent, media_session);
        handle_new_send_source(transport_agent, media_session);
    }
}

static gboolean add_media_session(GHashTable *args)
{
    OwrTransportAgent *transport_agent;
    OwrMediaSession *media_session, *s;
    GHashTable *sessions;
    GHashTableIter iter;
    gboolean media_session_found = FALSE;
    gboolean rtcp_mux = FALSE;
    guint stream_id;
    gchar *send_rtp_sink_pad_name;
    GstPad *rtp_sink_pad;
    GObject *session;

    g_return_val_if_fail(args, FALSE);

    transport_agent = g_hash_table_lookup(args, "transport_agent");
    media_session = OWR_MEDIA_SESSION(g_hash_table_lookup(args, "session"));

    g_return_val_if_fail(transport_agent, FALSE);
    g_return_val_if_fail(media_session, FALSE);

    sessions = transport_agent->priv->sessions;

    g_hash_table_iter_init(&iter, sessions);
    while (g_hash_table_iter_next(&iter, NULL, (gpointer)&s)) {
        if (s == media_session) {
            media_session_found = TRUE;
            break;
        }
    }

    if (media_session_found) {
        g_warning("An already existing media session was added to the transport agent. Action aborted.");
        goto end;
    }

    g_object_get(media_session, "rtcp-mux", &rtcp_mux, NULL);
    stream_id = nice_agent_add_stream(transport_agent->priv->nice_agent, rtcp_mux ? 1 : 2);
    if (!stream_id) {
        g_warning("Failed to add media session.");
        goto end;
    }

    g_hash_table_insert(sessions, GUINT_TO_POINTER(stream_id), media_session);
    g_object_ref(media_session);

    update_helper_servers(transport_agent, stream_id);

    _owr_media_session_set_on_send_source(media_session,
        g_cclosure_new_object_swap(G_CALLBACK(maybe_handle_new_send_source_with_payload), G_OBJECT(transport_agent)));

    _owr_media_session_set_on_send_payload(media_session,
        g_cclosure_new_object_swap(G_CALLBACK(maybe_handle_new_send_source_with_payload), G_OBJECT(transport_agent)));

    _owr_session_set_on_remote_candidate(OWR_SESSION(media_session),
        g_cclosure_new_object_swap(G_CALLBACK(on_new_remote_candidate), G_OBJECT(transport_agent)));

    /* This is to trigger the creation of the RTP session in RTPBIN */
    send_rtp_sink_pad_name = g_strdup_printf("send_rtp_sink_%u", stream_id);
    rtp_sink_pad = gst_element_get_request_pad(transport_agent->priv->rtpbin, send_rtp_sink_pad_name);
    g_free(send_rtp_sink_pad_name);
    gst_object_unref(rtp_sink_pad);

    prepare_transport_bin_receive_elements(transport_agent, stream_id, rtcp_mux);
    prepare_transport_bin_send_elements(transport_agent, stream_id, rtcp_mux);

    set_send_ssrc_and_cname(transport_agent, media_session);

    if (transport_agent->priv->local_max_port > 0) {
        nice_agent_set_port_range(transport_agent->priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTP,
            transport_agent->priv->local_min_port, transport_agent->priv->local_max_port);
        if (!rtcp_mux) {
            nice_agent_set_port_range(transport_agent->priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTCP,
                transport_agent->priv->local_min_port, transport_agent->priv->local_max_port);
        }
    }

    nice_agent_gather_candidates(transport_agent->priv->nice_agent, stream_id);

    /* stream_id is used as the rtpbin session id */
    g_signal_emit_by_name(transport_agent->priv->rtpbin, "get-internal-session", stream_id, &session);
    g_object_set_data(session, "session_id", GUINT_TO_POINTER(stream_id));
    g_signal_connect_after(session, "on-sending-rtcp", G_CALLBACK(on_sending_rtcp), transport_agent);
    g_signal_connect(session, "on-feedback-rtcp", G_CALLBACK(on_feedback_rtcp), transport_agent);

    maybe_handle_new_send_source_with_payload(transport_agent, media_session);
    if (_owr_session_get_remote_candidates(OWR_SESSION(media_session)))
        on_new_remote_candidate(transport_agent, FALSE, OWR_SESSION(media_session));
    if (_owr_session_get_forced_remote_candidates(OWR_SESSION(media_session)))
        on_new_remote_candidate(transport_agent, TRUE, OWR_SESSION(media_session));

end:
    g_object_unref(media_session);
    g_object_unref(transport_agent);
    g_hash_table_unref(args);
    return FALSE;
}

static GstElement *add_nice_element(OwrTransportAgent *transport_agent, guint stream_id,
    gboolean is_sink, gboolean is_rtcp, GstElement *bin)
{
    GstElement *nice_element = NULL;
    gchar *element_name;
    gboolean added_ok;

    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent), NULL);

    element_name = g_strdup_printf("nice-%s-%s-%u", is_rtcp ? "rtcp" : "rtp", is_sink
        ? "sink" : "src", stream_id);
    nice_element = gst_element_factory_make(is_sink ? "nicesink" : "nicesrc", element_name);
    g_free(element_name);

    g_object_set(nice_element, "agent", transport_agent->priv->nice_agent, NULL);
    g_object_set(nice_element, "stream", stream_id, NULL);
    g_object_set(nice_element, "component", is_rtcp
        ? NICE_COMPONENT_TYPE_RTCP : NICE_COMPONENT_TYPE_RTP, NULL);

    if (is_sink)
        g_object_set(nice_element, "sync", FALSE, "async", FALSE, NULL);

    added_ok = gst_bin_add(GST_BIN(bin), nice_element);
    g_warn_if_fail(added_ok);

    return nice_element;
}

static void set_srtp_key(OwrMediaSession *media_session, GParamSpec *pspec,
    GstElement *dtls_srtp_bin)
{
    GstBuffer *srtp_key_buf = _owr_media_session_get_srtp_key_buffer(media_session,
        g_param_spec_get_name(pspec));
    g_return_if_fail(GST_IS_BUFFER(srtp_key_buf));

    if (gst_buffer_get_size(srtp_key_buf) > 1) {
        g_object_set(dtls_srtp_bin,
            "srtp-auth", "hmac-sha1-80",
            "srtp-cipher", "aes-128-icm",
            "srtcp-auth", "hmac-sha1-80",
            "srtcp-cipher", "aes-128-icm",
            "key", srtp_key_buf,
            NULL);
    } else {
        gchar *dtls_certificate = NULL;
        g_object_get(media_session, "dtls-certificate", &dtls_certificate, NULL);

        if (!dtls_certificate) {
            g_object_set(dtls_srtp_bin,
                "srtp-auth", "null",
                "srtp-cipher", "null",
                "srtcp-auth", "null",
                "srtcp-cipher", "null",
                "key", NULL,
                NULL);
        } else
            g_free(dtls_certificate);
    }

    gst_buffer_unref(srtp_key_buf);
}

static void maybe_disable_dtls(OwrMediaSession *media_session, GParamSpec *pspec,
    GstElement *dtls_srtp_bin)
{
    OWR_UNUSED(pspec);
    OWR_UNUSED(dtls_srtp_bin);

    g_object_notify(G_OBJECT(media_session), "outgoing-srtp-key");
    g_object_notify(G_OBJECT(media_session), "incoming-srtp-key");
}

static void on_dtls_peer_certificate(GstElement *dtls_srtp_bin, GParamSpec *pspec,
    OwrSession *session)
{
    gchar *certificate = NULL;
    OWR_UNUSED(pspec);

    g_return_if_fail(GST_IS_ELEMENT(dtls_srtp_bin));
    g_return_if_fail(OWR_IS_SESSION(session));

    g_object_disconnect(dtls_srtp_bin, "any_signal::notify", G_CALLBACK(on_dtls_peer_certificate),
        session, NULL);
    g_object_get(dtls_srtp_bin, "peer-pem", &certificate, NULL);
    _owr_session_set_dtls_peer_certificate(session, certificate);
    g_free(certificate);
}

static GstElement *add_dtls_srtp_bin(OwrTransportAgent *transport_agent, guint stream_id,
    gboolean is_encoder, gboolean is_rtcp, GstElement *bin)
{
    OwrTransportAgentPrivate *priv;
    OwrMediaSession *media_session;
    GstElement *dtls_srtp_bin = NULL;
    gchar *element_name, *connection_id;
    gchar *cert, *key, *cert_key;
    gboolean added_ok;

    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent), NULL);
    priv = transport_agent->priv;

    element_name = g_strdup_printf("dtls_srtp_%s_%s_%u", is_rtcp ? "rtcp" : "rtp",
        is_encoder ? "encoder" : "decoder", stream_id);

    dtls_srtp_bin = gst_element_factory_make(is_encoder ? "erdtlssrtpenc" : "erdtlssrtpdec",
        element_name);
    connection_id = g_strdup_printf("%s_%u_%u", is_rtcp ? "rtcp" : "rtp",
        priv->agent_id, stream_id);
    g_object_set(dtls_srtp_bin, "connection-id", connection_id, NULL);
    g_free(connection_id);

    media_session = get_media_session(transport_agent, stream_id);
    g_warn_if_fail(OWR_IS_MEDIA_SESSION(media_session));

    if (!is_encoder) {
        g_object_get(media_session, "dtls-certificate", &cert, NULL);
        g_object_get(media_session, "dtls-key", &key, NULL);

        if (!g_strcmp0(cert, "(auto)")) {
            g_object_get(dtls_srtp_bin, "pem", &cert, NULL);
            g_object_set(media_session, "dtls-certificate", cert, NULL);
            g_object_set(media_session, "dtls-key", NULL, NULL);
        } else {
            cert_key = (cert && key) ? g_strdup_printf("%s%s", cert, key) : NULL;
            g_object_set(dtls_srtp_bin, "pem", cert_key, NULL);
            g_free(cert_key);
        }
        g_signal_connect_object(dtls_srtp_bin, "notify::peer-pem",
            G_CALLBACK(on_dtls_peer_certificate), media_session, 0);
        g_free(cert);
        g_free(key);
    } else {
        gboolean dtls_client_mode;
        g_object_get(media_session, "dtls-client-mode", &dtls_client_mode, NULL);
        g_object_set(dtls_srtp_bin, "is-client", dtls_client_mode, NULL);
    }

    g_signal_connect_object(media_session, is_encoder ? "notify::outgoing-srtp-key"
        : "notify::incoming-srtp-key", G_CALLBACK(set_srtp_key), dtls_srtp_bin, 0);
    g_signal_connect_object(G_OBJECT(media_session), "notify::dtls-certificate",
        G_CALLBACK(maybe_disable_dtls), dtls_srtp_bin, 0);
    maybe_disable_dtls(media_session, NULL, dtls_srtp_bin);

    added_ok = gst_bin_add(GST_BIN(bin), dtls_srtp_bin);
    g_warn_if_fail(added_ok);

    g_free(element_name);

    return dtls_srtp_bin;
}

static void transport_bin_pad_linked_cb(GstPad *pad, GstPad *peer, GstElement *bin)
{
    gchar *pad_name, valve_name[OWR_OBJECT_NAME_LENGTH_MAX];
    guint stream_id = -1;
    GstElement *valve = NULL;

    OWR_UNUSED(peer);

    pad_name = gst_pad_get_name(pad);
    if (g_str_has_prefix(pad_name, "audio_raw_src_"))
        sscanf(pad_name, "audio_raw_src_%u", &stream_id);
    if (g_str_has_prefix(pad_name, "video_src_"))
        sscanf(pad_name, "video_src_%*u_%u", &stream_id);
    g_free(pad_name);

    g_return_if_fail(stream_id != (guint)-1);

    FILL_DECODED_VALVE_NAME(valve_name, stream_id);
    valve = gst_bin_get_by_name(GST_BIN(bin), valve_name);
    g_return_if_fail(valve);

    GST_DEBUG_OBJECT(bin, "Opening valve for stream: %u", stream_id);
    g_object_set(valve, "drop", FALSE, NULL);
    gst_object_unref(valve);
}

static GstPad *ghost_pad_and_add_to_bin(GstPad *pad, GstElement *bin, const gchar *pad_name)
{
    gchar *bin_name;
    GstPad *ghost_pad;

    ghost_pad = gst_ghost_pad_new(pad_name, pad);

    gst_pad_set_active(ghost_pad, TRUE);

    bin_name = gst_element_get_name(bin);
    if (g_str_has_prefix(bin_name, "transport_bin_")) {
        if (g_str_has_prefix(pad_name, "video_src_") || g_str_has_prefix(pad_name, "audio_raw_src_"))
            g_signal_connect(ghost_pad, "linked", G_CALLBACK(transport_bin_pad_linked_cb), bin);
    }
    g_free(bin_name);

    gst_element_add_pad(bin, ghost_pad);

    return ghost_pad;
}

static void on_rtcp_mux_changed(OwrMediaSession *media_session, GParamSpec *pspec, GstElement *output_selector)
{
    gboolean rtcp_mux;
    GstPad *pad;
    gchar *pad_name;

    OWR_UNUSED(pspec);

    g_object_get(media_session, "rtcp-mux", &rtcp_mux, NULL);

    pad_name = g_strdup_printf("src_%u", rtcp_mux ? 0 : 1);
    pad = gst_element_get_static_pad(output_selector, pad_name);
    g_warn_if_fail(pad);

    g_object_set(output_selector, "active-pad", pad, NULL);

    gst_object_unref(pad);
    g_free(pad_name);
}

static void link_rtpbin_to_send_output_bin(OwrTransportAgent *transport_agent, guint stream_id,
    GstElement *dtls_srtp_bin_rtp, GstElement *dtls_srtp_bin_rtcp, GstElement *send_output_bin)
{
    gchar *rtpbin_pad_name, *dtls_srtp_pad_name;
    gchar *output_selector_name;
    gboolean linked_ok;
    GstPad *sink_pad, *src_pad;
    GstElement *output_selector;
    OwrMediaSession *media_session;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_return_if_fail(GST_IS_ELEMENT(dtls_srtp_bin_rtp));
    g_return_if_fail(!dtls_srtp_bin_rtcp || GST_IS_ELEMENT(dtls_srtp_bin_rtcp));

    /* RTP */

    rtpbin_pad_name = g_strdup_printf("send_rtp_src_%u", stream_id);
    dtls_srtp_pad_name = g_strdup_printf("rtp_sink_%u", stream_id);

    sink_pad = gst_element_get_request_pad(dtls_srtp_bin_rtp, dtls_srtp_pad_name);
    g_assert(GST_IS_PAD(sink_pad));
    ghost_pad_and_add_to_bin(sink_pad, send_output_bin, dtls_srtp_pad_name);
    gst_object_unref(sink_pad);

    linked_ok = gst_element_link_pads(transport_agent->priv->rtpbin, rtpbin_pad_name,
        send_output_bin, dtls_srtp_pad_name);
    g_warn_if_fail(linked_ok);

    g_free(rtpbin_pad_name);
    g_free(dtls_srtp_pad_name);

    /* RTCP */
    output_selector_name = g_strdup_printf("rtcp_output_selector_%u", stream_id);
    output_selector = gst_element_factory_make("output-selector", output_selector_name);
    g_free(output_selector_name);
    g_object_set(output_selector, "pad-negotiation-mode", 0, NULL);
    gst_bin_add(GST_BIN(send_output_bin), output_selector);
    gst_element_sync_state_with_parent(output_selector);

    rtpbin_pad_name = g_strdup_printf("send_rtcp_src_%u", stream_id);
    dtls_srtp_pad_name = g_strdup_printf("rtcp_sink_%u",  stream_id);

    /* RTCP muxing */
    sink_pad = gst_element_get_request_pad(dtls_srtp_bin_rtp, dtls_srtp_pad_name);
    g_assert(GST_IS_PAD(sink_pad));
    src_pad = gst_element_get_request_pad(output_selector, "src_%u");
    g_assert(GST_IS_PAD(src_pad));
    linked_ok = gst_pad_link(src_pad, sink_pad) == GST_PAD_LINK_OK;
    g_warn_if_fail(linked_ok);
    g_object_set(output_selector, "active-pad", src_pad, NULL);
    gst_object_unref(src_pad);
    gst_object_unref(sink_pad);

    if (dtls_srtp_bin_rtcp) {
        /* RTCP standalone */
        sink_pad = gst_element_get_request_pad(dtls_srtp_bin_rtcp, dtls_srtp_pad_name);
        g_assert(GST_IS_PAD(sink_pad));
        src_pad = gst_element_get_request_pad(output_selector, "src_%u");
        g_assert(GST_IS_PAD(src_pad));
        linked_ok = gst_pad_link(src_pad, sink_pad) == GST_PAD_LINK_OK;
        g_warn_if_fail(linked_ok);
        g_object_set(output_selector, "active-pad", src_pad, NULL);
        gst_object_unref(src_pad);
        gst_object_unref(sink_pad);

        media_session = get_media_session(transport_agent, stream_id);
        g_signal_connect_object(media_session, "notify::rtcp-mux", G_CALLBACK(on_rtcp_mux_changed),
            output_selector, 0);
    }

    sink_pad = gst_element_get_static_pad(output_selector, "sink");
    ghost_pad_and_add_to_bin(sink_pad, send_output_bin, dtls_srtp_pad_name);
    gst_object_unref(sink_pad);

    linked_ok = gst_element_link_pads(transport_agent->priv->rtpbin, rtpbin_pad_name,
        send_output_bin, dtls_srtp_pad_name);
    g_warn_if_fail(linked_ok);

    g_free(rtpbin_pad_name);
    g_free(dtls_srtp_pad_name);
}

static void prepare_transport_bin_send_elements(OwrTransportAgent *transport_agent, guint stream_id,
    gboolean rtcp_mux)
{
    GstElement *nice_element, *dtls_srtp_bin_rtp, *dtls_srtp_bin_rtcp = NULL;
    gboolean linked_ok, synced_ok;
    GstElement *send_output_bin;
    gchar *bin_name;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    bin_name = g_strdup_printf("send-output-bin-%u", stream_id);
    send_output_bin = gst_bin_new(bin_name);
    g_free(bin_name);

    if (!gst_bin_add(GST_BIN(transport_agent->priv->transport_bin), send_output_bin)) {
        GST_ERROR("Failed to add send-output-bin-%u to parent bin", stream_id);
        return;
    }
    if (!gst_element_sync_state_with_parent(send_output_bin)) {
        GST_ERROR("Failed to sync send-output-bin-%u to parent bin", stream_id);
        return;
    }

    nice_element = add_nice_element(transport_agent, stream_id, TRUE, FALSE, send_output_bin);
    dtls_srtp_bin_rtp = add_dtls_srtp_bin(transport_agent, stream_id, TRUE, FALSE, send_output_bin);
    linked_ok = gst_element_link(dtls_srtp_bin_rtp, nice_element);
    g_warn_if_fail(linked_ok);
    synced_ok = gst_element_sync_state_with_parent(nice_element);
    g_warn_if_fail(synced_ok);
    synced_ok = gst_element_sync_state_with_parent(dtls_srtp_bin_rtp);
    g_warn_if_fail(synced_ok);

    if (!rtcp_mux) {
        nice_element = add_nice_element(transport_agent, stream_id, TRUE, TRUE, send_output_bin);
        dtls_srtp_bin_rtcp = add_dtls_srtp_bin(transport_agent, stream_id, TRUE, TRUE, send_output_bin);
        linked_ok = gst_element_link(dtls_srtp_bin_rtcp, nice_element);
        g_warn_if_fail(linked_ok);
        synced_ok = gst_element_sync_state_with_parent(nice_element);
        g_warn_if_fail(synced_ok);
        synced_ok = gst_element_sync_state_with_parent(dtls_srtp_bin_rtcp);
        g_warn_if_fail(synced_ok);
    }

    link_rtpbin_to_send_output_bin(transport_agent, stream_id, dtls_srtp_bin_rtp, dtls_srtp_bin_rtcp, send_output_bin);
}

static void prepare_transport_bin_receive_elements(OwrTransportAgent *transport_agent,
    guint stream_id, gboolean rtcp_mux)
{
    GstElement *nice_element, *dtls_srtp_bin;
    GstPad *rtp_src_pad, *rtcp_src_pad;
    gchar *rtpbin_pad_name;
    gboolean linked_ok, synced_ok;
    GstElement *receive_input_bin;
    gchar *bin_name;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    bin_name = g_strdup_printf("receive-input-bin-%u", stream_id);
    receive_input_bin = gst_bin_new(bin_name);
    g_free(bin_name);

    if (!gst_bin_add(GST_BIN(transport_agent->priv->transport_bin), receive_input_bin)) {
        GST_ERROR("Failed to add receive-input-bin-%u to parent bin", stream_id);
        return;
    }
    if (!gst_element_sync_state_with_parent(receive_input_bin)) {
        GST_ERROR("Failed to sync receive-input-bin-%u to parent bin", stream_id);
        return;
    }

    nice_element = add_nice_element(transport_agent, stream_id, FALSE, FALSE, receive_input_bin);
    dtls_srtp_bin = add_dtls_srtp_bin(transport_agent, stream_id, FALSE, FALSE, receive_input_bin);

    rtp_src_pad = gst_element_get_static_pad(dtls_srtp_bin, "rtp_src");
    ghost_pad_and_add_to_bin(rtp_src_pad, receive_input_bin, "rtp_src");
    gst_object_unref(rtp_src_pad);

    rtpbin_pad_name = g_strdup_printf("recv_rtp_sink_%u", stream_id);
    linked_ok = gst_element_link_pads(receive_input_bin, "rtp_src", transport_agent->priv->rtpbin,
        rtpbin_pad_name);
    g_warn_if_fail(linked_ok);
    g_free(rtpbin_pad_name);
    synced_ok = gst_element_sync_state_with_parent(dtls_srtp_bin);
    g_warn_if_fail(synced_ok);
    linked_ok = gst_element_link(nice_element, dtls_srtp_bin);
    g_warn_if_fail(linked_ok);
    synced_ok = gst_element_sync_state_with_parent(nice_element);
    g_warn_if_fail(synced_ok);

    if (!rtcp_mux) {
        nice_element = add_nice_element(transport_agent, stream_id, FALSE, TRUE, receive_input_bin);
        dtls_srtp_bin = add_dtls_srtp_bin(transport_agent, stream_id, FALSE, TRUE, receive_input_bin);

        rtcp_src_pad = gst_element_get_static_pad(dtls_srtp_bin, "rtp_src");
        ghost_pad_and_add_to_bin(rtcp_src_pad, receive_input_bin, "rtcp_src");
        gst_object_unref(rtcp_src_pad);

        rtpbin_pad_name = g_strdup_printf("recv_rtcp_sink_%u", stream_id);
        linked_ok = gst_element_link_pads(receive_input_bin, "rtcp_src",
            transport_agent->priv->rtpbin, rtpbin_pad_name);
        g_warn_if_fail(linked_ok);
        g_free(rtpbin_pad_name);
        synced_ok = gst_element_sync_state_with_parent(dtls_srtp_bin);
        g_warn_if_fail(synced_ok);
        linked_ok = gst_element_link(nice_element, dtls_srtp_bin);
        g_warn_if_fail(linked_ok);
        synced_ok = gst_element_sync_state_with_parent(nice_element);
        g_warn_if_fail(synced_ok);
    }
}

static void set_send_ssrc_and_cname(OwrTransportAgent *transport_agent, OwrMediaSession *media_session)
{
    guint stream_id;
    GObject *session = NULL;
    GstStructure *sdes = NULL;
    guint send_ssrc = 0;

    g_return_if_fail(transport_agent);
    g_return_if_fail(media_session);

    stream_id = get_stream_id(transport_agent, OWR_SESSION(media_session));
    g_signal_emit_by_name(transport_agent->priv->rtpbin, "get-internal-session", stream_id, &session);
    g_warn_if_fail(session);
    g_object_get(session, "internal-ssrc", &send_ssrc, "sdes", &sdes, NULL);
    _owr_media_session_set_send_ssrc(media_session, send_ssrc);
    _owr_media_session_set_cname(media_session, gst_structure_get_string(sdes, "cname"));
    g_object_notify(G_OBJECT(media_session), "send-ssrc");
    g_object_notify(G_OBJECT(media_session), "cname");
    gst_structure_free(sdes);
    g_object_unref(session);
}

static gboolean emit_new_candidate(GHashTable *args)
{
    OwrTransportAgent *transport_agent;
    OwrTransportAgentPrivate *priv;
    OwrMediaSession *media_session;
    guint stream_id, component_id;
    gchar *foundation;
    GSList *lcands, *item;
    NiceCandidate *nice_candidate = NULL;
    OwrCandidate *owr_candidate;
    gchar *ufrag = NULL, *password = NULL;
    gboolean got_credentials;

    transport_agent = OWR_TRANSPORT_AGENT(g_hash_table_lookup(args, "transport_agent"));
    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent), FALSE);
    priv = transport_agent->priv;

    stream_id = GPOINTER_TO_UINT(g_hash_table_lookup(args, "stream_id"));
    component_id = GPOINTER_TO_UINT(g_hash_table_lookup(args, "component_id"));
    foundation = g_hash_table_lookup(args, "foundation");

    media_session = g_hash_table_lookup(priv->sessions, GUINT_TO_POINTER(stream_id));
    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), FALSE);

    lcands = nice_agent_get_local_candidates(priv->nice_agent, stream_id, component_id);
    for (item = lcands; item; item = item->next) {
        nice_candidate = item->data;
        g_warn_if_fail(nice_candidate->component_id == component_id);

        if (!g_strcmp0(nice_candidate->foundation, foundation))
            break;
    }
    if (!item)
        goto out;

    if (!nice_candidate->username || !nice_candidate->password) {
        got_credentials = nice_agent_get_local_credentials(priv->nice_agent, stream_id,
            &ufrag, &password);
        g_warn_if_fail(got_credentials);

        if (!nice_candidate->username)
            nice_candidate->username = ufrag;
        else
            g_free(ufrag);

        if (!nice_candidate->password)
            nice_candidate->password = password;
        else
            g_free(password);
    }

    owr_candidate = _owr_candidate_new_from_nice_candidate(nice_candidate);
    g_slist_free_full(lcands, (GDestroyNotify)nice_candidate_free);
    g_return_val_if_fail(owr_candidate, FALSE);

    g_signal_emit_by_name(media_session, "on-new-candidate", owr_candidate);

out:
    g_free(foundation);
    g_hash_table_destroy(args);
    g_object_unref(transport_agent);

    return FALSE;
}

static void on_new_candidate(NiceAgent *nice_agent, guint stream_id, guint component_id,
    gchar *foundation, OwrTransportAgent *transport_agent)
{
    GHashTable *args;

    g_return_if_fail(nice_agent);
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    g_object_ref(transport_agent);
    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "transport_agent", transport_agent);
    g_hash_table_insert(args, "stream_id", GUINT_TO_POINTER(stream_id));
    g_hash_table_insert(args, "component_id", GUINT_TO_POINTER(component_id));
    g_hash_table_insert(args, "foundation", g_strdup(foundation));

    _owr_schedule_with_hash_table((GSourceFunc)emit_new_candidate, args);
}

static gboolean emit_candidate_gathering_done(GHashTable *args)
{
    OwrMediaSession *media_session;

    media_session = OWR_MEDIA_SESSION(g_hash_table_lookup(args, "media_session"));
    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), FALSE);

    g_signal_emit_by_name(media_session, "on-candidate-gathering-done", NULL);

    g_hash_table_destroy(args);
    g_object_unref(media_session);

    return FALSE;
}

static void on_candidate_gathering_done(NiceAgent *nice_agent, guint stream_id, OwrTransportAgent *transport_agent)
{
    OwrMediaSession *media_session;
    GHashTable *args;

    g_return_if_fail(nice_agent);
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    media_session = g_hash_table_lookup(transport_agent->priv->sessions, GUINT_TO_POINTER(stream_id));
    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));
    g_object_ref(media_session);

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "media_session", media_session);

    _owr_schedule_with_hash_table((GSourceFunc)emit_candidate_gathering_done, args);
}

static guint get_stream_id(OwrTransportAgent *transport_agent, OwrSession *session)
{
    GHashTableIter iter;
    OwrSession *s;
    gpointer stream_id = GUINT_TO_POINTER(0);

    g_hash_table_iter_init(&iter, transport_agent->priv->sessions);
    while (g_hash_table_iter_next(&iter, &stream_id, (gpointer)&s)) {
        if (s == session)
            return GPOINTER_TO_UINT(stream_id);
    }

    g_warn_if_reached();
    return 0;
}

static OwrMediaSession * get_media_session(OwrTransportAgent *transport_agent, guint stream_id)
{
    GHashTableIter iter;
    OwrMediaSession *s = NULL;
    gpointer id = GUINT_TO_POINTER(0);

    g_hash_table_iter_init(&iter, transport_agent->priv->sessions);
    while (g_hash_table_iter_next(&iter, &id, (gpointer)&s)) {
        if (GPOINTER_TO_UINT(id) == stream_id)
            break;
    }

    return s;
}

/* pad is transfer full */
static void add_pads_to_bin_and_transport_bin(GstPad *pad, GstElement *bin, GstElement *transport_bin,
    const gchar *pad_name)
{
    GstPad *bin_pad;

    bin_pad = ghost_pad_and_add_to_bin(pad, bin, pad_name);

    ghost_pad_and_add_to_bin(bin_pad, transport_bin, pad_name);
}

static void handle_new_send_payload(OwrTransportAgent *transport_agent, OwrMediaSession *media_session)
{
    guint stream_id;
    OwrPayload *payload = NULL;
    GstElement *send_input_bin = NULL;
    GstElement *capsfilter = NULL, *encoder = NULL, *parser = NULL, *payloader = NULL,
        *rtp_capsfilter = NULL, *rtpbin = NULL;
    GstCaps *caps = NULL, *rtp_caps = NULL;
    gchar *name = NULL;
    gboolean link_ok = TRUE, sync_ok = TRUE;
    GstPad *sink_pad = NULL, *rtp_sink_pad = NULL, *rtp_capsfilter_src_pad = NULL,
        *ghost_src_pad = NULL;
    OwrMediaType media_type;
    GstPadLinkReturn link_res;

    g_return_if_fail(transport_agent);
    g_return_if_fail(media_session);

    stream_id = get_stream_id(transport_agent, OWR_SESSION(media_session));
    g_return_if_fail(stream_id);

    name = g_strdup_printf("send-input-bin-%u", stream_id);
    send_input_bin = gst_bin_new(name);
    g_free(name);

    gst_bin_add(GST_BIN(transport_agent->priv->transport_bin), send_input_bin);
    if (!gst_element_sync_state_with_parent(send_input_bin)) {
        GST_ERROR("Failed to sync send-input-bin-%u state with parent", stream_id);
        return;
    }

    rtpbin = transport_agent->priv->rtpbin;
    name = g_strdup_printf("send_rtp_sink_%u", stream_id);
    rtp_sink_pad = gst_element_get_static_pad(rtpbin, name);
    g_free(name);

    payload = _owr_media_session_get_send_payload(media_session);
    g_object_get(payload, "media-type", &media_type, NULL);

    name = g_strdup_printf("send-rtp-capsfilter-%u", stream_id);
    rtp_capsfilter = gst_element_factory_make("capsfilter", name);
    g_free(name);
    rtp_caps = _owr_payload_create_rtp_caps(payload);

    g_object_set(rtp_capsfilter, "caps", rtp_caps, NULL);
    gst_caps_unref(rtp_caps);
    gst_bin_add(GST_BIN(send_input_bin), rtp_capsfilter);

    rtp_capsfilter_src_pad = gst_element_get_static_pad(rtp_capsfilter, "src");
    name = g_strdup_printf("src_%u", stream_id);
    ghost_src_pad = ghost_pad_and_add_to_bin(rtp_capsfilter_src_pad, send_input_bin, name);
    gst_object_unref(rtp_capsfilter_src_pad);
    g_free(name);

    link_res = gst_pad_link(ghost_src_pad, rtp_sink_pad);
    g_warn_if_fail(link_res == GST_PAD_LINK_OK);
    gst_object_unref(rtp_sink_pad);

    sync_ok &= gst_element_sync_state_with_parent(rtp_capsfilter);
    g_warn_if_fail(sync_ok);

    if (media_type == OWR_MEDIA_TYPE_VIDEO) {
        GstElement *queue = NULL, *encoder_capsfilter;

        name = g_strdup_printf("send-input-video-queue-%u", stream_id);
        queue = gst_element_factory_make("queue", name);
        g_free(name);
        g_object_set(queue, "max-size-buffers", 3, "max-size-bytes", 0,
            "max-size-time", G_GUINT64_CONSTANT(0), "leaky", 2 /* leak downstream */, NULL);

        encoder = _owr_payload_create_encoder(payload);
        parser = _owr_payload_create_parser(payload);
        payloader = _owr_payload_create_payload_packetizer(payload);
        g_warn_if_fail(payloader && encoder);

        name = g_strdup_printf("send-input-video-encoder-capsfilter-%u", stream_id);
        encoder_capsfilter = gst_element_factory_make("capsfilter", name);
        g_free(name);
        caps = _owr_payload_create_encoded_caps(payload);
        g_object_set(encoder_capsfilter, "caps", caps, NULL);
        gst_caps_unref(caps);

        gst_bin_add_many(GST_BIN(send_input_bin), queue, encoder, encoder_capsfilter, payloader, NULL);
        if (parser) {
            gst_bin_add(GST_BIN(send_input_bin), parser);
            link_ok &= gst_element_link_many(queue, encoder, parser, encoder_capsfilter, payloader, NULL);
        } else {
            link_ok &= gst_element_link_many(queue, encoder, encoder_capsfilter, payloader, NULL);
        }
        link_ok &= gst_element_link_many(payloader, rtp_capsfilter, NULL);

        g_warn_if_fail(link_ok);

        sync_ok &= gst_element_sync_state_with_parent(rtp_capsfilter);
        sync_ok &= gst_element_sync_state_with_parent(payloader);
        if (parser)
            sync_ok &= gst_element_sync_state_with_parent(parser);
        sync_ok &= gst_element_sync_state_with_parent(encoder_capsfilter);
        sync_ok &= gst_element_sync_state_with_parent(encoder);
        sync_ok &= gst_element_sync_state_with_parent(queue);

        name = g_strdup_printf("video_sink_%u_%u", OWR_CODEC_TYPE_NONE, stream_id);
        sink_pad = gst_element_get_static_pad(queue, "sink");
        add_pads_to_bin_and_transport_bin(sink_pad, send_input_bin,
            transport_agent->priv->transport_bin, name);
        gst_object_unref(sink_pad);
        g_free(name);
    } else { /* Audio */
        name = g_strdup_printf("send_capsfilter_%u", stream_id);
        capsfilter = gst_element_factory_make("capsfilter", name);
        g_free(name);

        caps = _owr_payload_create_raw_caps(payload);
        g_object_set(capsfilter, "caps", caps, NULL);
        gst_caps_unref(caps);

        encoder = _owr_payload_create_encoder(payload);
        parser = _owr_payload_create_parser(payload);
        payloader = _owr_payload_create_payload_packetizer(payload);

        gst_bin_add_many(GST_BIN(send_input_bin), capsfilter, encoder, payloader, NULL);
        link_ok = gst_element_link_many(capsfilter, encoder, NULL);
        if (parser) {
            gst_bin_add(GST_BIN(send_input_bin), parser);
            link_ok &= gst_element_link_many(encoder, parser, payloader, NULL);
        } else {
            link_ok &= gst_element_link_many(encoder, payloader, NULL);
        }
        link_ok &= gst_element_link_many (payloader, rtp_capsfilter, NULL);
        g_warn_if_fail(link_ok);

        sync_ok &= gst_element_sync_state_with_parent(rtp_capsfilter);
        sync_ok &= gst_element_sync_state_with_parent(payloader);
        if (parser)
            sync_ok &= gst_element_sync_state_with_parent(parser);
        sync_ok &= gst_element_sync_state_with_parent(encoder);
        sync_ok &= gst_element_sync_state_with_parent(capsfilter);
        g_warn_if_fail(sync_ok);

        name = g_strdup_printf("audio_raw_sink_%u", stream_id);
        sink_pad = gst_element_get_static_pad(capsfilter, "sink");
        add_pads_to_bin_and_transport_bin(sink_pad, send_input_bin,
            transport_agent->priv->transport_bin, name);
        gst_object_unref(sink_pad);
        g_free(name);
    }
}

static void on_new_remote_candidate(OwrTransportAgent *transport_agent, gboolean forced, OwrSession *session)
{
    guint stream_id;
    NiceCandidate *nice_candidate;
    GSList *item, *nice_cands_rtp = NULL, *nice_cands_rtcp = NULL;
    gchar *username = NULL, *password = NULL;
    gboolean forced_ok;
    gint cands_added;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_return_if_fail(OWR_IS_SESSION(session));

    stream_id = get_stream_id(transport_agent, session);
    g_return_if_fail(stream_id);

    item = forced ? _owr_session_get_forced_remote_candidates(session) :
        _owr_session_get_remote_candidates(session);
    for (; item; item = item->next) {
        nice_candidate = _owr_candidate_to_nice_candidate(OWR_CANDIDATE(item->data));

        if (!nice_candidate)
            continue;

        if (!username)
            username = nice_candidate->username;
        else if (g_strcmp0(username, nice_candidate->username))
            g_warning("'username' not the same for all remote candidates in the session");

        if (!password)
            password = nice_candidate->password;
        else if (g_strcmp0(password, nice_candidate->password))
            g_warning("'password' not the same for all remote candidates in the session");

        if (forced) {
            forced_ok = nice_agent_set_selected_remote_candidate(transport_agent->priv->nice_agent,
                stream_id, nice_candidate->component_id, nice_candidate);
            g_warn_if_fail(forced_ok);
        } else if (nice_candidate->component_id == NICE_COMPONENT_TYPE_RTP)
            nice_cands_rtp = g_slist_append(nice_cands_rtp, nice_candidate);
        else
            nice_cands_rtcp = g_slist_append(nice_cands_rtcp, nice_candidate);
    }

    if (username && password) {
        nice_agent_set_remote_credentials(transport_agent->priv->nice_agent, stream_id,
            username, password);
    }

    if (nice_cands_rtp) {
        cands_added = nice_agent_set_remote_candidates(transport_agent->priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTP, nice_cands_rtp);
        g_slist_free_full(nice_cands_rtp, (GDestroyNotify)nice_candidate_free);
        g_warn_if_fail(cands_added > 0);
    }

    if (nice_cands_rtcp) {
        cands_added = nice_agent_set_remote_candidates(transport_agent->priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTCP, nice_cands_rtcp);
        g_slist_free_full(nice_cands_rtcp, (GDestroyNotify)nice_candidate_free);
        g_warn_if_fail(cands_added > 0);
    }
}

static gboolean emit_on_incoming_source(GHashTable *args)
{
    OwrMediaSession *media_session;
    OwrMediaSource *source;

    media_session = g_hash_table_lookup(args, "media_session");
    source = g_hash_table_lookup(args, "source");

    g_signal_emit_by_name(media_session, "on-incoming-source", source);

    g_hash_table_unref(args);
    return FALSE;
}

static void signal_incoming_source(OwrMediaType type, OwrTransportAgent *transport_agent,
    guint stream_id, OwrCodecType codec_type)
{
    OwrMediaSession *media_session;
    OwrMediaSource *source;
    GHashTable *args;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    media_session = get_media_session(transport_agent, stream_id);

    source = _owr_remote_media_source_new(type, stream_id, codec_type,
        transport_agent->priv->transport_bin);

    g_return_if_fail(source);

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "media_session", media_session);
    g_hash_table_insert(args, "source", source);

    _owr_schedule_with_hash_table((GSourceFunc)emit_on_incoming_source, args);
}

static void on_transport_bin_pad_added(GstElement *transport_bin, GstPad *new_pad, OwrTransportAgent *transport_agent)
{
    gchar *new_pad_name;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
    OwrCodecType codec_type = OWR_CODEC_TYPE_NONE;
    guint stream_id = 0;

    g_return_if_fail(GST_IS_BIN(transport_bin));
    g_return_if_fail(GST_IS_PAD(new_pad));
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    new_pad_name = gst_pad_get_name(new_pad);

    if (g_str_has_prefix(new_pad_name, "audio_raw_src")) {
        media_type = OWR_MEDIA_TYPE_AUDIO;
        codec_type = OWR_CODEC_TYPE_NONE;
        sscanf(new_pad_name, "audio_raw_src_%u", &stream_id);
    } else if (g_str_has_prefix(new_pad_name, "video_src_")) {
        media_type = OWR_MEDIA_TYPE_VIDEO;
        sscanf(new_pad_name, "video_src_%u_%u", &codec_type, &stream_id);
    }

    if (media_type != OWR_MEDIA_TYPE_UNKNOWN && codec_type == OWR_CODEC_TYPE_NONE)
        signal_incoming_source(media_type, transport_agent, stream_id, codec_type);

    g_free(new_pad_name);
}

static void on_rtpbin_pad_added(GstElement *rtpbin, GstPad *new_pad, OwrTransportAgent *transport_agent)
{
    gchar *new_pad_name = NULL;

    g_return_if_fail(rtpbin);
    g_return_if_fail(new_pad);
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    new_pad_name = gst_pad_get_name(new_pad);
    if (g_str_has_prefix(new_pad_name, "recv_rtp_src_")) {
        guint32 session_id = 0, ssrc = 0, pt = 0;
        OwrMediaSession *media_session = NULL;
        OwrPayload *payload = NULL;
        OwrMediaType media_type;

        sscanf(new_pad_name, "recv_rtp_src_%u_%u_%u", &session_id, &ssrc, &pt);

        media_session = get_media_session(transport_agent, session_id);
        payload = _owr_media_session_get_receive_payload(media_session, pt);

        g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));
        g_return_if_fail(OWR_IS_PAYLOAD(payload));
        g_object_get(payload, "media-type", &media_type, NULL);

        g_object_set_data(G_OBJECT(media_session), "ssrc", GUINT_TO_POINTER(ssrc));
        if (media_type == OWR_MEDIA_TYPE_VIDEO)
            setup_video_receive_elements(new_pad, session_id, payload, transport_agent);
        else
            setup_audio_receive_elements(new_pad, session_id, payload, transport_agent);
    } else if (g_str_has_prefix(new_pad_name, "send_rtp_src")) {
        guint32 session_id = 0;
        sscanf(new_pad_name, "send_rtp_src_%u", &session_id);
    }

    g_free(new_pad_name);
}

static void setup_video_receive_elements(GstPad *new_pad, guint32 session_id, OwrPayload *payload, OwrTransportAgent *transport_agent)
{
    GstPad *depay_sink_pad = NULL, *ghost_pad = NULL;
    gboolean sync_ok = TRUE;
    GstElement *receive_output_bin;
    GstElement *rtpdepay, *videorepair1, *decoded_valve, *parser, *decoder;
    GstPadLinkReturn link_res;
    gboolean link_ok = TRUE;
    OwrCodecType codec_type;
    gchar name[100];
    GstPad *pad;

    g_object_set_data(G_OBJECT(new_pad), "transport-agent", (gpointer)transport_agent);
    g_object_set_data(G_OBJECT(new_pad), "session-id", GUINT_TO_POINTER(session_id));

    g_snprintf(name, OWR_OBJECT_NAME_LENGTH_MAX, "receive-output-bin-%u", session_id);
    receive_output_bin = gst_bin_new(name);

    gst_bin_add(GST_BIN(transport_agent->priv->transport_bin), receive_output_bin);
    if (!gst_element_sync_state_with_parent(receive_output_bin)) {
        GST_ERROR("Failed to sync receive-output-bin-%u state with parent", session_id);
        return;
    }

    rtpdepay = _owr_payload_create_payload_depacketizer(payload);
    g_snprintf(name, OWR_OBJECT_NAME_LENGTH_MAX, "videorepair1_%u", session_id);
    videorepair1 = gst_element_factory_make("videorepair", name);
    FILL_DECODED_VALVE_NAME(name, session_id);
    decoded_valve = gst_element_factory_make("valve", name);
    g_object_set(decoded_valve, "drop", TRUE, NULL);
    g_object_get(payload, "codec-type", &codec_type, NULL);
    parser = _owr_payload_create_parser(payload);
    decoder = _owr_payload_create_decoder(payload);

    gst_bin_add_many(GST_BIN(receive_output_bin), rtpdepay,
        decoded_valve, videorepair1, decoder, /*decoded_tee,*/ NULL);
    depay_sink_pad = gst_element_get_static_pad(rtpdepay, "sink");
    if (parser) {
        gst_bin_add(GST_BIN(receive_output_bin), parser);
        link_ok &= gst_element_link_many(rtpdepay, parser, videorepair1, decoder, NULL);
    } else {
        link_ok &= gst_element_link_many(rtpdepay, videorepair1, decoder, NULL);
    }
    link_ok &= gst_element_link_many(decoder, decoded_valve, NULL);

    ghost_pad = ghost_pad_and_add_to_bin(depay_sink_pad, receive_output_bin, "sink");
    link_res = gst_pad_link(new_pad, ghost_pad);
    gst_object_unref(depay_sink_pad);
    ghost_pad = NULL;
    g_warn_if_fail(link_ok && (link_res == GST_PAD_LINK_OK));

    sync_ok &= gst_element_sync_state_with_parent(decoder);
    if (parser)
        sync_ok &= gst_element_sync_state_with_parent(parser);
    sync_ok &= gst_element_sync_state_with_parent(videorepair1);
    sync_ok &= gst_element_sync_state_with_parent(decoded_valve);
    sync_ok &= gst_element_sync_state_with_parent(rtpdepay);
    g_warn_if_fail(sync_ok);

    pad = gst_element_get_static_pad(decoded_valve, "src");
    g_snprintf(name, OWR_OBJECT_NAME_LENGTH_MAX, "video_src_%u_%u", OWR_CODEC_TYPE_NONE,
        session_id);
    add_pads_to_bin_and_transport_bin(pad, receive_output_bin, transport_agent->priv->transport_bin, name);
    gst_object_unref(pad);
}

static void setup_audio_receive_elements(GstPad *new_pad, guint32 session_id, OwrPayload *payload, OwrTransportAgent *transport_agent)
{
    GstElement *receive_output_bin;
    gchar *pad_name = NULL;
    GstElement *rtp_capsfilter, *rtpdepay, *parser, *decoder, *decoded_valve;
    GstPad *rtp_caps_sink_pad = NULL, *pad = NULL, *ghost_pad = NULL;
    gchar *element_name = NULL;
    GstCaps *rtp_caps = NULL;
    gboolean link_ok = FALSE;
    gboolean sync_ok = TRUE;

    g_object_set_data(G_OBJECT(new_pad), "transport-agent", (gpointer)transport_agent);
    g_object_set_data(G_OBJECT(new_pad), "session-id", GUINT_TO_POINTER(session_id));

    pad_name = g_strdup_printf("receive-output-bin-%u", session_id);
    receive_output_bin = gst_bin_new(pad_name);
    g_free(pad_name);
    pad_name = NULL;

    gst_bin_add(GST_BIN(transport_agent->priv->transport_bin), receive_output_bin);
    sync_ok &= gst_element_sync_state_with_parent(receive_output_bin);

    element_name = g_strdup_printf("recv-rtp-capsfilter-%u", session_id);
    rtp_capsfilter = gst_element_factory_make("capsfilter", element_name);
    g_free(element_name);
    rtp_caps = _owr_payload_create_rtp_caps(payload);
    g_object_set(rtp_capsfilter, "caps", rtp_caps, NULL);
    gst_caps_unref(rtp_caps);

    rtpdepay = _owr_payload_create_payload_depacketizer(payload);

    pad_name = g_strdup_printf("decoded_valve_%u", session_id);
    decoded_valve = gst_element_factory_make("valve", pad_name);
    g_object_set(decoded_valve, "drop", TRUE, NULL);
    g_free(pad_name);

    parser = _owr_payload_create_parser(payload);
    decoder = _owr_payload_create_decoder(payload);

    gst_bin_add_many(GST_BIN(receive_output_bin), rtp_capsfilter, rtpdepay,
        decoded_valve, decoder, NULL);
    link_ok = gst_element_link_many(rtp_capsfilter, rtpdepay, NULL);
    if (parser) {
        gst_bin_add(GST_BIN(receive_output_bin), parser);
        link_ok &= gst_element_link_many(rtpdepay, parser, decoder, decoded_valve, NULL);
    } else {
        link_ok &= gst_element_link_many(rtpdepay, decoder, decoded_valve, NULL);
    }
    g_warn_if_fail(link_ok);

    rtp_caps_sink_pad = gst_element_get_static_pad(rtp_capsfilter, "sink");
    ghost_pad = ghost_pad_and_add_to_bin(rtp_caps_sink_pad, receive_output_bin, "sink");
    if (!GST_PAD_LINK_SUCCESSFUL(gst_pad_link(new_pad, ghost_pad))) {
        GST_ERROR("Failed to link rtpbin with receive-output-bin-%u", session_id);
        return;
    }
    ghost_pad = NULL;

    sync_ok &= gst_element_sync_state_with_parent(decoder);
    if (parser)
        sync_ok &= gst_element_sync_state_with_parent(parser);
    sync_ok &= gst_element_sync_state_with_parent(decoded_valve);
    sync_ok &= gst_element_sync_state_with_parent(rtpdepay);
    sync_ok &= gst_element_sync_state_with_parent(rtp_capsfilter);
    g_warn_if_fail(sync_ok);

    pad = gst_element_get_static_pad(decoded_valve, "src");
    pad_name = g_strdup_printf("audio_raw_src_%u", session_id);
    add_pads_to_bin_and_transport_bin(pad, receive_output_bin,
        transport_agent->priv->transport_bin, pad_name);
    gst_object_unref(pad);
    g_free(pad_name);
}


static GstCaps * on_rtpbin_request_pt_map(GstElement *rtpbin, guint session_id, guint pt, OwrTransportAgent *transport_agent)
{
    OwrMediaSession *media_session = NULL;
    OwrPayload *payload = NULL;
    GstCaps *caps = NULL;

    g_return_val_if_fail(rtpbin, NULL);
    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent), NULL);

    media_session = OWR_MEDIA_SESSION(g_hash_table_lookup(transport_agent->priv->sessions, GUINT_TO_POINTER(session_id)));
    payload = _owr_media_session_get_receive_payload(media_session, pt);

    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), NULL);
    g_return_val_if_fail(OWR_IS_PAYLOAD(payload), NULL);
    caps = _owr_payload_create_rtp_caps(payload);
    g_object_unref(payload);

    return caps;
}

static gboolean on_sending_rtcp(GObject *session, GstBuffer *buffer, gboolean early,
    OwrTransportAgent *agent)
{
    GstRTCPBuffer rtcp_buffer = {NULL, {NULL, 0, NULL, 0, 0, {0}, {0}}};
    GstRTCPPacket rtcp_packet;
    GstRTCPType packet_type;
    gboolean has_packet, do_not_suppress = FALSE;
    OwrMediaSession *media_session;
    OwrPayload *send_payload;
    OwrMediaType media_type = -1;
    GValueArray *sources = NULL;
    GObject *source = NULL;
    guint session_id = 0;

    OWR_UNUSED(early);

    if (gst_rtcp_buffer_map(buffer, GST_MAP_READ, &rtcp_buffer)) {
        has_packet = gst_rtcp_buffer_get_first_packet(&rtcp_buffer, &rtcp_packet);
        for (; has_packet; has_packet = gst_rtcp_packet_move_to_next(&rtcp_packet)) {
            packet_type = gst_rtcp_packet_get_type(&rtcp_packet);
            if (packet_type == GST_RTCP_TYPE_PSFB || packet_type == GST_RTCP_TYPE_RTPFB) {
                do_not_suppress = TRUE;
                break;
            }
        }
        gst_rtcp_buffer_unmap(&rtcp_buffer);
    }

    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(agent), do_not_suppress);

    session_id = GPOINTER_TO_UINT(g_object_get_data(session, "session_id"));
    media_session = get_media_session(agent, session_id);
    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), do_not_suppress);
    send_payload = _owr_media_session_get_send_payload(media_session);
    if (send_payload)
        g_object_get(send_payload, "media-type", &media_type, NULL);

    g_object_get(session, "sources", &sources, NULL);
    source = g_value_get_object(g_value_array_get_nth(sources, 0));
    prepare_rtcp_stats(media_session, source);
    g_value_array_free(sources);

    return do_not_suppress;
}

static gboolean update_stats_hash_table(GQuark field_id, const GValue *src_value,
    GHashTable *stats_hash_table)
{
    gchar *key = g_strdup(g_quark_to_string(field_id));
    GValue *value = g_slice_new0(GValue);
    value = g_value_init(value, G_VALUE_TYPE(src_value));
    g_value_copy(src_value, value);
    g_hash_table_insert(stats_hash_table, key, value);
    return TRUE;
}

static void value_slice_free(gpointer value)
{
    g_value_unset(value);
    g_slice_free(GValue, value);
}

static gboolean emit_stats_signal(GHashTable *stats_hash)
{
    GValue *value;
    OwrMediaSession *media_session;

    g_return_val_if_fail(stats_hash, FALSE);
    value = g_hash_table_lookup(stats_hash, "media_session");
    g_return_val_if_fail(G_VALUE_HOLDS_OBJECT(value), FALSE);
    media_session = g_value_get_object(value);
    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), FALSE);
    g_hash_table_remove(stats_hash, "media_session");
    g_signal_emit_by_name(media_session, "on-new-stats", stats_hash, NULL);
    return FALSE;
}

static void prepare_rtcp_stats(OwrMediaSession *media_session, GObject *rtp_source)
{
    GstStructure *stats;
    GHashTable *stats_hash;
    GValue *value;

    g_object_get(rtp_source, "stats", &stats, NULL);
    stats_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, value_slice_free);
    value = g_slice_new0(GValue);
    value = g_value_init(value, G_TYPE_STRING);
    g_value_set_string(value, "rtcp");
    g_hash_table_insert(stats_hash, g_strdup("type"), value);
    gst_structure_foreach(stats,
        (GstStructureForeachFunc)update_stats_hash_table, stats_hash);
    gst_structure_free(stats);

    value = g_slice_new0(GValue);
    value = g_value_init(value, OWR_TYPE_MEDIA_SESSION);
    g_value_set_object(value, media_session);
    g_hash_table_insert(stats_hash, g_strdup("media_session"), value);

    _owr_schedule_with_hash_table((GSourceFunc)emit_stats_signal, stats_hash);
}

static void on_ssrc_active(GstElement *rtpbin, guint session_id, guint ssrc,
    OwrTransportAgent *transport_agent)
{
    OwrMediaSession *media_session;
    GObject *rtp_session, *rtp_source;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    media_session = get_media_session(transport_agent, session_id);
    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));

    g_signal_emit_by_name(rtpbin, "get-internal-session", session_id, &rtp_session);
    g_signal_emit_by_name(rtp_session, "get-source-by-ssrc", ssrc, &rtp_source);
    prepare_rtcp_stats(media_session, rtp_source);
    g_object_unref(rtp_source);
    g_object_unref(rtp_session);
}

static void on_feedback_rtcp(GObject *session, guint fbtype, guint type, guint sender_ssrc, guint media_ssrc, GstBuffer *fci, OwrTransportAgent *transport_agent)
{
    g_return_if_fail(session);
    g_return_if_fail(transport_agent);

    OWR_UNUSED(sender_ssrc);
    OWR_UNUSED(media_ssrc);
    OWR_UNUSED(fci);

    /* FIXME: temporary solution until everyone supports RTP retransmission properly */
    if (fbtype == GST_RTCP_TYPE_RTPFB && type == GST_RTCP_RTPFB_TYPE_NACK) {
        GstPad *rtp_sink_pad;
        guint session_id = GPOINTER_TO_UINT(g_object_get_data(session, "session_id"));
        gchar *name = g_strdup_printf("send_rtp_sink_%u", session_id);
        g_warn_if_fail(session_id);
        rtp_sink_pad = gst_element_get_static_pad(transport_agent->priv->rtpbin, name);
        g_free(name);
        g_warn_if_fail(GST_IS_PAD(rtp_sink_pad));
        gst_pad_push_event(rtp_sink_pad, gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM,
            gst_structure_new("GstForceKeyUnit", "all-headers", G_TYPE_BOOLEAN, FALSE, NULL)));
        gst_object_unref(rtp_sink_pad);
    }
}
