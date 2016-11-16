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
\*\ OwrTransportAgent
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_transport_agent.h"

#include "owr_arrival_time_meta.h"
#include "owr_audio_payload.h"
#include "owr_candidate_private.h"
#include "owr_data_channel.h"
#include "owr_data_channel_private.h"
#include "owr_data_channel_protocol.h"
#include "owr_data_session.h"
#include "owr_data_session_private.h"
#include "owr_media_session.h"
#include "owr_media_session_private.h"
#include "owr_media_source.h"
#include "owr_media_source_private.h"
#include "owr_message_origin_private.h"
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
#include <interfaces.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtpdefs.h>
#include <gst/sctp/sctpreceivemeta.h>
#include <gst/sctp/sctpsendmeta.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN(_owrtransportagent_debug);
GST_DEBUG_CATEGORY_EXTERN(_owrsession_debug);
#define GST_CAT_DEFAULT _owrtransportagent_debug

#define DEFAULT_ICE_CONTROLLING_MODE TRUE
#define GST_RTCP_RTPFB_TYPE_SCREAM 18

enum {
    PROP_0,
    PROP_ICE_CONTROLLING_MODE,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };
static guint next_transport_agent_id = 1;

#define OWR_TRANSPORT_AGENT_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_TRANSPORT_AGENT, OwrTransportAgentPrivate))

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface);

G_DEFINE_TYPE_WITH_CODE(OwrTransportAgent, owr_transport_agent, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(OWR_TYPE_MESSAGE_ORIGIN, owr_message_origin_interface_init))

typedef struct {
    OwrDataChannelState state;
    GstElement *data_sink, *data_src;
    guint session_id;

    gboolean ordered;
    gint max_packet_life_time;
    gint max_packet_retransmits;
    gchar *protocol;
    gboolean negotiated;
    guint16 id;
    gchar *label;
    GRWLock rw_mutex;
    guint ctrl_bytes_sent;
} DataChannel;

typedef struct {
    GstElement *dtls_srtp_bin_rtp;
    GstElement *dtls_srtp_bin_rtcp;
    GstElement *send_output_bin;

    gboolean linked_rtcp;
} SendBinInfo;

typedef struct {
    GstElement *nice_src_rtp, *dtls_dec_rtp;
    gulong nice_src_block_rtp;

    GstElement *nice_src_rtcp, *dtls_dec_rtcp;
    gulong nice_src_block_rtcp;

    GstElement *nice_sink_rtp, *dtls_enc_rtp;
    GstElement *nice_sink_rtcp, *dtls_enc_rtcp;
} PendingSessionInfo;

struct _OwrTransportAgentPrivate {
    NiceAgent *nice_agent;
    gboolean ice_controlling_mode;

    GMutex sessions_lock;
    GHashTable *sessions;
    GHashTable *pending_sessions;
    guint agent_id;
    gchar *transport_bin_name;
    GstElement *pipeline, *transport_bin;
    GstElement *rtpbin;

    /* session_id -> struct SendBinInfo */
    GHashTable *send_bins;

    gboolean local_address_added;

    GHashTable *streams;
    GList *rtcp_list;
    GMutex rtcp_lock;

    guint local_min_port;
    guint local_max_port;

    guint deferred_helper_server_adds;
    GList *helper_server_infos;

    GHashTable *data_channels;
    GRWLock data_channels_rw_mutex;
    gboolean data_session_added, data_session_established;
    OwrMessageOriginBusSet *message_origin_bus_set;
};

typedef struct {
    OwrTransportAgent *transport_agent;
    guint session_id;
} AgentAndSessionIdPair;

typedef struct {
    OwrTransportAgent *transport_agent;
    guint session_id;
    gint rtx_pt;
    gboolean adapt;

    gushort highest_seq;
    guint16 ack_vec;
    guint8 n_loss;
    guint8 n_ecn;
    guint receive_wallclock;

    guint32 last_feedback_wallclock;
} ScreamRx;

#define GEN_HASH_KEY(seq, ssrc) (seq ^ ssrc)

static void owr_transport_agent_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_transport_agent_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);


static void add_helper_server_info(GResolver *resolver, GAsyncResult *result, GHashTable *info);
static void update_helper_servers(OwrTransportAgent *transport_agent, guint stream_id);
static gboolean add_session(GHashTable *args);
static guint get_stream_id(OwrTransportAgent *transport_agent, OwrSession *session);
static OwrSession * get_session(OwrTransportAgent *transport_agent, guint stream_id);
static void prepare_transport_bin_send_elements(OwrTransportAgent *transport_agent, guint stream_id, gboolean rtcp_mux, PendingSessionInfo *pending_session_info);
static void prepare_transport_bin_receive_elements(OwrTransportAgent *transport_agent, guint stream_id, gboolean rtcp_mux, PendingSessionInfo *pending_session_info);
static void prepare_transport_bin_data_receive_elements(OwrTransportAgent *transport_agent,
    guint stream_id);
static void prepare_transport_bin_data_send_elements(OwrTransportAgent *transport_agent,
    guint stream_id);
static void set_send_ssrc_and_cname(OwrTransportAgent *agent, OwrMediaSession *media_session);
static void on_new_candidate(NiceAgent *nice_agent, NiceCandidate *nice_candidate, OwrTransportAgent *transport_agent);
static void on_candidate_gathering_done(NiceAgent *nice_agent, guint stream_id, OwrTransportAgent *transport_agent);
static void on_component_state_changed(NiceAgent *nice_agent, guint stream_id, guint component_id, OwrIceState state, OwrTransportAgent *transport_agent);
static void handle_new_send_payload(OwrTransportAgent *transport_agent, OwrMediaSession *media_session, OwrPayload * payload);
static void on_new_remote_candidate(OwrTransportAgent *transport_agent, gboolean forced, OwrSession *session);
static void on_local_candidate_change(OwrTransportAgent *transport_agent, OwrCandidate *candidate, OwrSession *session);

static void on_transport_bin_pad_added(GstElement *transport_bin, GstPad *new_pad, OwrTransportAgent *transport_agent);
static void on_rtpbin_pad_added(GstElement *rtpbin, GstPad *new_pad, OwrTransportAgent *agent);
static void setup_video_receive_elements(GstPad *new_pad, guint32 session_id, OwrPayload *payload, OwrTransportAgent *transport_agent);
static void setup_audio_receive_elements(GstPad *new_pad, guint32 session_id, OwrPayload *payload, OwrTransportAgent *transport_agent);
static GstCaps * on_rtpbin_request_pt_map(GstElement *rtpbin, guint session_id, guint pt, OwrTransportAgent *agent);
static GstElement * on_rtpbin_request_aux_sender(GstElement *rtpbin, guint session_id, OwrTransportAgent *transport_agent);
static GstElement * on_rtpbin_request_aux_receiver(GstElement *rtpbin, guint session_id, OwrTransportAgent *transport_agent);
static void on_dtls_enc_key_set(GstElement *dtls_srtp_enc, AgentAndSessionIdPair *data);
static void on_new_selected_pair(NiceAgent *nice_agent,
    guint stream_id, guint component_id,
    NiceCandidate *lcandidate, NiceCandidate *rcandidate,
    OwrTransportAgent *transport_agent);

static gboolean on_sending_rtcp(GObject *session, GstBuffer *buffer, gboolean early, OwrTransportAgent *agent);
static void on_receiving_rtcp(GObject *session, GstBuffer *buffer, OwrTransportAgent *agent);
static void on_feedback_rtcp(GObject *session, guint type, guint fbtype, guint sender_ssrc, guint media_ssrc, GstBuffer *fci, OwrTransportAgent *transport_agent);
static GstPadProbeReturn probe_save_ts(GstPad *srcpad, GstPadProbeInfo *info, void *user_data);
static GstPadProbeReturn probe_rtp_info(GstPad *srcpad, GstPadProbeInfo *info, ScreamRx *scream_rx);
static void on_ssrc_active(GstElement *rtpbin, guint session_id, guint ssrc, OwrTransportAgent *transport_agent);
static void on_new_jitterbuffer(GstElement *rtpbin, GstElement *jitterbuffer, guint session_id, guint ssrc, OwrTransportAgent *transport_agent);
static void prepare_rtcp_stats(OwrMediaSession *media_session, GObject *rtp_source);

static void data_channel_free(DataChannel *data_channel);
static gboolean create_datachannel(OwrTransportAgent *transport_agent, guint32 session_id,
    OwrDataChannel *data_channel);
static void sctpdec_pad_added(GstElement *sctpdec, GstPad *sctpdec_srcpad,
    OwrTransportAgent *transport_agent);
static void sctpdec_pad_removed(GstElement *sctpdec, GstPad *sctpdec_srcpad,
    OwrTransportAgent *transport_agent);
static void on_sctp_association_established(GstElement *sctpenc, gboolean established,
    OwrTransportAgent *transport_agent);
static GstFlowReturn new_data_callback(GstAppSink *appsink, OwrTransportAgent *transport_agent);
static gboolean create_datachannel_appsrc(OwrTransportAgent *transport_agent,
    OwrDataChannel *data_channel);
static gboolean is_valid_sctp_stream_id(OwrTransportAgent *transport_agent, guint32 session_id,
    guint16 sctp_stream_id, gboolean remotly_initiated);
static guint8 * create_datachannel_open_request(OwrDataChannelChannelType channel_type,
    guint32 reliability_param, guint16 priority, const gchar *label, guint16 label_len,
    const gchar *protocol, guint16 protocol_len, guint32 *buf_size);
static guint8 * create_datachannel_ack(guint32 *buf_size);
static void handle_data_channel_control_message(OwrTransportAgent *transport_agent, guint8 *data,
    guint32 size, guint16 sctp_stream_id);
static void handle_data_channel_open_request(OwrTransportAgent *transport_agent, guint8 *data,
    guint32 size, guint16 sctp_stream_id);
static gboolean emit_data_channel_requested(GHashTable *args);
static void handle_data_channel_ack(OwrTransportAgent *transport_agent, guint8 *data, guint32 size,
    guint16 sctp_stream_id);
static void handle_data_channel_message(OwrTransportAgent *transport_agent, guint8 *data,
    guint32 size, guint16 sctp_stream_id, gboolean is_binary);
static gboolean emit_incoming_data(GHashTable *args);
static guint64 on_datachannel_request_bytes_sent(OwrTransportAgent *transport_agent,
    OwrDataChannel *data_channel);
static void on_datachannel_close(OwrTransportAgent *transport_agent, OwrDataChannel *data_channel);
static gboolean is_same_session(gpointer stream_id_p, OwrSession *session1, OwrSession *session2);
static void on_new_datachannel(OwrTransportAgent *transport_agent, OwrDataChannel *data_channel,
    OwrDataSession *data_session);
static void complete_data_channel_and_ack(OwrTransportAgent *transport_agent,
    OwrDataChannel *data_channel);
static void on_datachannel_send(OwrTransportAgent *transport_agent, guint8 *data, guint len,
    gboolean is_binary, OwrDataChannel *data_channel);
static void maybe_close_data_channel(OwrTransportAgent *transport_agent, DataChannel *data_channel_info);

static gboolean on_payload_adaptation_request(GstElement *screamqueue, guint pt,
    OwrMediaSession *media_session);

static void owr_transport_agent_finalize(GObject *object)
{
    OwrTransportAgent *transport_agent = NULL;
    OwrTransportAgentPrivate *priv = NULL;
    OwrSession *session = NULL;
    GList *sessions_list = NULL, *item = NULL;

    g_return_if_fail(_owr_is_initialized());

    transport_agent = OWR_TRANSPORT_AGENT(object);
    priv = transport_agent->priv;

    gst_element_set_state(priv->pipeline, GST_STATE_NULL);
    gst_object_unref(priv->pipeline);

    sessions_list = g_hash_table_get_values(priv->sessions);
    for (item = sessions_list; item; item = item->next) {
        session = item->data;
        if (OWR_IS_MEDIA_SESSION(session))
            _owr_media_session_clear_closures(OWR_MEDIA_SESSION(session));
        else if (OWR_IS_DATA_SESSION(session))
            _owr_data_session_clear_closures(OWR_DATA_SESSION(session));
    }
    g_list_free(sessions_list);

    g_hash_table_destroy(priv->sessions);
    g_hash_table_destroy(priv->pending_sessions);
    g_mutex_clear(&priv->sessions_lock);

    g_hash_table_destroy(priv->data_channels);
    g_rw_lock_clear(&priv->data_channels_rw_mutex);

    g_object_unref(priv->nice_agent);

    g_free(priv->transport_bin_name);

    for (item = priv->helper_server_infos; item; item = item->next) {
        g_free(g_hash_table_lookup(item->data, "address"));
        g_free(g_hash_table_lookup(item->data, "username"));
        g_free(g_hash_table_lookup(item->data, "password"));
        g_hash_table_destroy(item->data);
    }
    g_list_free(priv->helper_server_infos);

    g_hash_table_destroy(priv->send_bins);

    owr_message_origin_bus_set_free(priv->message_origin_bus_set);
    priv->message_origin_bus_set = NULL;
    g_list_free_full(priv->rtcp_list, (GDestroyNotify)g_hash_table_unref);
    g_mutex_clear(&priv->rtcp_lock);

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

static gpointer owr_transport_agent_get_bus_set(OwrMessageOrigin *origin)
{
    return OWR_TRANSPORT_AGENT(origin)->priv->message_origin_bus_set;
}

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface)
{
    interface->get_bus_set = owr_transport_agent_get_bus_set;
}

/* FIXME: Copy from owr/orw.c without any error handling whatsoever */
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer user_data)
{
    gboolean ret, is_warning = FALSE;
    GstStateChangeReturn change_status;
    gchar *message_type, *debug;
    GError *error;
    OwrTransportAgent *transport_agent = user_data;
    GstElement *pipeline = transport_agent->priv->pipeline;

    g_return_val_if_fail(GST_IS_BUS(bus), TRUE);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_LATENCY:
        ret = gst_bin_recalculate_latency(GST_BIN(pipeline));
        g_warn_if_fail(ret);
        break;

    case GST_MESSAGE_CLOCK_LOST:
        change_status = gst_element_set_state(pipeline, GST_STATE_PAUSED);
        g_warn_if_fail(change_status != GST_STATE_CHANGE_FAILURE);
        change_status = gst_element_set_state(pipeline, GST_STATE_PLAYING);
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

        if (!is_warning) {
            OWR_POST_ERROR(transport_agent, PROCESSING_ERROR, NULL);
        }

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
    GSource *bus_source;
    gchar *pipeline_name;

    transport_agent->priv = priv = OWR_TRANSPORT_AGENT_GET_PRIVATE(transport_agent);

    priv->ice_controlling_mode = DEFAULT_ICE_CONTROLLING_MODE;
    priv->agent_id = next_transport_agent_id++;
    priv->nice_agent = NULL;

    priv->sessions = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_object_unref);
    priv->pending_sessions = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    g_mutex_init(&priv->sessions_lock);

    priv->rtcp_list = NULL;
    g_mutex_init(&transport_agent->priv->rtcp_lock);

    g_return_if_fail(_owr_is_initialized());

    priv->nice_agent = nice_agent_new(_owr_get_main_context(), NICE_COMPATIBILITY_RFC5245);
    g_object_bind_property(transport_agent, "ice-controlling-mode", priv->nice_agent,
        "controlling-mode", G_BINDING_SYNC_CREATE);
    g_signal_connect(G_OBJECT(priv->nice_agent), "new-candidate-full",
        G_CALLBACK(on_new_candidate), transport_agent);
    g_signal_connect(G_OBJECT(priv->nice_agent), "candidate-gathering-done",
        G_CALLBACK(on_candidate_gathering_done), transport_agent);
    g_signal_connect(G_OBJECT(priv->nice_agent), "component-state-changed",
        G_CALLBACK(on_component_state_changed), transport_agent);
    g_signal_connect(G_OBJECT(priv->nice_agent), "new-selected-pair-full",
        G_CALLBACK(on_new_selected_pair), transport_agent);

    pipeline_name = g_strdup_printf("transport-agent-%u", priv->agent_id);
    priv->pipeline = gst_pipeline_new(pipeline_name);
    gst_pipeline_use_clock(GST_PIPELINE(priv->pipeline), gst_system_clock_obtain());
    gst_element_set_base_time(priv->pipeline, _owr_get_base_time());
    gst_element_set_start_time(priv->pipeline, GST_CLOCK_TIME_NONE);
    g_free(pipeline_name);

#ifdef OWR_DEBUG
    g_signal_connect(priv->pipeline, "deep-notify", G_CALLBACK(_owr_deep_notify), NULL);
#endif

    bus = gst_pipeline_get_bus(GST_PIPELINE(priv->pipeline));
    bus_source = gst_bus_create_watch(bus);
    g_source_set_callback(bus_source, (GSourceFunc) bus_call, transport_agent, NULL);
    g_source_attach(bus_source, _owr_get_main_context());
    g_source_unref(bus_source);

    priv->transport_bin_name = g_strdup_printf("transport_bin_%u", priv->agent_id);
    priv->transport_bin = gst_bin_new(priv->transport_bin_name);
    priv->rtpbin = gst_element_factory_make("rtpbin", "rtpbin");
    g_object_set(priv->rtpbin, "do-lost", TRUE, NULL);

    /* NOTE: We hard-code the savpf here as it is required for WebRTC */
    gst_util_set_object_arg(G_OBJECT(priv->rtpbin), "rtp-profile", "savpf");

    g_signal_connect(priv->rtpbin, "pad-added", G_CALLBACK(on_rtpbin_pad_added), transport_agent);
    g_signal_connect(priv->rtpbin, "request-pt-map", G_CALLBACK(on_rtpbin_request_pt_map), transport_agent);
    g_signal_connect(priv->rtpbin, "request-aux-sender", G_CALLBACK(on_rtpbin_request_aux_sender), transport_agent);
    g_signal_connect(priv->rtpbin, "request-aux-receiver", G_CALLBACK(on_rtpbin_request_aux_receiver), transport_agent);
    g_signal_connect(priv->rtpbin, "on-ssrc-active", G_CALLBACK(on_ssrc_active), transport_agent);
    g_signal_connect(priv->rtpbin, "new-jitterbuffer", G_CALLBACK(on_new_jitterbuffer), transport_agent);

    g_signal_connect(priv->transport_bin, "pad-added", G_CALLBACK(on_transport_bin_pad_added), transport_agent);

    gst_bin_add(GST_BIN(priv->transport_bin), priv->rtpbin);
    gst_bin_add(GST_BIN(priv->pipeline), priv->transport_bin);

    priv->local_address_added = FALSE;
    priv->local_min_port = 0;
    priv->local_max_port = 0;

    priv->deferred_helper_server_adds = 0;
    priv->helper_server_infos = NULL;

    priv->data_channels = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)data_channel_free);
    g_rw_lock_init(&priv->data_channels_rw_mutex);
    priv->data_session_added = FALSE;
    priv->data_session_established = FALSE;

    priv->send_bins = g_hash_table_new_full(NULL, NULL, NULL, g_free);

    priv->message_origin_bus_set = owr_message_origin_bus_set_new();
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

    transport_agent->priv->deferred_helper_server_adds++;

    helper_server_info = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(transport_agent));
    g_hash_table_insert(helper_server_info, "transport_agent", transport_agent);
    g_hash_table_insert(helper_server_info, "type", GUINT_TO_POINTER(type));
    g_hash_table_insert(helper_server_info, "port", GUINT_TO_POINTER(port));
    if (username)
        g_hash_table_insert(helper_server_info, "username", g_strdup(username));
    if (password)
        g_hash_table_insert(helper_server_info, "password", g_strdup(password));

    g_object_ref(transport_agent);
    resolver = g_resolver_get_default();
    g_main_context_push_thread_default(_owr_get_main_context());
    g_resolver_lookup_by_name_async(resolver, address, NULL,
        (GAsyncReadyCallback)add_helper_server_info, helper_server_info);
    g_main_context_pop_thread_default(_owr_get_main_context());
    g_object_unref(resolver);
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
    else
        transport_agent->priv->local_address_added = TRUE;
}

void owr_transport_agent_set_local_port_range(OwrTransportAgent *transport_agent, guint min_port, guint max_port)
{
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_return_if_fail(max_port < 65536);
    g_return_if_fail(min_port <= max_port);

    transport_agent->priv->local_min_port = min_port;
    transport_agent->priv->local_max_port = max_port;
}

/**
 * owr_transport_agent_add_session:
 * @agent:
 * @session: (transfer full):
 */
void owr_transport_agent_add_session(OwrTransportAgent *agent, OwrSession *session)
{
    GHashTable *args;

    g_return_if_fail(agent);
    g_return_if_fail(OWR_IS_MEDIA_SESSION(session) || OWR_IS_DATA_SESSION(session));

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(agent));
    g_hash_table_insert(args, "transport_agent", agent);
    g_hash_table_insert(args, "session", session);

    g_object_ref(agent);

    _owr_schedule_with_hash_table((GSourceFunc)add_session, args);

}


/* Internal functions */

static void add_helper_server_info(GResolver *resolver, GAsyncResult *result, GHashTable *info)
{
    OwrTransportAgent *transport_agent;
    OwrTransportAgentPrivate *priv;
    GList *stream_ids, *item, *address_list;
    GHashTable *stats_table;
    OwrMessageOrigin *message_origin;
    GValue *value;
    guint stream_id;
    GError *error = NULL;

    transport_agent = OWR_TRANSPORT_AGENT(g_hash_table_lookup(info, "transport_agent"));
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_hash_table_remove(info, "transport_agent");

    stats_table = g_hash_table_lookup(info, "__data");
    g_warn_if_fail(stats_table);
    g_hash_table_remove(info, "__data");
    message_origin = OWR_MESSAGE_ORIGIN(g_hash_table_lookup(info, "__origin"));
    g_warn_if_fail(message_origin);
    g_hash_table_remove(info, "__origin");
    if (stats_table && message_origin) {
        value = _owr_value_table_add(stats_table, "call_time", G_TYPE_INT64);
        g_value_set_int64(value, g_get_monotonic_time());
    } else if (message_origin) {
        g_object_unref(message_origin);
    }

    priv = transport_agent->priv;

    priv->deferred_helper_server_adds--;

    address_list = g_resolver_lookup_by_name_finish(resolver, result, &error);

    if (!address_list) {
        g_printerr("Failed to resolve helper server address: %s\n", error->message);
        g_error_free(error);
        g_free(g_hash_table_lookup(info, "username"));
        g_free(g_hash_table_lookup(info, "password"));
        g_hash_table_unref(info);
        info = NULL;
    } else {
        g_hash_table_insert(info, "address", g_inet_address_to_string(address_list->data));
        g_resolver_free_addresses(address_list);

        priv->helper_server_infos = g_list_append(priv->helper_server_infos, info);
    }

    g_mutex_lock(&priv->sessions_lock);
    stream_ids = g_hash_table_get_keys(priv->sessions);
    g_mutex_unlock(&priv->sessions_lock);
    for (item = stream_ids; item; item = item->next) {
        stream_id = GPOINTER_TO_UINT(item->data);
        if (info)
            update_helper_servers(transport_agent, stream_id);
        if (!priv->deferred_helper_server_adds)
            nice_agent_gather_candidates(priv->nice_agent, stream_id);
    }
    g_list_free(stream_ids);

    g_object_unref(transport_agent);

    if (stats_table && message_origin) {
        value = _owr_value_table_add(stats_table, "end_time", G_TYPE_INT64);
        g_value_set_int64(value, g_get_monotonic_time());
        OWR_POST_STATS(message_origin, SCHEDULE, stats_table);
        g_object_unref(message_origin);
    }
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
        case OWR_HELPER_SERVER_TYPE_TURN_TLS:
            nice_agent_set_relay_info(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTP,
                                      address, port, username, password, NICE_RELAY_TYPE_TURN_TLS);
            nice_agent_set_relay_info(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTCP,
                                      address, port, username, password, NICE_RELAY_TYPE_TURN_TLS);
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
    gst_object_unref(sinkpad);
    if (ret) {
        gst_element_post_message(pipeline,
            gst_message_new_latency(GST_OBJECT(pipeline)));
    }

    return ret;
}

static void handle_new_send_source(OwrTransportAgent *transport_agent,
    OwrMediaSession *media_session, OwrMediaSource * send_source, OwrPayload * send_payload)
{
    GstElement *transport_bin, *src;
    GstCaps *caps;
    OwrCodecType codec_type = OWR_CODEC_TYPE_NONE;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
    guint stream_id = 0;
    GstPad *srcpad;

    g_return_if_fail(transport_agent);
    g_return_if_fail(media_session);

    g_assert(send_source);
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
    gst_object_unref(srcpad);

    gst_element_sync_state_with_parent(src);
}

static void maybe_handle_new_send_source_with_payload(OwrTransportAgent *transport_agent,
    OwrMediaSession *media_session)
{
    OwrPayload *payload = NULL;
    OwrMediaSource *media_source = NULL;
    GHashTable *event_data;
    GValue *value;
    guint stream_id;
    gboolean pending;

    stream_id = get_stream_id(transport_agent, OWR_SESSION(media_session));
    g_return_if_fail(stream_id);

    g_mutex_lock(&transport_agent->priv->sessions_lock);
    pending = g_hash_table_lookup_extended(transport_agent->priv->pending_sessions, GUINT_TO_POINTER(stream_id), NULL, NULL);
    g_mutex_unlock(&transport_agent->priv->sessions_lock);

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));

    if (!pending &&
        (payload = _owr_media_session_get_send_payload(media_session)) &&
        (media_source = _owr_media_session_get_send_source(media_session))) {

        event_data = _owr_value_table_new();
        value = _owr_value_table_add(event_data, "start_time", G_TYPE_INT64);
        g_value_set_int64(value, g_get_monotonic_time());

        handle_new_send_payload(transport_agent, media_session, payload);
        handle_new_send_source(transport_agent, media_session, media_source, payload);

        value = _owr_value_table_add(event_data, "end_time", G_TYPE_INT64);
        g_value_set_int64(value, g_get_monotonic_time());
        OWR_POST_STATS(media_session, SEND_PIPELINE_ADDED, event_data);
    }

    if (payload)
        g_object_unref(payload);
    if (media_source)
        g_object_unref(media_source);
}

static void remove_existing_send_source_and_payload(OwrTransportAgent *transport_agent, OwrMediaSource *media_source,
    OwrMediaSession *media_session)
{
    guint stream_id;
    gchar *pad_name = NULL, *bin_name;
    GstPad *bin_src_pad, *sinkpad;
    GstElement *send_input_bin, *source_bin;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
    GHashTable *event_data;
    GValue *value;

    g_assert(media_source);

    event_data = _owr_value_table_new();
    value = _owr_value_table_add(event_data, "start_time", G_TYPE_INT64);
    g_value_set_int64(value, g_get_monotonic_time());

    /* Setting a new, different source but have one already */

    stream_id = get_stream_id(transport_agent, OWR_SESSION(media_session));

    /* Unlink the source bin */
    g_object_get(media_source, "media-type", &media_type, NULL);
    g_warn_if_fail(media_type != OWR_MEDIA_TYPE_UNKNOWN);
    if (media_type == OWR_MEDIA_TYPE_VIDEO)
        pad_name = g_strdup_printf("video_sink_%u_%u", OWR_CODEC_TYPE_NONE, stream_id);
    else
        pad_name = g_strdup_printf("audio_raw_sink_%u", stream_id);
    sinkpad = gst_element_get_static_pad(transport_agent->priv->transport_bin, pad_name);
    g_assert(sinkpad);
    g_free(pad_name);

    bin_src_pad = gst_pad_get_peer(sinkpad);
    g_assert(bin_src_pad);
    source_bin = GST_ELEMENT(gst_pad_get_parent(bin_src_pad));
    g_assert(source_bin);

    /* Shutting down will flush immediately */
    _owr_media_source_release_source(media_source, source_bin);
    gst_element_set_state(source_bin, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(transport_agent->priv->pipeline), source_bin);
    gst_object_unref(bin_src_pad);
    gst_object_unref(source_bin);

    /* Now the payload bin */
    bin_name = g_strdup_printf("send-input-bin-%u", stream_id);
    send_input_bin = gst_bin_get_by_name(GST_BIN(transport_agent->priv->transport_bin), bin_name);
    g_assert(send_input_bin);
    g_free(bin_name);
    gst_element_set_state(send_input_bin, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(transport_agent->priv->transport_bin), send_input_bin);
    gst_object_unref(send_input_bin);

    /* Remove the connecting ghostpad */
    gst_pad_set_active(sinkpad, FALSE);
    gst_element_remove_pad(transport_agent->priv->transport_bin, sinkpad);
    gst_object_unref(sinkpad);

    value = _owr_value_table_add(event_data, "end_time", G_TYPE_INT64);
    g_value_set_int64(value, g_get_monotonic_time());
    OWR_POST_STATS(media_session, SEND_PIPELINE_REMOVED, event_data);
}

static void on_new_send_payload(OwrTransportAgent *transport_agent,
    OwrPayload *old_payload, OwrPayload *new_payload,
    OwrMediaSession *media_session)
{
    g_assert(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_assert(OWR_IS_MEDIA_SESSION(media_session));
    g_assert(!new_payload || OWR_IS_PAYLOAD(new_payload));
    g_assert(!old_payload || OWR_IS_PAYLOAD(old_payload));

    if (old_payload && old_payload != new_payload) {
        OwrMediaSource *media_source = _owr_media_session_get_send_source(media_session);
        remove_existing_send_source_and_payload(transport_agent, media_source, media_session);
        g_object_unref(media_source);
    }

    if (new_payload && old_payload != new_payload)
        maybe_handle_new_send_source_with_payload(transport_agent, media_session);
}

static void on_new_send_source(OwrTransportAgent *transport_agent,
    OwrMediaSource *old_media_source, OwrMediaSource *new_media_source,
    OwrMediaSession *media_session)
{
    g_assert(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_assert(OWR_IS_MEDIA_SESSION(media_session));
    g_assert(!new_media_source || OWR_IS_MEDIA_SOURCE(new_media_source));

    g_assert(!old_media_source || OWR_IS_MEDIA_SOURCE(old_media_source));

    if (old_media_source && old_media_source != new_media_source)
        remove_existing_send_source_and_payload(transport_agent, old_media_source, media_session);

    if (new_media_source && old_media_source != new_media_source)
        maybe_handle_new_send_source_with_payload(transport_agent, media_session);
}

static gboolean add_session(GHashTable *args)
{
    OwrTransportAgent *transport_agent;
    OwrTransportAgentPrivate *priv;
    OwrSession *session;
    guint stream_id;
    gboolean rtcp_mux = TRUE;
    GObject *rtp_session;
    GstStateChangeReturn state_change_status;
    PendingSessionInfo *pending_session_info;
    guint port;

    g_return_val_if_fail(args, FALSE);

    transport_agent = g_hash_table_lookup(args, "transport_agent");
    session = OWR_SESSION(g_hash_table_lookup(args, "session"));

    g_return_val_if_fail(transport_agent, FALSE);
    g_return_val_if_fail(session, FALSE);

    priv = transport_agent->priv;
    g_mutex_lock(&priv->sessions_lock);
    if (g_hash_table_find(priv->sessions, (GHRFunc)is_same_session, session)) {
        g_warning("An already existing media session was added to the transport agent. Action aborted.");
        g_mutex_unlock(&priv->sessions_lock);
        goto end;
    }
    g_mutex_unlock(&priv->sessions_lock);

    if (OWR_IS_MEDIA_SESSION(session))
        g_object_get(OWR_MEDIA_SESSION(session), "rtcp-mux", &rtcp_mux, NULL);

    stream_id = nice_agent_add_stream(priv->nice_agent, rtcp_mux ? 1 : 2);
    if (!stream_id) {
        g_warning("Failed to add media session.");
        goto end;
    }

    g_mutex_lock(&priv->sessions_lock);
    g_hash_table_insert(priv->sessions, GUINT_TO_POINTER(stream_id), session);
    g_object_ref(session);
    g_mutex_unlock(&priv->sessions_lock);

    update_helper_servers(transport_agent, stream_id);

    _owr_session_set_on_remote_candidate(session,
        g_cclosure_new_object_swap(G_CALLBACK(on_new_remote_candidate), G_OBJECT(transport_agent)));
    _owr_session_set_on_local_candidate_change(session,
        g_cclosure_new_object_swap(G_CALLBACK(on_local_candidate_change), G_OBJECT(transport_agent)));

    if (OWR_IS_MEDIA_SESSION(session)) {
        guint send_ssrc = 0;
        gchar *cname = NULL;

        _owr_media_session_set_on_send_source(OWR_MEDIA_SESSION(session),
            g_cclosure_new_object_swap(G_CALLBACK(on_new_send_source), G_OBJECT(transport_agent)));

        _owr_media_session_set_on_send_payload(OWR_MEDIA_SESSION(session),
            g_cclosure_new_object_swap(G_CALLBACK(on_new_send_payload), G_OBJECT(transport_agent)));
        pending_session_info = g_new0(PendingSessionInfo, 1);
        prepare_transport_bin_receive_elements(transport_agent, stream_id, rtcp_mux, pending_session_info);
        prepare_transport_bin_send_elements(transport_agent, stream_id, rtcp_mux, pending_session_info);

        g_mutex_lock(&transport_agent->priv->sessions_lock);
        g_hash_table_insert(transport_agent->priv->pending_sessions, GUINT_TO_POINTER(stream_id), pending_session_info);
        g_mutex_unlock(&transport_agent->priv->sessions_lock);

        g_object_get(session, "send-ssrc", &send_ssrc, "cname", &cname, NULL);
        if (!send_ssrc || !cname)
            set_send_ssrc_and_cname(transport_agent, OWR_MEDIA_SESSION(session));
    } else if (OWR_IS_DATA_SESSION(session)) {
        _owr_data_session_set_on_datachannel_added(OWR_DATA_SESSION(session),
            g_cclosure_new_object_swap(G_CALLBACK(on_new_datachannel),
            G_OBJECT(transport_agent)));
        prepare_transport_bin_data_receive_elements(transport_agent, stream_id);
        prepare_transport_bin_data_send_elements(transport_agent, stream_id);
    }

    if (priv->local_max_port > 0) {
        nice_agent_set_port_range(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTP,
            priv->local_min_port, priv->local_max_port);
        if (!rtcp_mux) {
            nice_agent_set_port_range(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTCP,
                priv->local_min_port, priv->local_max_port);
        }
    }

    /* OwrSession port settings override OwrTransportAgent settings */
    if ((port = _owr_session_get_local_port(session, OWR_COMPONENT_TYPE_RTP))) {
        nice_agent_set_port_range(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTP,
            port, port);
    }
    if ((port = _owr_session_get_local_port(session, OWR_COMPONENT_TYPE_RTCP))) {
        nice_agent_set_port_range(priv->nice_agent, stream_id, NICE_COMPONENT_TYPE_RTCP,
            port, port);
    }

    if (!priv->local_address_added) {
        GList *item, *local_ips = nice_interfaces_get_local_ips(FALSE);

        for (item = local_ips; item; item = item->next) {
            gchar *local_ip = item->data;
            GInetAddress *inet_address = g_inet_address_new_from_string(local_ip);

            if (inet_address) {
                if (!g_inet_address_get_is_link_local(inet_address))
                    owr_transport_agent_add_local_address(transport_agent, local_ip);
                g_object_unref(inet_address);
            }
        }
        g_list_free_full(local_ips, g_free);
    }

    if (!priv->deferred_helper_server_adds)
        nice_agent_gather_candidates(priv->nice_agent, stream_id);

    if (OWR_IS_MEDIA_SESSION(session)) {
        /* stream_id is used as the rtpbin session id */
        g_signal_emit_by_name(priv->rtpbin, "get-internal-session", stream_id, &rtp_session);
        g_object_set(rtp_session, "rtcp-min-interval", GST_SECOND, "bandwidth", 0.0, "rtp-profile", GST_RTP_PROFILE_SAVPF, NULL);
/*
        g_object_set(rtp_session, "bandwidth", (gdouble)700000,
            "rtcp-fraction", (gdouble)100000,
            "rtcp-min-interval", (guint64)200000000, NULL);*/
        g_object_set_data(rtp_session, "session_id", GUINT_TO_POINTER(stream_id));
        g_signal_connect_after(rtp_session, "on-sending-rtcp", G_CALLBACK(on_sending_rtcp), transport_agent);
        g_signal_connect(rtp_session, "on-feedback-rtcp", G_CALLBACK(on_feedback_rtcp), transport_agent);
        g_signal_connect_after(rtp_session, "on-receiving-rtcp", G_CALLBACK(on_receiving_rtcp), NULL);
        g_object_unref(rtp_session);

        maybe_handle_new_send_source_with_payload(transport_agent, OWR_MEDIA_SESSION(session));
    }

    if (_owr_session_get_remote_candidates(session))
        on_new_remote_candidate(transport_agent, FALSE, session);
    if (_owr_session_get_forced_remote_candidates(session))
        on_new_remote_candidate(transport_agent, TRUE, session);

    state_change_status = gst_element_set_state(transport_agent->priv->pipeline, GST_STATE_PLAYING);
    g_warn_if_fail(state_change_status != GST_STATE_CHANGE_FAILURE);

end:
    g_object_unref(session);
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

    g_object_set(nice_element, "agent", transport_agent->priv->nice_agent,
        "stream", stream_id,
        "component", is_rtcp
        ? NICE_COMPONENT_TYPE_RTCP : NICE_COMPONENT_TYPE_RTP, NULL);

    if (is_sink) {
        g_object_set(nice_element, "enable-last-sample", FALSE, "async", FALSE, NULL);
        if (is_rtcp) {
            g_object_set(nice_element, "sync", FALSE, NULL);
        }
    } else if (!is_rtcp) {
        GstPad *nice_src_pad = NULL;

        nice_src_pad = gst_element_get_static_pad(nice_element, "src");
        gst_pad_add_probe(nice_src_pad, GST_PAD_PROBE_TYPE_BUFFER, probe_save_ts, NULL, NULL);
        gst_object_unref(nice_src_pad);
    }

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
    OwrSession *session;
    GstElement *dtls_srtp_bin = NULL;
    gchar *element_name, *connection_id;
    gchar *cert, *key, *cert_key;
    gboolean added_ok;

    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent), NULL);
    priv = transport_agent->priv;

    element_name = g_strdup_printf("dtls_srtp_%s_%s_%u", is_rtcp ? "rtcp" : "rtp",
        is_encoder ? "encoder" : "decoder", stream_id);

    dtls_srtp_bin = gst_element_factory_make(is_encoder ? "dtlssrtpenc" : "dtlssrtpdec",
        element_name);
    connection_id = g_strdup_printf("%s_%u_%u", is_rtcp ? "rtcp" : "rtp",
        priv->agent_id, stream_id);
    g_object_set(dtls_srtp_bin, "connection-id", connection_id, NULL);
    g_free(connection_id);

    session = get_session(transport_agent, stream_id);

    if (!is_encoder) {
        g_object_get(session, "dtls-certificate", &cert, NULL);
        g_object_get(session, "dtls-key", &key, NULL);

        if (!g_strcmp0(cert, "(auto)")) {
            g_object_get(dtls_srtp_bin, "pem", &cert, NULL);
            g_object_set(session, "dtls-certificate", cert, NULL);
            g_object_set(session, "dtls-key", NULL, NULL);
            GST_DEBUG("used auto generated cert stuff.. THIS SHOULD NOT HAPPEN!!!!");
        } else {
            cert_key = (cert && key) ? g_strdup_printf("%s%s", cert, key) : NULL;
            g_object_set(dtls_srtp_bin, "pem", cert_key, NULL);
            g_free(cert_key);
        }
        g_signal_connect_object(dtls_srtp_bin, "notify::peer-pem",
            G_CALLBACK(on_dtls_peer_certificate), session, 0);
        g_free(cert);
        g_free(key);
    } else {
        gboolean dtls_client_mode;
        g_object_get(session, "dtls-client-mode", &dtls_client_mode, NULL);
        g_object_set(dtls_srtp_bin, "is-client", dtls_client_mode, NULL);
    }

    if (OWR_IS_MEDIA_SESSION(session)) {
        g_signal_connect_object(OWR_MEDIA_SESSION(session), is_encoder ? "notify::outgoing-srtp-key"
            : "notify::incoming-srtp-key", G_CALLBACK(set_srtp_key), dtls_srtp_bin, 0);
        g_signal_connect_object(OWR_MEDIA_SESSION(session), "notify::dtls-certificate",
            G_CALLBACK(maybe_disable_dtls), dtls_srtp_bin, 0);
        maybe_disable_dtls(OWR_MEDIA_SESSION(session), NULL, dtls_srtp_bin);

        /* Keep this in locked state until the nice streams are connected, only
         * then it makes sense to start the DTLS handshake
         */
        gst_element_set_locked_state(dtls_srtp_bin, TRUE);
    }

    added_ok = gst_bin_add(GST_BIN(bin), dtls_srtp_bin);
    g_warn_if_fail(added_ok);

    g_free(element_name);
    g_object_unref(session);

    return dtls_srtp_bin;
}

static GstPad *ghost_pad_and_add_to_bin(GstPad *pad, GstElement *bin, const gchar *pad_name)
{
    GstPad *ghost_pad;

    ghost_pad = gst_ghost_pad_new(pad_name, pad);

    gst_pad_set_active(ghost_pad, TRUE);

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

static gboolean emit_bitrate_change(GHashTable *args)
{
    OwrMediaSession *session;
    OwrPayload *payload;
    guint bitrate;

    session = g_hash_table_lookup(args, "session");
    bitrate = GPOINTER_TO_UINT(g_hash_table_lookup(args, "bitrate"));

    payload = _owr_media_session_get_send_payload(session);

    if (payload) {
        guint old_bitrate = 0;
        g_object_get(payload, "bitrate", &old_bitrate, NULL);
        if (bitrate != old_bitrate) {
            GST_INFO("Updating bitrate to %u from %u", bitrate, old_bitrate);
            g_object_set(payload, "bitrate", bitrate, NULL);
        }
        g_object_unref(payload);
    } else
        GST_WARNING("No send payload set for media session");

    g_object_unref(session);
    g_hash_table_destroy(args);

    return FALSE;
}

static void on_bitrate_change(GstElement *scream_queue, guint bitrate, guint ssrc, guint pt,
    OwrMediaSession *session)
{
    GHashTable *args;
    OWR_UNUSED(scream_queue);
    OWR_UNUSED(ssrc);
    OWR_UNUSED(pt);

    g_return_if_fail(session);

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(session));

    g_hash_table_insert(args, "session", g_object_ref(session));
    g_hash_table_insert(args, "bitrate", GUINT_TO_POINTER(bitrate));

    _owr_schedule_with_hash_table((GSourceFunc)emit_bitrate_change, args);
}

static void link_rtpbin_to_send_output_bin(OwrTransportAgent *transport_agent, guint stream_id, gboolean rtp, gboolean rtcp)
{
    gchar *rtpbin_pad_name, *dtls_srtp_pad_name;
    gchar *output_selector_name;
    gboolean linked_ok;
    GstPad *sink_pad, *src_pad;
    GstElement *output_selector;
    GstElement *dtls_srtp_bin_rtp;
    GstElement *dtls_srtp_bin_rtcp;
    GstElement *send_output_bin;
    OwrMediaSession *media_session;
    SendBinInfo *send_bin_info;
    GstElement *scream_queue;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    send_bin_info = g_hash_table_lookup(transport_agent->priv->send_bins, GINT_TO_POINTER(stream_id));
    g_return_if_fail(send_bin_info);

    dtls_srtp_bin_rtp = send_bin_info->dtls_srtp_bin_rtp;
    dtls_srtp_bin_rtcp = send_bin_info->dtls_srtp_bin_rtcp;
    send_output_bin = send_bin_info->send_output_bin;

    g_return_if_fail(GST_IS_ELEMENT(dtls_srtp_bin_rtp));
    g_return_if_fail(!dtls_srtp_bin_rtcp || GST_IS_ELEMENT(dtls_srtp_bin_rtcp));

    media_session = OWR_MEDIA_SESSION(get_session(transport_agent, stream_id));

    scream_queue = gst_bin_get_by_name(GST_BIN(send_output_bin), "screamqueue");
    g_assert(scream_queue);
    g_signal_connect_object(scream_queue, "on-bitrate-change", G_CALLBACK(on_bitrate_change),
        media_session, 0);
        /* TODO: Move connect to prepare_transport_bin_send_elements */

    /* RTP */
    if (rtp) {
        rtpbin_pad_name = g_strdup_printf("send_rtp_src_%u", stream_id);
        dtls_srtp_pad_name = g_strdup_printf("rtp_sink_%u", stream_id);

        sink_pad = gst_element_get_static_pad(scream_queue, "sink");
        g_assert(GST_IS_PAD(sink_pad));
        ghost_pad_and_add_to_bin(sink_pad, send_output_bin, dtls_srtp_pad_name);
        gst_object_unref(sink_pad);

        linked_ok = gst_element_link_pads(transport_agent->priv->rtpbin, rtpbin_pad_name,
            send_output_bin, dtls_srtp_pad_name);
        g_warn_if_fail(linked_ok);

        g_free(rtpbin_pad_name);
        g_free(dtls_srtp_pad_name);
    }

    /* RTCP */
    if (rtcp && !send_bin_info->linked_rtcp) {
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
            /* NOTE: at this point when doing standalone RTCP, the RTCP bin is unblocked and is in the
             * correct state, but might not have finished the DTLS handshake yet. See
             * on_new_selected_pair */
            sink_pad = gst_element_get_request_pad(dtls_srtp_bin_rtcp, dtls_srtp_pad_name);
            g_assert(GST_IS_PAD(sink_pad));
            src_pad = gst_element_get_request_pad(output_selector, "src_%u");
            g_assert(GST_IS_PAD(src_pad));
            linked_ok = gst_pad_link(src_pad, sink_pad) == GST_PAD_LINK_OK;
            g_warn_if_fail(linked_ok);
            g_object_set(output_selector, "active-pad", src_pad, NULL);
            gst_object_unref(src_pad);
            gst_object_unref(sink_pad);
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
        g_object_unref(media_session);

        send_bin_info->linked_rtcp = TRUE;
    }
}

static void prepare_transport_bin_send_elements(OwrTransportAgent *transport_agent,
    guint stream_id, gboolean rtcp_mux, PendingSessionInfo *pending_session_info)
{
    GstElement *nice_element, *dtls_srtp_bin_rtp, *dtls_srtp_bin_rtcp = NULL;
    GstElement *scream_queue = NULL;
    gboolean linked_ok, synced_ok;
    GstElement *send_output_bin;
    SendBinInfo *send_bin_info;
    gchar *bin_name, *dtls_srtp_pad_name;
    OwrMediaSession *media_session;
    AgentAndSessionIdPair *agent_and_session_id_pair;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    bin_name = g_strdup_printf("send-output-bin-%u", stream_id);
    send_output_bin = gst_bin_new(bin_name);
    g_free(bin_name);

    if (!gst_bin_add(GST_BIN(transport_agent->priv->transport_bin), send_output_bin)) {
        GST_ERROR("Failed to add send-output-bin-%u to parent bin", stream_id);
        return;
    }

    if (!gst_element_sync_state_with_parent(send_output_bin)) {
        GST_ERROR("Failed to sync send-output-bin-%u state with parent bin", stream_id);
        return;
    }

    media_session = OWR_MEDIA_SESSION(get_session(transport_agent, stream_id));
    scream_queue = gst_element_factory_make("screamqueue", "screamqueue");
    g_assert(scream_queue);
    g_object_set(scream_queue, "scream-controller-id", transport_agent->priv->agent_id, NULL);
    g_signal_connect(scream_queue, "on-payload-adaptation-request",
        (GCallback)on_payload_adaptation_request, media_session);
    gst_bin_add(GST_BIN(send_output_bin), scream_queue);

    pending_session_info->nice_sink_rtp = nice_element = add_nice_element(transport_agent, stream_id, TRUE, FALSE, send_output_bin);
    pending_session_info->dtls_enc_rtp = dtls_srtp_bin_rtp = add_dtls_srtp_bin(transport_agent, stream_id, TRUE, FALSE, send_output_bin);
    linked_ok = gst_element_link(dtls_srtp_bin_rtp, nice_element);
    g_warn_if_fail(linked_ok);

    agent_and_session_id_pair = g_new0(AgentAndSessionIdPair, 1);
    agent_and_session_id_pair->transport_agent = transport_agent;
    agent_and_session_id_pair->session_id = stream_id;
    g_signal_connect_data(dtls_srtp_bin_rtp, "on-key-set", G_CALLBACK(on_dtls_enc_key_set), agent_and_session_id_pair, (GClosureNotify) g_free, 0);

    dtls_srtp_pad_name = g_strdup_printf("rtp_sink_%u", stream_id);
    linked_ok = gst_element_link_pads(scream_queue, "src", dtls_srtp_bin_rtp, dtls_srtp_pad_name);
    g_free(dtls_srtp_pad_name);
    g_warn_if_fail(linked_ok);

    synced_ok = gst_element_sync_state_with_parent(nice_element);
    g_warn_if_fail(synced_ok);
    synced_ok = gst_element_sync_state_with_parent(scream_queue);
    g_warn_if_fail(synced_ok);

    if (!rtcp_mux) {
        pending_session_info->nice_sink_rtcp = nice_element = add_nice_element(transport_agent, stream_id, TRUE, TRUE, send_output_bin);
        pending_session_info->dtls_enc_rtcp = dtls_srtp_bin_rtcp = add_dtls_srtp_bin(transport_agent, stream_id, TRUE, TRUE, send_output_bin);
        linked_ok = gst_element_link(dtls_srtp_bin_rtcp, nice_element);
        g_warn_if_fail(linked_ok);

        agent_and_session_id_pair = g_new0(AgentAndSessionIdPair, 1);
        agent_and_session_id_pair->transport_agent = transport_agent;
        agent_and_session_id_pair->session_id = stream_id;
        g_signal_connect_data(dtls_srtp_bin_rtp, "on-key-set", G_CALLBACK(on_dtls_enc_key_set), agent_and_session_id_pair, (GClosureNotify) g_free, 0);

        synced_ok = gst_element_sync_state_with_parent(nice_element);
        g_warn_if_fail(synced_ok);
    }

    send_bin_info = g_new(SendBinInfo, 1);
    send_bin_info->dtls_srtp_bin_rtp = dtls_srtp_bin_rtp;
    send_bin_info->dtls_srtp_bin_rtcp = dtls_srtp_bin_rtcp;
    send_bin_info->send_output_bin = send_output_bin;
    send_bin_info->linked_rtcp = FALSE;

    g_hash_table_insert(transport_agent->priv->send_bins, GINT_TO_POINTER(stream_id), send_bin_info);
}

static GstPadProbeReturn nice_src_pad_block(GstPad *pad, GstPadProbeInfo *info, AgentAndSessionIdPair *data)
{
    OWR_UNUSED(pad);
    OWR_UNUSED(info);
    OWR_UNUSED(data);

    return GST_PAD_PROBE_OK;
}

static void prepare_transport_bin_receive_elements(OwrTransportAgent *transport_agent,
    guint stream_id, gboolean rtcp_mux, PendingSessionInfo *pending_session_info)
{
    GstElement *nice_element, *dtls_srtp_bin, *funnel;
    GstPad *rtp_src_pad, *rtcp_src_pad, *rtp_sink_pad;
    GstPad *nice_src_pad;
    gchar *rtpbin_pad_name;
    gboolean linked_ok, synced_ok;
    GstElement *receive_input_bin;
    ScreamRx *scream_rx;
#ifdef TEST_RTX
    GstElement *identity;
#endif
    gchar *bin_name;
    AgentAndSessionIdPair *agent_and_session_id_pair;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    bin_name = g_strdup_printf("receive-input-bin-%u", stream_id);
    receive_input_bin = gst_bin_new(bin_name);
    g_free(bin_name);

    if (!gst_bin_add(GST_BIN(transport_agent->priv->transport_bin), receive_input_bin)) {
        GST_ERROR("Failed to add receive-input-bin-%u to parent bin", stream_id);
        return;
    }

    if (!gst_element_sync_state_with_parent(receive_input_bin)) {
        GST_ERROR("Failed to sync receive-input-bin-%u state with parent bin", stream_id);
        return;
    }

    pending_session_info->nice_src_rtp = nice_element = add_nice_element(transport_agent, stream_id, FALSE, FALSE, receive_input_bin);
    pending_session_info->dtls_dec_rtp = dtls_srtp_bin = add_dtls_srtp_bin(transport_agent, stream_id, FALSE, FALSE, receive_input_bin);

    agent_and_session_id_pair = g_new0(AgentAndSessionIdPair, 1);
    agent_and_session_id_pair->transport_agent = transport_agent;
    agent_and_session_id_pair->session_id = stream_id;
    nice_src_pad = gst_element_get_static_pad(nice_element, "src");
    pending_session_info->nice_src_block_rtp = gst_pad_add_probe(nice_src_pad, GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST, (GstPadProbeCallback)nice_src_pad_block, agent_and_session_id_pair, (GDestroyNotify) g_free);
    gst_object_unref(nice_src_pad);

    synced_ok = gst_element_sync_state_with_parent(nice_element);
    g_warn_if_fail(synced_ok);

    rtp_src_pad = gst_element_get_static_pad(dtls_srtp_bin, "rtp_src");
    ghost_pad_and_add_to_bin(rtp_src_pad, receive_input_bin, "rtp_src");
    gst_object_unref(rtp_src_pad);

    rtpbin_pad_name = g_strdup_printf("recv_rtp_sink_%u", stream_id);

#ifndef TEST_RTX
    linked_ok = gst_element_link_pads(receive_input_bin, "rtp_src", transport_agent->priv->rtpbin,
        rtpbin_pad_name);
#else
    identity = gst_element_factory_make("identity", NULL);
    g_object_set(identity, "drop-probability", 0.01, NULL);
    gst_bin_add(GST_BIN(transport_agent->priv->transport_bin), identity);
    gst_element_link_pads(receive_input_bin, "rtp_src", identity, "sink");
    linked_ok = gst_element_link_pads(identity, "src", transport_agent->priv->rtpbin,
        rtpbin_pad_name);
    gst_element_sync_state_with_parent(identity);
#endif
    g_warn_if_fail(linked_ok);
    g_free(rtpbin_pad_name);

    if (!rtcp_mux) {
        funnel = gst_element_factory_make("funnel", NULL);
        gst_bin_add(GST_BIN(receive_input_bin), funnel);

        linked_ok = gst_element_link_pads(dtls_srtp_bin, "rtcp_src", funnel, "sink_0");
        g_warn_if_fail(linked_ok);

        pending_session_info->nice_src_rtcp = nice_element = add_nice_element(transport_agent, stream_id, FALSE, TRUE, receive_input_bin);
        pending_session_info->dtls_dec_rtcp = dtls_srtp_bin = add_dtls_srtp_bin(transport_agent, stream_id, FALSE, TRUE, receive_input_bin);

        agent_and_session_id_pair = g_new0(AgentAndSessionIdPair, 1);
        agent_and_session_id_pair->transport_agent = transport_agent;
        agent_and_session_id_pair->session_id = stream_id;
        nice_src_pad = gst_element_get_static_pad(nice_element, "src");
        pending_session_info->nice_src_block_rtcp = gst_pad_add_probe(nice_src_pad, GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST, (GstPadProbeCallback)nice_src_pad_block, agent_and_session_id_pair, (GDestroyNotify) g_free);
        gst_object_unref(nice_src_pad);

        linked_ok = gst_element_link_pads(dtls_srtp_bin, "rtcp_src", funnel, "sink_1");
        g_warn_if_fail(linked_ok);

        rtcp_src_pad = gst_element_get_static_pad(funnel, "src");
        ghost_pad_and_add_to_bin(rtcp_src_pad, receive_input_bin, "rtcp_src");
        gst_object_unref(rtcp_src_pad);

        rtpbin_pad_name = g_strdup_printf("recv_rtcp_sink_%u", stream_id);
        linked_ok = gst_element_link_pads(receive_input_bin, "rtcp_src",
            transport_agent->priv->rtpbin, rtpbin_pad_name);
        g_warn_if_fail(linked_ok);
        g_free(rtpbin_pad_name);
        synced_ok = gst_element_sync_state_with_parent(funnel);
        g_warn_if_fail(synced_ok);
    } else {
        rtcp_src_pad = gst_element_get_static_pad(dtls_srtp_bin, "rtcp_src");
        ghost_pad_and_add_to_bin(rtcp_src_pad, receive_input_bin, "rtcp_src");
        gst_object_unref(rtcp_src_pad);
        rtpbin_pad_name = g_strdup_printf("recv_rtcp_sink_%u", stream_id);
        linked_ok = gst_element_link_pads(receive_input_bin, "rtcp_src",
            transport_agent->priv->rtpbin, rtpbin_pad_name);
        g_warn_if_fail(linked_ok);
        g_free(rtpbin_pad_name);
    }

    rtpbin_pad_name = g_strdup_printf("recv_rtp_sink_%u", stream_id);
    rtp_sink_pad = gst_element_get_static_pad(transport_agent->priv->rtpbin, rtpbin_pad_name);
    g_free(rtpbin_pad_name);
    scream_rx = g_new0(ScreamRx, 1);
    scream_rx->rtx_pt = -2; /* unknown */
    scream_rx->transport_agent = transport_agent;
    scream_rx->session_id = stream_id;
    scream_rx->adapt = TRUE; /* Always initiates to TRUE. Sets to TRUE or FALSE in probe_rtp_info */
    gst_pad_add_probe(rtp_sink_pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_rtp_info,
        scream_rx, g_free);
    gst_object_unref(rtp_sink_pad);
}


static void prepare_transport_bin_data_receive_elements(OwrTransportAgent *transport_agent,
    guint stream_id)
{
    OwrTransportAgentPrivate *priv;
    GstElement *nice_element, *dtls_srtp_bin, *sctpdec;
    GstElement *receive_input_bin;
    gchar *name;
    OwrDataSession *data_session;
    gboolean link_ok, sync_ok;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    priv = transport_agent->priv;

    data_session = OWR_DATA_SESSION(g_hash_table_lookup(priv->sessions,
        GUINT_TO_POINTER(stream_id)));
    g_return_if_fail(data_session);

    name = g_strdup_printf("receive-input-bin-%u", stream_id);
    receive_input_bin = gst_bin_new(name);
    g_free(name);

    if (!gst_bin_add(GST_BIN(priv->transport_bin), receive_input_bin)) {
        GST_ERROR("Failed to add receive-input-bin-%u to parent bin", stream_id);
        return;
    }
    if (!gst_element_sync_state_with_parent(receive_input_bin)) {
        GST_ERROR("Failed to sync receive-input-bin-%u to parent bin", stream_id);
        return;
    }

    nice_element = add_nice_element(transport_agent, stream_id, FALSE, FALSE, receive_input_bin);
    dtls_srtp_bin = add_dtls_srtp_bin(transport_agent, stream_id, FALSE, FALSE, receive_input_bin);
    sctpdec = _owr_data_session_create_decoder(data_session);
    g_return_if_fail(nice_element && dtls_srtp_bin && sctpdec);

    g_object_set_data(G_OBJECT(sctpdec), "session-id", GUINT_TO_POINTER(stream_id));
    gst_bin_add(GST_BIN(receive_input_bin), sctpdec);
    g_signal_connect(sctpdec, "pad-added", (GCallback)sctpdec_pad_added, transport_agent);
    g_signal_connect(sctpdec, "pad-removed", (GCallback)sctpdec_pad_removed, transport_agent);

    link_ok = gst_element_link_pads(dtls_srtp_bin, "data_src", sctpdec, "sink");
    link_ok &= gst_element_link(nice_element, dtls_srtp_bin);
    g_warn_if_fail(link_ok);

    sync_ok = gst_element_sync_state_with_parent(sctpdec);
    sync_ok &= gst_element_sync_state_with_parent(dtls_srtp_bin);
    sync_ok &= gst_element_sync_state_with_parent(nice_element);
    g_warn_if_fail(sync_ok);
}

static void prepare_transport_bin_data_send_elements(OwrTransportAgent *transport_agent,
    guint stream_id)
{
    OwrTransportAgentPrivate *priv;
    GstElement *nice_element, *dtls_srtp_bin, *sctpenc, *send_output_bin;
    OwrDataSession *data_session;
    gboolean linked_ok, sync_ok;
    gchar *name;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    priv = transport_agent->priv;
    data_session = OWR_DATA_SESSION(g_hash_table_lookup(priv->sessions, GUINT_TO_POINTER(stream_id)));

    name = g_strdup_printf("send-output-bin-%u", stream_id);
    send_output_bin = gst_bin_new(name);
    g_free(name);

    if (!gst_bin_add(GST_BIN(transport_agent->priv->transport_bin), send_output_bin)) {
        GST_ERROR("Failed to add send-output-bin-%u to parent bin", stream_id);
        return;
    }
    if (!gst_element_sync_state_with_parent(send_output_bin)) {
        GST_ERROR("Failed to sync send-output-bin-%u to parent bin", stream_id);
        return;
    }

    nice_element = add_nice_element(transport_agent, stream_id, TRUE, FALSE, send_output_bin);
    dtls_srtp_bin = add_dtls_srtp_bin(transport_agent, stream_id, TRUE, FALSE, send_output_bin);
    sctpenc = _owr_data_session_create_encoder(data_session);
    g_warn_if_fail(sctpenc);

    g_object_set_data(G_OBJECT(sctpenc), "session-id", GUINT_TO_POINTER(stream_id));
    gst_bin_add(GST_BIN(send_output_bin), sctpenc);
    g_signal_connect(sctpenc, "sctp-association-established",
        G_CALLBACK(on_sctp_association_established), transport_agent);

    linked_ok = gst_element_link(dtls_srtp_bin, nice_element);
    linked_ok &= gst_element_link_pads(sctpenc, "src", dtls_srtp_bin, "data_sink");
    g_warn_if_fail(linked_ok);

    sync_ok = gst_element_sync_state_with_parent(nice_element);
    sync_ok &= gst_element_sync_state_with_parent(dtls_srtp_bin);
    sync_ok &= gst_element_sync_state_with_parent(sctpenc);
    g_warn_if_fail(sync_ok);
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
    g_object_set(media_session, "send-ssrc", send_ssrc, "cname",
        gst_structure_get_string(sdes, "cname"), NULL);
    gst_structure_free(sdes);
    g_object_unref(session);
}

static gboolean emit_new_candidate(GHashTable *args)
{
    OwrTransportAgent *transport_agent;
    OwrTransportAgentPrivate *priv;
    OwrSession *session;
    NiceCandidate *nice_candidate;
    OwrCandidate *owr_candidate;
    gchar *ufrag = NULL, *password = NULL;
    gboolean got_credentials;

    transport_agent = OWR_TRANSPORT_AGENT(g_hash_table_lookup(args, "transport_agent"));
    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent), FALSE);
    priv = transport_agent->priv;

    nice_candidate = (NiceCandidate *)g_hash_table_lookup(args, "nice_candidate");
    g_return_val_if_fail(nice_candidate, FALSE);
    session = get_session(transport_agent, nice_candidate->stream_id);
    g_return_val_if_fail(OWR_IS_SESSION(session), FALSE);

    if (!nice_candidate->username || !nice_candidate->password) {
        got_credentials = nice_agent_get_local_credentials(priv->nice_agent,
            nice_candidate->stream_id, &ufrag, &password);
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
    g_return_val_if_fail(owr_candidate, FALSE);
    nice_candidate_free(nice_candidate);

    g_signal_emit_by_name(session, "on-new-candidate", owr_candidate);
    g_object_unref(owr_candidate);

    g_object_unref(session);
    g_hash_table_destroy(args);
    g_object_unref(transport_agent);

    return FALSE;
}

static void on_new_candidate(NiceAgent *nice_agent, NiceCandidate *nice_candidate,
    OwrTransportAgent *transport_agent)
{
    GHashTable *args;

    g_return_if_fail(nice_agent);
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_return_if_fail(nice_candidate);

    g_object_ref(transport_agent);
    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(transport_agent));
    g_hash_table_insert(args, "transport_agent", transport_agent);
    g_hash_table_insert(args, "nice_candidate", nice_candidate_copy(nice_candidate));

    _owr_schedule_with_hash_table((GSourceFunc)emit_new_candidate, args);

}

static gboolean emit_candidate_gathering_done(GHashTable *args)
{
    OwrTransportAgent *transport_agent;
    OwrSession *session;
    OwrCandidate *local_candidate, *remote_candidate;
    guint stream_id;
    int i;

    transport_agent = g_hash_table_lookup(args, "transport-agent");
    session = g_hash_table_lookup(args, "session");
    g_signal_emit_by_name(session, "on-candidate-gathering-done", NULL);

    stream_id = get_stream_id(transport_agent, session);
    g_return_val_if_fail(stream_id, FALSE);

    for (i = 0; i < OWR_COMPONENT_MAX; i++) {
        _owr_session_get_candidate_pair(session, i, &local_candidate, &remote_candidate);

        if (local_candidate && remote_candidate) {
            gchar *lfoundation = NULL, *rfoundation = NULL;

            g_object_get(local_candidate, "foundation", &lfoundation, NULL);
            g_object_get(remote_candidate, "foundation", &rfoundation, NULL);

            if (lfoundation && rfoundation) {
                GST_DEBUG_OBJECT(transport_agent, "Forcing pair %s:%s on stream %u",
                        lfoundation, rfoundation, stream_id);
                nice_agent_set_selected_pair(transport_agent->priv->nice_agent,
                        stream_id, i, lfoundation, rfoundation);
            } else
                g_assert_not_reached();

            g_free(lfoundation);
            g_free(rfoundation);
        }
    }

    g_hash_table_destroy(args);
    g_object_unref(session);

    return FALSE;
}

static void on_candidate_gathering_done(NiceAgent *nice_agent, guint stream_id, OwrTransportAgent *transport_agent)
{
    OwrSession *session;
    GHashTable *args;

    g_return_if_fail(nice_agent);
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    session = get_session(transport_agent, stream_id);
    g_return_if_fail(OWR_IS_SESSION(session));

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(session));
    g_hash_table_insert(args, "transport-agent", transport_agent);
    g_hash_table_insert(args, "session", session);

    _owr_schedule_with_hash_table((GSourceFunc)emit_candidate_gathering_done, args);

}

static gboolean emit_ice_state_changed(GHashTable *args)
{
    OwrSession *session;
    guint session_id;
    OwrComponentType component_type;
    OwrIceState state;

    session = g_hash_table_lookup(args, "session");
    session_id = GPOINTER_TO_UINT(g_hash_table_lookup(args, "session-id"));
    component_type = GPOINTER_TO_UINT(g_hash_table_lookup(args, "component-type"));
    state = GPOINTER_TO_UINT(g_hash_table_lookup(args, "ice-state"));

    _owr_session_emit_ice_state_changed(session, session_id, component_type, state);

    g_hash_table_destroy(args);
    g_object_unref(session);

    return FALSE;
}

static void on_component_state_changed(NiceAgent *nice_agent, guint stream_id,
    guint component_id, OwrIceState state, OwrTransportAgent *transport_agent)
{
    OwrSession *session;
    GHashTable *args;

    g_return_if_fail(nice_agent);
    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    session = get_session(transport_agent, stream_id);
    g_return_if_fail(OWR_IS_SESSION(session));

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(session));
    g_hash_table_insert(args, "session", session);
    g_hash_table_insert(args, "session-id", GUINT_TO_POINTER(stream_id));
    g_hash_table_insert(args, "component-type", GUINT_TO_POINTER(component_id));
    g_hash_table_insert(args, "ice-state", GUINT_TO_POINTER(state));

    _owr_schedule_with_hash_table((GSourceFunc)emit_ice_state_changed, args);

}

static void on_new_selected_pair(NiceAgent *nice_agent,
    guint stream_id, guint component_id,
    NiceCandidate *lcandidate, NiceCandidate *rcandidate,
    OwrTransportAgent *transport_agent)
{
    OwrSession *session;
    PendingSessionInfo *pending_session_info;

    OWR_UNUSED(nice_agent);
    OWR_UNUSED(lcandidate);
    OWR_UNUSED(rcandidate);

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));

    session = get_session(transport_agent, stream_id);
    g_return_if_fail(OWR_IS_SESSION(session));

    g_mutex_lock(&transport_agent->priv->sessions_lock);
    pending_session_info = g_hash_table_lookup(transport_agent->priv->pending_sessions, GUINT_TO_POINTER(stream_id));
    if (pending_session_info) {
        if (component_id == NICE_COMPONENT_TYPE_RTP && pending_session_info->nice_src_block_rtp) {
            GstPad *pad;
            gboolean sync_ok, link_ok;

            link_ok = gst_element_link(pending_session_info->nice_src_rtp, pending_session_info->dtls_dec_rtp);
            g_warn_if_fail(link_ok);

            gst_element_set_locked_state(pending_session_info->dtls_dec_rtp, FALSE);
            sync_ok = gst_element_sync_state_with_parent(pending_session_info->dtls_dec_rtp);
            g_warn_if_fail(sync_ok);
            gst_element_set_locked_state(pending_session_info->dtls_enc_rtp, FALSE);
            sync_ok = gst_element_sync_state_with_parent(pending_session_info->dtls_enc_rtp);
            g_warn_if_fail(sync_ok);

            pad = gst_element_get_static_pad(pending_session_info->nice_src_rtp, "src");
            gst_pad_remove_probe(pad, pending_session_info->nice_src_block_rtp);
            gst_object_unref(pad);
            pending_session_info->nice_src_block_rtp = 0;
        }

        /* when doing standalone RTCP we unblock the RTCP bin and sync its state whenever the first component of
         * the stream is connected. This is because the actual RTCP connection might be established
         * much later (or even never). Also see link_rtpbin_to_send_output_bin.
         */
        if (pending_session_info->nice_src_block_rtcp) {
            GstPad *pad;
            gboolean sync_ok, link_ok;

            link_ok = gst_element_link(pending_session_info->nice_src_rtcp, pending_session_info->dtls_dec_rtcp);
            g_warn_if_fail(link_ok);

            gst_element_set_locked_state(pending_session_info->dtls_dec_rtcp, FALSE);
            sync_ok = gst_element_sync_state_with_parent(pending_session_info->dtls_dec_rtcp);
            g_warn_if_fail(sync_ok);
            gst_element_set_locked_state(pending_session_info->dtls_enc_rtcp, FALSE);
            sync_ok = gst_element_sync_state_with_parent(pending_session_info->dtls_enc_rtcp);
            g_warn_if_fail(sync_ok);

            pad = gst_element_get_static_pad(pending_session_info->nice_src_rtcp, "src");
            gst_pad_remove_probe(pad, pending_session_info->nice_src_block_rtcp);
            gst_object_unref(pad);
            pending_session_info->nice_src_block_rtcp = 0;
        }
    }
    g_mutex_unlock(&transport_agent->priv->sessions_lock);
}

static gboolean maybe_handle_new_send_source_with_payload_from_main_thread(GHashTable *args)
{
    OwrTransportAgent *transport_agent;
    OwrMediaSession *session;

    session = OWR_MEDIA_SESSION(g_hash_table_lookup(args, "session"));
    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(session), FALSE);
    transport_agent = OWR_TRANSPORT_AGENT(g_hash_table_lookup(args, "transport_agent"));
    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent), FALSE);

    maybe_handle_new_send_source_with_payload(transport_agent, session);

    g_hash_table_destroy(args);
    g_object_unref(session);
    g_object_unref(transport_agent);

    return FALSE;
}

static void
on_dtls_enc_key_set(GstElement *dtls_srtp_enc, AgentAndSessionIdPair *data)
{
    OwrTransportAgent *transport_agent = data->transport_agent;
    OwrSession *session;
    guint stream_id = data->session_id;
    PendingSessionInfo *pending_session_info;

    session = get_session(transport_agent, stream_id);
    g_return_if_fail(session);

    /* Once we have the key, the DTLS handshake is done and we can start sending data here. Note
     * that we only wait for the DTLS handshake to be completed for the RTP component.
     */
    g_mutex_lock(&transport_agent->priv->sessions_lock);
    pending_session_info = g_hash_table_lookup(transport_agent->priv->pending_sessions, GUINT_TO_POINTER(stream_id));
    /* FIXME: What to do about RTCP? It's not guaranteed to ever be enabled if
     * RTCP muxing is used but the usage wasn't known beforehand
     */
    if (pending_session_info && dtls_srtp_enc == pending_session_info->dtls_enc_rtp) {
        GHashTable *args;

        g_hash_table_remove(transport_agent->priv->pending_sessions, GUINT_TO_POINTER(stream_id));
        args = g_hash_table_new(g_str_hash, g_str_equal);
        g_hash_table_insert(args, "session", session);
        g_hash_table_insert(args, "transport_agent", g_object_ref(transport_agent));
        _owr_schedule_with_hash_table((GSourceFunc)maybe_handle_new_send_source_with_payload_from_main_thread, args);
    }
    g_mutex_unlock(&transport_agent->priv->sessions_lock);

    g_object_unref(session);
}

static guint get_stream_id(OwrTransportAgent *transport_agent, OwrSession *session)
{
    GHashTableIter iter;
    OwrSession *s;
    gpointer stream_id = GUINT_TO_POINTER(0);

    g_mutex_lock(&transport_agent->priv->sessions_lock);
    g_hash_table_iter_init(&iter, transport_agent->priv->sessions);
    while (g_hash_table_iter_next(&iter, &stream_id, (gpointer)&s)) {
        if (s == session) {
            g_mutex_unlock(&transport_agent->priv->sessions_lock);
            return GPOINTER_TO_UINT(stream_id);
        }
    }
    g_mutex_unlock(&transport_agent->priv->sessions_lock);

    g_warn_if_reached();
    return 0;
}

static OwrSession * get_session(OwrTransportAgent *transport_agent, guint stream_id)
{
    OwrSession *s;

    g_mutex_lock(&transport_agent->priv->sessions_lock);
    s = OWR_SESSION(g_hash_table_lookup(transport_agent->priv->sessions, GUINT_TO_POINTER(stream_id)));
    if (s)
        g_object_ref(s);
    g_mutex_unlock(&transport_agent->priv->sessions_lock);

    return s;
}

static void update_flip_method(OwrPayload *payload, GParamSpec *pspec, GstElement *flip)
{
    guint rotation = 0;
    gboolean mirror = FALSE;
    gint flip_method;

    g_return_if_fail(OWR_IS_VIDEO_PAYLOAD(payload));
    g_return_if_fail(G_IS_PARAM_SPEC(pspec) || !pspec);
    g_return_if_fail(GST_IS_ELEMENT(flip));

    g_object_get(payload, "rotation", &rotation, "mirror", &mirror, NULL);
    flip_method = _owr_rotation_and_mirror_to_video_flip_method(rotation, mirror);
    g_object_set(flip, "method", flip_method, NULL);
}

/* pad is transfer full */
static void add_pads_to_bin_and_transport_bin(GstPad *pad, GstElement *bin, GstElement *transport_bin,
    const gchar *pad_name)
{
    GstPad *bin_pad;

    bin_pad = ghost_pad_and_add_to_bin(pad, bin, pad_name);

    ghost_pad_and_add_to_bin(bin_pad, transport_bin, pad_name);
}

static void on_caps(GstElement *sink, GParamSpec *pspec, OwrSession *session)
{
    GstCaps *caps;

    OWR_UNUSED(pspec);

    g_object_get(sink, "caps", &caps, NULL);

    if (GST_IS_CAPS(caps))
        GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Sending media configured with caps: %" GST_PTR_FORMAT, caps);
}

static void handle_new_send_payload(OwrTransportAgent *transport_agent, OwrMediaSession *media_session, OwrPayload * payload)
{
    guint stream_id;
    GstElement *send_input_bin = NULL;
    GstElement *encoder = NULL, *parser = NULL, *payloader = NULL,
        *rtp_capsfilter = NULL, *rtpbin = NULL;
    GstCaps *caps = NULL, *rtp_caps = NULL;
    gchar *name = NULL;
    gboolean link_ok = TRUE, sync_ok = TRUE;
    GstPad *sink_pad = NULL, *rtp_sink_pad = NULL, *rtp_capsfilter_src_pad = NULL,
        *ghost_src_pad = NULL, *encoder_sink_pad;
    OwrMediaType media_type;
    GstPadLinkReturn link_res;
    guint send_ssrc = 0;
    gchar *cname = NULL;

    g_return_if_fail(transport_agent);
    g_return_if_fail(media_session);
    g_assert(payload);

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
    rtp_sink_pad = gst_element_get_request_pad(rtpbin, name);
    g_free(name);

    link_rtpbin_to_send_output_bin(transport_agent, stream_id, TRUE, TRUE);

    g_object_get(payload, "media-type", &media_type, NULL);

    name = g_strdup_printf("send-rtp-capsfilter-%u", stream_id);
    rtp_capsfilter = gst_element_factory_make("capsfilter", name);
    g_free(name);
    rtp_caps = _owr_payload_create_rtp_caps(payload);

    g_object_get(media_session, "send-ssrc", &send_ssrc, "cname", &cname, NULL);
    if (cname) {
        GObject *internal_session = NULL;
        GstStructure *sdes = NULL;

        g_signal_emit_by_name(rtpbin, "get-internal-session", stream_id, &internal_session);
        g_warn_if_fail(internal_session);

        g_object_get(internal_session, "sdes", &sdes, NULL);
        gst_structure_set(sdes, "cname", G_TYPE_STRING, cname, NULL);
        g_object_set(internal_session, "sdes", sdes, NULL);

        gst_structure_free(sdes);
        g_object_unref(internal_session);
    }
    if (send_ssrc)
        gst_caps_set_simple(rtp_caps, "ssrc", G_TYPE_UINT, send_ssrc, NULL);

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
        GstElement *gldownload, *flip, *queue = NULL, *encoder_capsfilter;

        name = g_strdup_printf("send-input-video-gldownload-%u", stream_id);
        gldownload = gst_element_factory_make("gldownload", name);
        g_free(name);

        name = g_strdup_printf("send-input-video-flip-%u", stream_id);
        flip = gst_element_factory_make("videoflip", name);
        g_assert(flip);
        g_free(name);
        g_return_if_fail(OWR_IS_VIDEO_PAYLOAD(payload));
        g_signal_connect_object(payload, "notify::rotation", G_CALLBACK(update_flip_method), flip, 0);
        g_signal_connect_object(payload, "notify::mirror", G_CALLBACK(update_flip_method), flip, 0);
        update_flip_method(payload, NULL, flip);

        name = g_strdup_printf("send-input-video-queue-%u", stream_id);
        queue = gst_element_factory_make("queue", name);
        g_free(name);
        g_object_set(queue, "max-size-buffers", 3, "max-size-bytes", 0,
            "max-size-time", G_GUINT64_CONSTANT(0), NULL);

        encoder = _owr_payload_create_encoder(payload);
        parser = _owr_payload_create_parser(payload);
        payloader = _owr_payload_create_payload_packetizer(payload);
        g_warn_if_fail(payloader && encoder);

        encoder_sink_pad = gst_element_get_static_pad(encoder, "sink");
        g_signal_connect(encoder_sink_pad, "notify::caps", G_CALLBACK(on_caps), OWR_SESSION(media_session));
        gst_object_unref(encoder_sink_pad);

        name = g_strdup_printf("send-input-video-encoder-capsfilter-%u", stream_id);
        encoder_capsfilter = gst_element_factory_make("capsfilter", name);
        g_free(name);
        caps = _owr_payload_create_encoded_caps(payload);
        g_object_set(encoder_capsfilter, "caps", caps, NULL);
        gst_caps_unref(caps);

        gst_bin_add_many(GST_BIN(send_input_bin), gldownload, flip, queue, encoder, encoder_capsfilter, payloader, NULL);
        if (parser) {
            gst_bin_add(GST_BIN(send_input_bin), parser);
            link_ok &= gst_element_link_many(gldownload, flip, queue, encoder, parser, encoder_capsfilter, payloader, NULL);
        } else
            link_ok &= gst_element_link_many(gldownload, flip, queue, encoder, encoder_capsfilter, payloader, NULL);

        link_ok &= gst_element_link_many(payloader, rtp_capsfilter, NULL);

        g_warn_if_fail(link_ok);

        sync_ok &= gst_element_sync_state_with_parent(rtp_capsfilter);
        sync_ok &= gst_element_sync_state_with_parent(payloader);
        if (parser)
            sync_ok &= gst_element_sync_state_with_parent(parser);
        sync_ok &= gst_element_sync_state_with_parent(encoder_capsfilter);
        sync_ok &= gst_element_sync_state_with_parent(encoder);
        sync_ok &= gst_element_sync_state_with_parent(queue);
        sync_ok &= gst_element_sync_state_with_parent(flip);
        sync_ok &= gst_element_sync_state_with_parent(gldownload);

        name = g_strdup_printf("video_sink_%u_%u", OWR_CODEC_TYPE_NONE, stream_id);
        sink_pad = gst_element_get_static_pad(gldownload, "sink");
        add_pads_to_bin_and_transport_bin(sink_pad, send_input_bin,
            transport_agent->priv->transport_bin, name);
        gst_object_unref(sink_pad);
        g_free(name);
    } else { /* Audio */
        encoder = _owr_payload_create_encoder(payload);
        parser = _owr_payload_create_parser(payload);
        payloader = _owr_payload_create_payload_packetizer(payload);

        encoder_sink_pad = gst_element_get_static_pad(encoder, "sink");
        g_signal_connect(encoder_sink_pad, "notify::caps", G_CALLBACK(on_caps), OWR_SESSION(media_session));
        gst_object_unref(encoder_sink_pad);

        gst_bin_add_many(GST_BIN(send_input_bin), encoder, payloader, NULL);
        if (parser) {
            gst_bin_add(GST_BIN(send_input_bin), parser);
            link_ok &= gst_element_link_many(encoder, parser, payloader, NULL);
        } else
            link_ok &= gst_element_link_many(encoder, payloader, NULL);

        link_ok &= gst_element_link_many(payloader, rtp_capsfilter, NULL);
        g_warn_if_fail(link_ok);

        sync_ok &= gst_element_sync_state_with_parent(rtp_capsfilter);
        sync_ok &= gst_element_sync_state_with_parent(payloader);
        if (parser)
            sync_ok &= gst_element_sync_state_with_parent(parser);
        sync_ok &= gst_element_sync_state_with_parent(encoder);
        g_warn_if_fail(sync_ok);

        name = g_strdup_printf("audio_raw_sink_%u", stream_id);
        sink_pad = gst_element_get_static_pad(encoder, "sink");
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

static void on_local_candidate_change(OwrTransportAgent *transport_agent, OwrCandidate *candidate, OwrSession *session)
{
    guint stream_id;
    gchar *ufrag = NULL, *password = NULL;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    g_return_if_fail(OWR_IS_CANDIDATE(candidate));
    g_return_if_fail(OWR_IS_SESSION(session));

    stream_id = get_stream_id(transport_agent, session);
    g_return_if_fail(stream_id);

    g_object_get(G_OBJECT(candidate), "ufrag", &ufrag, "password", &password, NULL);
    nice_agent_set_local_credentials(transport_agent->priv->nice_agent, stream_id, ufrag, password);
    g_free(ufrag);
    g_free(password);
}

static gboolean emit_on_incoming_source(GHashTable *args)
{
    OwrMediaSession *media_session;
    OwrMediaSource *source;

    media_session = g_hash_table_lookup(args, "media_session");
    source = g_hash_table_lookup(args, "source");

    g_signal_emit_by_name(media_session, "on-incoming-source", source);

    g_object_unref(source);
    g_object_unref(media_session);
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

    media_session = OWR_MEDIA_SESSION(get_session(transport_agent, stream_id));

    source = _owr_remote_media_source_new(type, stream_id, codec_type,
        transport_agent->priv->transport_bin);

    g_return_if_fail(source);

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(media_session));
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

        media_session = OWR_MEDIA_SESSION(get_session(transport_agent, session_id));
        payload = _owr_media_session_get_receive_payload(media_session, pt);

        g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));
        g_return_if_fail(OWR_IS_PAYLOAD(payload));
        g_object_get(payload, "media-type", &media_type, NULL);

        g_object_set_data(G_OBJECT(media_session), "ssrc", GUINT_TO_POINTER(ssrc));
        if (media_type == OWR_MEDIA_TYPE_VIDEO)
            setup_video_receive_elements(new_pad, session_id, payload, transport_agent);
        else
            setup_audio_receive_elements(new_pad, session_id, payload, transport_agent);

        /* Hook up RTCP sending if it isn't already */
        link_rtpbin_to_send_output_bin(transport_agent, session_id, FALSE, TRUE);

        g_object_unref(media_session);
        g_object_unref(payload);
    } else if (g_str_has_prefix(new_pad_name, "send_rtp_src")) {
        guint32 session_id = 0;
        sscanf(new_pad_name, "send_rtp_src_%u", &session_id);
    }

    g_free(new_pad_name);
}

typedef struct {
    OwrSession *session;
    guint session_id;
} SessionData;

static void session_data_free(gpointer session_data)
{
    g_slice_free(SessionData, session_data);
}

static GstPadProbeReturn check_for_keyframe(GstPad *pad, GstPadProbeInfo *info,
    gpointer user_data)
{
    SessionData *session_data = (SessionData *)user_data;
    OWR_UNUSED(pad);

    if (!GST_BUFFER_FLAG_IS_SET(info->data, GST_BUFFER_FLAG_DELTA_UNIT)) {
        GST_CAT_INFO_OBJECT(_owrsession_debug, session_data->session,
            "Session %u, Received keyframe for %u\n", session_data->session_id,
            GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(session_data->session), "ssrc")));
    }

    return GST_PAD_PROBE_OK;
}

static void setup_video_receive_elements(GstPad *new_pad, guint32 session_id, OwrPayload *payload, OwrTransportAgent *transport_agent)
{
    GstPad *depay_sink_pad = NULL, *ghost_pad = NULL;
    gboolean sync_ok = TRUE;
    GstElement *receive_output_bin;
    GstElement *rtpdepay, *videorepair1, *parser, *decoder;
    GstPadLinkReturn link_res;
    gboolean link_ok = TRUE;
    OwrCodecType codec_type;
    gchar name[100];
    GstPad *pad;
    SessionData *session_data;

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

    pad = gst_element_get_static_pad(videorepair1, "src");
    session_data = g_slice_new(SessionData);
    session_data->session = get_session(transport_agent, session_id);
    session_data->session_id = session_id;
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, check_for_keyframe,
        session_data, session_data_free);
    gst_object_unref(pad);
    pad = NULL;

    g_object_get(payload, "codec-type", &codec_type, NULL);
    parser = _owr_payload_create_parser(payload);
    decoder = _owr_payload_create_decoder(payload);

    gst_bin_add_many(GST_BIN(receive_output_bin), rtpdepay,
        videorepair1, decoder, /*decoded_tee,*/ NULL);
    depay_sink_pad = gst_element_get_static_pad(rtpdepay, "sink");
    if (parser) {
        gst_bin_add(GST_BIN(receive_output_bin), parser);
        link_ok &= gst_element_link_many(rtpdepay, parser, videorepair1, decoder, NULL);
    } else
        link_ok &= gst_element_link_many(rtpdepay, videorepair1, decoder, NULL);

    ghost_pad = ghost_pad_and_add_to_bin(depay_sink_pad, receive_output_bin, "sink");
    link_res = gst_pad_link(new_pad, ghost_pad);
    gst_object_unref(depay_sink_pad);
    ghost_pad = NULL;
    g_warn_if_fail(link_ok && (link_res == GST_PAD_LINK_OK));

    sync_ok &= gst_element_sync_state_with_parent(decoder);
    if (parser)
        sync_ok &= gst_element_sync_state_with_parent(parser);
    sync_ok &= gst_element_sync_state_with_parent(videorepair1);
    sync_ok &= gst_element_sync_state_with_parent(rtpdepay);
    g_warn_if_fail(sync_ok);

    pad = gst_element_get_static_pad(decoder, "src");
    g_snprintf(name, OWR_OBJECT_NAME_LENGTH_MAX, "video_src_%u_%u", OWR_CODEC_TYPE_NONE,
        session_id);
    add_pads_to_bin_and_transport_bin(pad, receive_output_bin, transport_agent->priv->transport_bin, name);
    gst_object_unref(pad);
}

static void setup_audio_receive_elements(GstPad *new_pad, guint32 session_id, OwrPayload *payload, OwrTransportAgent *transport_agent)
{
    GstElement *receive_output_bin;
    gchar *pad_name = NULL;
    GstElement *rtp_capsfilter, *rtpdepay, *parser, *decoder;
    GstPad *rtp_caps_sink_pad = NULL, *pad = NULL, *ghost_pad = NULL;
    gchar *element_name = NULL;
    GstCaps *rtp_caps = NULL;
    gboolean link_ok = FALSE;
    gboolean sync_ok = TRUE;

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

    parser = _owr_payload_create_parser(payload);
    decoder = _owr_payload_create_decoder(payload);

    gst_bin_add_many(GST_BIN(receive_output_bin), rtp_capsfilter, rtpdepay,
        decoder, NULL);
    link_ok = gst_element_link_many(rtp_capsfilter, rtpdepay, NULL);
    if (parser) {
        gst_bin_add(GST_BIN(receive_output_bin), parser);
        link_ok &= gst_element_link_many(rtpdepay, parser, decoder, NULL);
    } else
        link_ok &= gst_element_link_many(rtpdepay, decoder, NULL);

    g_warn_if_fail(link_ok);

    rtp_caps_sink_pad = gst_element_get_static_pad(rtp_capsfilter, "sink");
    ghost_pad = ghost_pad_and_add_to_bin(rtp_caps_sink_pad, receive_output_bin, "sink");
    gst_object_unref(rtp_caps_sink_pad);
    if (!GST_PAD_LINK_SUCCESSFUL(gst_pad_link(new_pad, ghost_pad))) {
        GST_ERROR("Failed to link rtpbin with receive-output-bin-%u", session_id);
        return;
    }
    ghost_pad = NULL;

    sync_ok &= gst_element_sync_state_with_parent(decoder);
    if (parser)
        sync_ok &= gst_element_sync_state_with_parent(parser);
    sync_ok &= gst_element_sync_state_with_parent(rtpdepay);
    sync_ok &= gst_element_sync_state_with_parent(rtp_capsfilter);
    g_warn_if_fail(sync_ok);

    pad = gst_element_get_static_pad(decoder, "src");
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

    media_session = OWR_MEDIA_SESSION(get_session(transport_agent, session_id));
    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), NULL);

    payload = _owr_media_session_get_receive_payload(media_session, pt);

    if (payload) {
        caps = _owr_payload_create_rtp_caps(payload);
        g_object_unref(payload);
    }
    g_object_unref(media_session);

    return caps;
}

static GstElement * create_aux_bin(gchar *prefix, GstElement *rtx, guint session_id)
{
    GstElement *bin;
    GstPad *pad;
    gchar *tmp;

    tmp = g_strdup_printf("%s_%u", prefix, session_id);
    bin = gst_bin_new(tmp);
    g_free(tmp);

    gst_bin_add(GST_BIN(bin), rtx);

    tmp = g_strdup_printf("src_%u", session_id);
    pad = gst_element_get_static_pad(rtx, "src");

    gst_element_add_pad(bin, gst_ghost_pad_new(tmp, pad));

    gst_object_unref(pad);
    g_free(tmp);

    tmp = g_strdup_printf("sink_%u", session_id);
    pad = gst_element_get_static_pad(rtx, "sink");

    gst_element_add_pad(bin, gst_ghost_pad_new(tmp, pad));

    gst_object_unref(pad);
    g_free(tmp);

    return bin;
}

static GstElement * on_rtpbin_request_aux_sender(G_GNUC_UNUSED GstElement *rtpbin, guint session_id, OwrTransportAgent *transport_agent)
{
    OwrMediaSession *media_session;
    OwrPayload *payload;
    GstElement *rtxsend;
    GstStructure *pt_map;
    gint rtx_pt;
    guint pt, rtx_time;
    gchar *tmp;

    media_session = OWR_MEDIA_SESSION(get_session(transport_agent, session_id));
    g_return_val_if_fail(media_session, NULL);

    payload = _owr_media_session_get_send_payload(media_session);
    g_object_unref(media_session);
    if (!payload)
        goto no_retransmission;

    g_object_get(payload, "rtx-payload-type", &rtx_pt, NULL);
    if (rtx_pt < 0) {
        g_object_unref(payload);
        goto no_retransmission;
    }

    rtxsend = gst_element_factory_make("rtprtxsend", NULL);
    g_return_val_if_fail(rtxsend, NULL);

    /* Create and set the pt map */
    g_object_get(payload, "payload-type", &pt, NULL);

    pt_map = gst_structure_new_empty("application/x-rtp-pt-map");
    tmp = g_strdup_printf("%u", pt);
    gst_structure_set(pt_map, tmp, G_TYPE_UINT, (guint) rtx_pt, NULL);
    g_free(tmp);

    g_object_set(rtxsend, "payload-type-map", pt_map, NULL);
    gst_structure_free(pt_map);

    g_object_get(payload, "rtx-time", &rtx_time, NULL);
    if (rtx_time)
        g_object_set(rtxsend, "max-size-time", rtx_time, NULL);

    g_object_unref(payload);
    return create_aux_bin("rtprtxsend", rtxsend, session_id);

no_retransmission:
    return NULL;
}

static GstElement * on_rtpbin_request_aux_receiver(G_GNUC_UNUSED GstElement *rtpbin, G_GNUC_UNUSED guint session_id, OwrTransportAgent *transport_agent)
{
    OwrMediaSession *media_session;
    GstElement *rtxrecv;
    GstStructure *pt_map;

    media_session = OWR_MEDIA_SESSION(get_session(transport_agent, session_id));
    g_return_val_if_fail(media_session, NULL);

    pt_map = _owr_media_session_get_receive_rtx_pt_map(media_session);
    g_object_unref(media_session);

    if (!pt_map)
        goto no_retransmission;

    rtxrecv = gst_element_factory_make("rtprtxreceive", NULL);
    g_return_val_if_fail(rtxrecv, NULL);

    g_object_set(rtxrecv, "payload-type-map", pt_map, NULL);
    gst_structure_free(pt_map);
    /* FIXME: how do we get rtx-time? */

    return create_aux_bin("rtprtxrecv", rtxrecv, session_id);

no_retransmission:
    return NULL;
}

static void print_rtcp_type(GObject *session, guint session_id,
    GstRTCPType packet_type)
{
    GST_CAT_DEBUG_OBJECT(_owrsession_debug, session, "Session %u, Received RTCP %s\n", session_id,
        packet_type == GST_RTCP_TYPE_INVALID ? "Invalid type (INVALID)" :
        packet_type == GST_RTCP_TYPE_SR ? "Sender Report (SR)" :
        packet_type == GST_RTCP_TYPE_RR ? "Receiver Report (RR)" :
        packet_type == GST_RTCP_TYPE_SDES ? "Source Description (SDES)" :
        packet_type == GST_RTCP_TYPE_BYE ? "Goodbye (BYE)" :
        packet_type == GST_RTCP_TYPE_APP ? "Application defined (APP)" :
        packet_type == GST_RTCP_TYPE_RTPFB ? "RTP Feedback (RTPFB)" :
        packet_type == GST_RTCP_TYPE_PSFB ? "Payload-Specific Feedback (PSFB)" :
        "unknown");
}

static void print_rtcp_feedback_type(GObject *session, guint session_id,
    guint fbtype, guint media_ssrc, GstRTCPType packet_type, guint8 *fci,
    gboolean is_received)
{
    if (fbtype == GST_RTCP_FB_TYPE_INVALID) {
        GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Invalid type\n",
            session_id, is_received ? "Received" : "Sent", media_ssrc);
    } else if (packet_type == GST_RTCP_TYPE_RTPFB) {
        switch (fbtype) {
        case GST_RTCP_RTPFB_TYPE_NACK:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Generic NACK\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_RTPFB_TYPE_TMMBR:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Temporary Maximum Media Stream Bit Rate Request\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_RTPFB_TYPE_TMMBN:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Temporary Maximum Media Stream Bit Rate Notification\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_RTPFB_TYPE_RTCP_SR_REQ:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Request an SR packet for early synchronization\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_RTPFB_TYPE_SCREAM:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: SCReAM\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        default:
            GST_CAT_WARNING_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Unknown feedback type %u\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc, fbtype);
            break;
        }
    } else if (packet_type == GST_RTCP_TYPE_PSFB) {
        switch (fbtype) {
        case GST_RTCP_PSFB_TYPE_PLI:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Picture Loss Indication\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_PSFB_TYPE_SLI:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Slice Loss Indication\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_PSFB_TYPE_RPSI:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Reference Picture Selection Indication\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_PSFB_TYPE_AFB:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Application layer Feedback\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_PSFB_TYPE_FIR:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Full Intra Request Command\n",
                session_id, is_received ? "Received" : "Sent", fci ? GST_READ_UINT32_BE(fci) : 0);
            break;
        case GST_RTCP_PSFB_TYPE_TSTR:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Temporal-Spatial Trade-off Request\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_PSFB_TYPE_TSTN:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Temporal-Spatial Trade-off Notification\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        case GST_RTCP_PSFB_TYPE_VBCN:
            GST_CAT_INFO_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Video Back Channel Message\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc);
            break;
        default:
            GST_CAT_WARNING_OBJECT(_owrsession_debug, session, "Session %u, %s RTCP feedback for %u: Unknown feedback type %u\n",
                session_id, is_received ? "Received" : "Sent", media_ssrc, fbtype);
            break;
        }
    }
}

static gboolean on_sending_rtcp(GObject *session, GstBuffer *buffer, gboolean early,
    OwrTransportAgent *agent)
{
    OwrTransportAgentPrivate *priv = agent->priv;
    GstRTCPBuffer rtcp_buffer = {NULL, {NULL, 0, NULL, 0, 0, {0}, {0}}};
    GstRTCPPacket rtcp_packet;
    GstRTCPType packet_type;
    gboolean has_packet, do_not_suppress = FALSE;
    OwrMediaSession *media_session;
    OwrPayload *send_payload;
    OwrMediaType media_type = -1;
    GValueArray *sources = NULL;
    GObject *source = NULL;
    guint session_id = 0, rtcp_session_id = 0;
    GList *it, *next;
    GHashTable *rtcp_info;

    OWR_UNUSED(early);

    session_id = GPOINTER_TO_UINT(g_object_get_data(session, "session_id"));

    if (gst_rtcp_buffer_map(buffer, GST_MAP_READ | GST_MAP_WRITE, &rtcp_buffer)) {
        guint pt, fmt, ssrc, last_fb_wc, highest_seq, n_loss, n_ecn;

        has_packet = gst_rtcp_buffer_get_first_packet(&rtcp_buffer, &rtcp_packet);
        for (; has_packet; has_packet = gst_rtcp_packet_move_to_next(&rtcp_packet)) {
            packet_type = gst_rtcp_packet_get_type(&rtcp_packet);
            print_rtcp_type(session, session_id, packet_type);
            if (packet_type == GST_RTCP_TYPE_PSFB || packet_type == GST_RTCP_TYPE_RTPFB) {
                print_rtcp_feedback_type(session, session_id, gst_rtcp_packet_fb_get_type(&rtcp_packet),
                    gst_rtcp_packet_fb_get_media_ssrc(&rtcp_packet), packet_type,
                    gst_rtcp_packet_fb_get_fci(&rtcp_packet), FALSE);
                do_not_suppress = TRUE;
                break;
            }
        }

        g_mutex_lock(&priv->rtcp_lock);
        it = priv->rtcp_list;
        while (it) {
            rtcp_info = (GHashTable *)it->data;

            pt = GPOINTER_TO_UINT(g_hash_table_lookup(rtcp_info, "pt"));
            ssrc = GPOINTER_TO_UINT(g_hash_table_lookup(rtcp_info, "ssrc"));
            rtcp_session_id = GPOINTER_TO_UINT(g_hash_table_lookup(rtcp_info, "session-id"));

            if (pt == GST_RTCP_TYPE_RTPFB) {
                if (session_id != rtcp_session_id) {
                    it = g_list_next(it);
                    continue;
                }
            }

            if (!gst_rtcp_buffer_add_packet(&rtcp_buffer, pt, &rtcp_packet)) {
                it = g_list_next(it);
                continue;
            }

            fmt = GPOINTER_TO_UINT(g_hash_table_lookup(rtcp_info, "fmt"));
            if (fmt == GST_RTCP_RTPFB_TYPE_SCREAM) {
                guint8 *fci_buf;
                last_fb_wc = GPOINTER_TO_UINT(g_hash_table_lookup(rtcp_info, "last-feedback-wallclock"));
                highest_seq = GPOINTER_TO_UINT(g_hash_table_lookup(rtcp_info, "highest-seq"));
                n_loss = GPOINTER_TO_UINT(g_hash_table_lookup(rtcp_info, "n-loss"));
                n_ecn = GPOINTER_TO_UINT(g_hash_table_lookup(rtcp_info, "n-ecn"));

                gst_rtcp_packet_fb_set_type(&rtcp_packet, fmt);
                gst_rtcp_packet_fb_set_sender_ssrc(&rtcp_packet, 0);
                gst_rtcp_packet_fb_set_media_ssrc(&rtcp_packet, ssrc);
                if (!gst_rtcp_packet_fb_set_fci_length(&rtcp_packet, 3)) {
                    /* Send next time instead.. */
                    gst_rtcp_packet_remove(&rtcp_packet);
                    it = g_list_next(it);
                    continue;
                }

                fci_buf = gst_rtcp_packet_fb_get_fci(&rtcp_packet);
                GST_WRITE_UINT16_BE(fci_buf, highest_seq);
                GST_WRITE_UINT8(fci_buf + 2, n_loss);
                GST_WRITE_UINT8(fci_buf + 3, n_ecn);
                GST_WRITE_UINT32_BE(fci_buf + 4, last_fb_wc);
                /* qbit not implemented yet  */
                GST_WRITE_UINT32_BE(fci_buf + 8, 0);
                do_not_suppress = TRUE;

                GST_DEBUG_OBJECT(session, "Sending scream feedback: "
                    "highest_seq: %u, n_loss: %u, n_ecn: %u, last_fb_wc: %u",
                    highest_seq, n_loss, n_ecn, last_fb_wc);
            }

            next = g_list_next(it);
            g_hash_table_unref(rtcp_info);
            agent->priv->rtcp_list = g_list_delete_link(agent->priv->rtcp_list, it);
            it = next;
        }
        g_mutex_unlock(&priv->rtcp_lock);

        gst_rtcp_buffer_unmap(&rtcp_buffer);
    }

    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(agent), do_not_suppress);

    media_session = OWR_MEDIA_SESSION(get_session(agent, session_id));
    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), do_not_suppress);
    send_payload = _owr_media_session_get_send_payload(media_session);
    if (send_payload) {
        g_object_get(send_payload, "media-type", &media_type, NULL);
        g_object_unref(send_payload);
    }

    g_object_get(session, "sources", &sources, NULL);
    source = g_value_get_object(g_value_array_get_nth(sources, 0));
    prepare_rtcp_stats(media_session, source);
    g_value_array_free(sources);
    g_object_unref(media_session);

    return do_not_suppress;
}

static void on_receiving_rtcp(GObject *session, GstBuffer *buffer,
    OwrTransportAgent *agent)
{
    GstRTCPBuffer rtcp_buffer = {NULL, {NULL, 0, NULL, 0, 0, {0}, {0}}};
    GstRTCPPacket rtcp_packet;
    GstRTCPType packet_type;
    gboolean has_packet;
    guint session_id = 0;

    OWR_UNUSED(agent);

    session_id = GPOINTER_TO_UINT(g_object_get_data(session, "session_id"));

    if (gst_rtcp_buffer_map(buffer, GST_MAP_READ, &rtcp_buffer)) {
        has_packet = gst_rtcp_buffer_get_first_packet(&rtcp_buffer, &rtcp_packet);
        for (; has_packet; has_packet = gst_rtcp_packet_move_to_next(&rtcp_packet)) {
            packet_type = gst_rtcp_packet_get_type(&rtcp_packet);
            print_rtcp_type(session, session_id, packet_type);
            if (packet_type == GST_RTCP_TYPE_PSFB || packet_type == GST_RTCP_TYPE_RTPFB) {
                print_rtcp_feedback_type(session, session_id, gst_rtcp_packet_fb_get_type(&rtcp_packet),
                    gst_rtcp_packet_fb_get_media_ssrc(&rtcp_packet), packet_type,
                    gst_rtcp_packet_fb_get_fci(&rtcp_packet), TRUE);
                break;
            }
        }
        gst_rtcp_buffer_unmap(&rtcp_buffer);
    }
}

static gboolean update_stats_hash_table(GQuark field_id, const GValue *src_value,
    GHashTable *stats_hash_table)
{
    const gchar *key = g_quark_to_string(field_id);
    GValue *value = _owr_value_table_add(stats_hash_table, key, G_VALUE_TYPE(src_value));
    g_value_copy(src_value, value);
    return TRUE;
}

static gboolean emit_stats_signal(GHashTable *stats_hash)
{
    GValue *value;
    OwrMediaSession *media_session;

    g_return_val_if_fail(stats_hash, FALSE);
    value = g_hash_table_lookup(stats_hash, "media_session");
    g_return_val_if_fail(G_VALUE_HOLDS_OBJECT(value), FALSE);
    media_session = g_value_dup_object(value);
    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), FALSE);
    g_hash_table_remove(stats_hash, "media_session");
    g_signal_emit_by_name(media_session, "on-new-stats", stats_hash, NULL);
    g_object_unref(media_session);
    g_hash_table_unref(stats_hash);
    return FALSE;
}

static void prepare_rtcp_stats(OwrMediaSession *media_session, GObject *rtp_source)
{
    GstStructure *stats;
    GHashTable *stats_hash;
    GValue *value;

    g_object_get(rtp_source, "stats", &stats, NULL);
    stats_hash = _owr_value_table_new();
    value = _owr_value_table_add(stats_hash, "type", G_TYPE_STRING);
    g_value_set_string(value, "rtcp");
    gst_structure_foreach(stats,
        (GstStructureForeachFunc)update_stats_hash_table, stats_hash);
    gst_structure_free(stats);

    value = _owr_value_table_add(stats_hash, "media_session", OWR_TYPE_MEDIA_SESSION);
    g_value_set_object(value, media_session);

    _owr_schedule_with_hash_table((GSourceFunc)emit_stats_signal, stats_hash);

}

static void on_ssrc_active(GstElement *rtpbin, guint session_id, guint ssrc,
    OwrTransportAgent *transport_agent)
{
    OwrMediaSession *media_session;
    GObject *rtp_session, *rtp_source;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    media_session = OWR_MEDIA_SESSION(get_session(transport_agent, session_id));
    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));

    g_signal_emit_by_name(rtpbin, "get-internal-session", session_id, &rtp_session);
    g_signal_emit_by_name(rtp_session, "get-source-by-ssrc", ssrc, &rtp_source);
    prepare_rtcp_stats(media_session, rtp_source);
    g_object_unref(rtp_source);
    g_object_unref(rtp_session);
    g_object_unref(media_session);
}

static void on_new_jitterbuffer(G_GNUC_UNUSED GstElement *rtpbin, GstElement *jitterbuffer, guint session_id, G_GNUC_UNUSED guint ssrc, OwrTransportAgent *transport_agent)
{
    OwrMediaSession *media_session;

    g_return_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent));
    media_session = OWR_MEDIA_SESSION(get_session(transport_agent, session_id));
    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));

    if (_owr_media_session_want_receive_rtx(media_session))
        g_object_set(jitterbuffer, "do-retransmission", TRUE, NULL);

    g_object_bind_property(media_session, "jitter-buffer-latency", jitterbuffer,
        "latency", G_BINDING_SYNC_CREATE);

    g_object_unref(media_session);
}

static void data_channel_free(DataChannel *data_channel_info)
{
    if (data_channel_info->data_sink)
        gst_object_unref(data_channel_info->data_sink);
    if (data_channel_info->data_src)
        gst_object_unref(data_channel_info->data_src);
    g_free(data_channel_info->protocol);
    g_free(data_channel_info->label);
    g_rw_lock_clear(&data_channel_info->rw_mutex);
    g_free(data_channel_info);
}

static gboolean create_datachannel(OwrTransportAgent *transport_agent, guint32 session_id, OwrDataChannel *data_channel)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    guint8 *buf;
    guint32 buf_size, reliability_param;
    GstFlowReturn flow_ret;
    GstBuffer *gstbuf;
    gboolean result = FALSE;
    OwrDataChannelChannelType channel_type;
    guint priority = 0; /* Priority not implemented yet */
    DataChannel *data_channel_info;
    gboolean ordered, negotiated;
    gint max_packet_life_time, max_packet_retransmits, sctp_stream_id;
    gchar *protocol, *label;

    g_object_get(data_channel, "ordered", &ordered, "max-packet-life-time", &max_packet_life_time,
        "max-retransmits", &max_packet_retransmits, "protocol", &protocol, "negotiated", &negotiated,
        "id", &sctp_stream_id, "label", &label, NULL);

    if (max_packet_life_time != -1 && max_packet_retransmits != -1) {
        g_warning("Invalid datachannel parameters");
        g_free(protocol);
        g_free(label);
        goto end;
    }

    if (!negotiated && !is_valid_sctp_stream_id(transport_agent, session_id, sctp_stream_id, FALSE)) {
        g_warning("Invalid stream_id");
        g_free(protocol);
        g_free(label);
        goto end;
    }

    g_rw_lock_writer_lock(&priv->data_channels_rw_mutex);
    if (g_hash_table_contains(priv->data_channels, GUINT_TO_POINTER(sctp_stream_id))) {
        g_warning("Data channel with stream id %u already exists", sctp_stream_id);
        g_free(protocol);
        g_free(label);
        goto end;
    }

    data_channel_info = g_new0(DataChannel, 1);
    data_channel_info->state = OWR_DATA_CHANNEL_STATE_CONNECTING;
    data_channel_info->id = sctp_stream_id;
    data_channel_info->label = label;
    data_channel_info->protocol = protocol;
    data_channel_info->session_id = session_id;
    data_channel_info->ctrl_bytes_sent = 0;
    data_channel_info->negotiated = negotiated;
    data_channel_info->ordered = ordered;
    data_channel_info->max_packet_life_time = max_packet_life_time;
    data_channel_info->max_packet_retransmits = max_packet_retransmits;

    g_rw_lock_init(&data_channel_info->rw_mutex);
    g_hash_table_insert(priv->data_channels, GUINT_TO_POINTER(sctp_stream_id),
        (gpointer)data_channel_info);
    g_rw_lock_writer_unlock(&priv->data_channels_rw_mutex);

    if (!create_datachannel_appsrc(transport_agent, data_channel))
        goto end;

    if (!negotiated) {
        channel_type = (ordered ? 0 : 0x80) | (max_packet_life_time  != -1 ? 0x02 : 0) |
            (max_packet_retransmits != -1 ? 0x01 : 0);

        reliability_param = 0;
        if (max_packet_life_time != -1)
            reliability_param += max_packet_life_time;
        if (max_packet_retransmits != -1)
            reliability_param += max_packet_retransmits;

        buf = create_datachannel_open_request(channel_type, reliability_param, priority,
            label, strlen(label), protocol, strlen(protocol), &buf_size);
        gstbuf = gst_buffer_new_wrapped(buf, buf_size);
        gst_sctp_buffer_add_send_meta(gstbuf, OWR_DATA_CHANNEL_PPID_CONTROL, TRUE,
            GST_SCTP_SEND_META_PARTIAL_RELIABILITY_NONE, 0);
        data_channel_info->ctrl_bytes_sent += buf_size;
        flow_ret = gst_app_src_push_buffer(GST_APP_SRC(data_channel_info->data_src), gstbuf);

        if (flow_ret != GST_FLOW_OK)
            g_critical("Failed to push data buffer: %s", gst_flow_get_name(flow_ret));
    }
    result = TRUE;

end:
    return result;
}


static void sctpdec_pad_added(GstElement *sctpdec, GstPad *sctpdec_srcpad,
    OwrTransportAgent *transport_agent)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    GstElement *data_sink, *receive_input_bin;
    GstPad *appsink_sinkpad;
    GstPadLinkReturn link_ret;
    guint sctp_stream_id, session_id;
    gchar *name;
    GstAppSinkCallbacks callbacks;
    DataChannel *data_channel_info;
    gboolean remotely_initiated = FALSE, valid_id;

    name = gst_pad_get_name(sctpdec_srcpad);
    sscanf(name, "src_%u", &sctp_stream_id);
    g_free(name);
    session_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(sctpdec), "session-id"));

    g_rw_lock_writer_lock(&priv->data_channels_rw_mutex);
    data_channel_info = (DataChannel *)g_hash_table_lookup(priv->data_channels,
        GUINT_TO_POINTER(sctp_stream_id));
    if (!data_channel_info) {
        remotely_initiated = TRUE;
        data_channel_info = g_new0(DataChannel, 1);
        data_channel_info->state = OWR_DATA_CHANNEL_STATE_CONNECTING;
        data_channel_info->session_id = session_id;
        data_channel_info->label = NULL;
        data_channel_info->protocol = NULL;
        data_channel_info->negotiated = FALSE;
        g_rw_lock_init(&data_channel_info->rw_mutex);
        g_hash_table_insert(priv->data_channels, GUINT_TO_POINTER(sctp_stream_id),
            (gpointer)data_channel_info);
    }
    g_rw_lock_writer_unlock(&priv->data_channels_rw_mutex);

    valid_id = is_valid_sctp_stream_id(transport_agent, session_id, sctp_stream_id,
        remotely_initiated);
    if (!data_channel_info->negotiated && !valid_id) {
        g_warning("Invalid stream_id");
        g_hash_table_remove(priv->data_channels, GUINT_TO_POINTER(sctp_stream_id));
        g_signal_emit_by_name(sctpdec, "reset-stream", sctp_stream_id);
        return;
    }

    name = g_strdup_printf("data_sink_%u", sctp_stream_id);
    data_sink = gst_element_factory_make("appsink", name);
    g_assert(data_sink);
    g_free(name);

    g_object_set(G_OBJECT(data_sink), "emit-signals", FALSE, "drop", FALSE, "sync", FALSE, "async",
        FALSE, "enable-last-sample", FALSE, NULL);

    g_assert(!data_channel_info->data_sink);

    callbacks.eos = NULL;
    callbacks.new_preroll = NULL;
    callbacks.new_sample = (GstFlowReturn (*)(GstAppSink *, gpointer)) new_data_callback;
    gst_app_sink_set_callbacks(GST_APP_SINK(data_sink), &callbacks,
        transport_agent, NULL);

    gst_object_ref(data_sink);
    data_channel_info->data_sink = data_sink;
    data_channel_info->id = sctp_stream_id;

    name = g_strdup_printf("receive-input-bin-%u", session_id);
    receive_input_bin = gst_bin_get_by_name(GST_BIN(priv->transport_bin), name);
    g_free(name);

    gst_bin_add(GST_BIN(receive_input_bin), data_sink);
    gst_object_unref(receive_input_bin);

    appsink_sinkpad = gst_element_get_static_pad(data_sink, "sink");

    link_ret = gst_pad_link(sctpdec_srcpad, appsink_sinkpad);
    g_assert(GST_PAD_LINK_SUCCESSFUL(link_ret));
    gst_object_unref(appsink_sinkpad);

    gst_element_sync_state_with_parent(data_sink);
}

static void sctpdec_pad_removed(GstElement *sctpdec, GstPad *sctpdec_srcpad,
    OwrTransportAgent *transport_agent)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    guint id;
    DataChannel *data_channel_info;
    GstPad *appsrc_srcpad, *sctpenc_sinkpad;
    gboolean already_closing = FALSE;
    gchar *name;
    OwrDataSession *data_session;
    OwrDataChannel *data_channel;
    GstElement *receive_bin, *sctpenc, *send_bin;

    name = gst_pad_get_name(sctpdec_srcpad);
    if (!sscanf(name, "src_%u", &id)) {
        g_free(name);
        return;
    }
    g_free(name);

    g_rw_lock_writer_lock(&priv->data_channels_rw_mutex);
    data_channel_info = g_hash_table_lookup(priv->data_channels, GUINT_TO_POINTER(id));
    g_assert(data_channel_info);
    if (data_channel_info->state == OWR_DATA_CHANNEL_STATE_CLOSING)
        already_closing = TRUE;
    else {
        data_channel_info->state = OWR_DATA_CHANNEL_STATE_CLOSING;
        data_session = OWR_DATA_SESSION(
            get_session(transport_agent, data_channel_info->session_id));
        g_assert(OWR_IS_DATA_SESSION(data_session));
        data_channel = _owr_data_session_get_datachannel(data_session,
            data_channel_info->id);
        g_assert(data_channel);
    }
    g_rw_lock_writer_unlock(&priv->data_channels_rw_mutex);

    if (already_closing)
        return;

    _owr_data_channel_set_ready_state(data_channel, OWR_DATA_CHANNEL_READY_STATE_CLOSING);

    receive_bin = GST_ELEMENT(gst_element_get_parent(data_channel_info->data_sink));
    gst_element_set_state(data_channel_info->data_sink, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(receive_bin), data_channel_info->data_sink);
    gst_object_unref(receive_bin);

    gst_object_unref(data_channel_info->data_sink);
    g_rw_lock_writer_lock(&data_channel_info->rw_mutex);
    data_channel_info->data_sink = NULL;
    g_rw_lock_writer_unlock(&data_channel_info->rw_mutex);

    appsrc_srcpad = gst_element_get_static_pad(data_channel_info->data_src, "src");
    sctpenc_sinkpad = gst_pad_get_peer(appsrc_srcpad);

    sctpenc = gst_pad_get_parent_element(sctpenc_sinkpad);
    send_bin = GST_ELEMENT(gst_element_get_parent(sctpenc));

    gst_element_set_state(data_channel_info->data_src, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(send_bin), data_channel_info->data_src);
    gst_object_unref(send_bin);

    gst_object_unref(data_channel_info->data_src);
    g_rw_lock_writer_lock(&data_channel_info->rw_mutex);
    data_channel_info->data_src = NULL;
    g_rw_lock_writer_unlock(&data_channel_info->rw_mutex);

    gst_pad_unlink(appsrc_srcpad, sctpenc_sinkpad);
    gst_element_release_request_pad(sctpenc, sctpenc_sinkpad);
    gst_object_unref(sctpenc);

    maybe_close_data_channel(transport_agent, data_channel_info);

    gst_object_unref(appsrc_srcpad);

    OWR_UNUSED(sctpdec);
}

static void on_sctp_association_established(GstElement *sctpenc, gboolean established,
    OwrTransportAgent *transport_agent)
{
    guint session_id;
    OwrDataSession *data_session;
    GList *data_channels, *it;

    transport_agent->priv->data_session_established = established;
    if (established) {
        g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "An SCTP association has been established");
        session_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(sctpenc), "session-id"));
        data_session = OWR_DATA_SESSION(get_session(transport_agent, session_id));
        g_assert(data_session);

        data_channels = _owr_data_session_get_datachannels(data_session);

        for (it = data_channels; it; it = it->next) {
            OwrDataChannel *data_channel;

            data_channel = OWR_DATA_CHANNEL(it->data);
            on_new_datachannel(transport_agent, data_channel, data_session);
        }

        g_list_free(data_channels);
    } else
        g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "An SCTP association has been terminated");
}

static GstFlowReturn new_data_callback(GstAppSink *appsink, OwrTransportAgent *transport_agent)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo info = {NULL, 0, NULL, 0, 0, {NULL}, {0}};
    guint16 ppid = 0;
    gpointer state = NULL;
    GstMeta *meta;
    const GstMetaInfo *meta_info = GST_SCTP_RECEIVE_META_INFO;
    gchar *name;
    guint sctp_stream_id;
    GstFlowReturn flow_ret = GST_FLOW_ERROR;

    sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
    g_return_val_if_fail(sample, GST_FLOW_ERROR);

    name = gst_element_get_name(GST_ELEMENT(appsink));
    sscanf(name, "data_sink_%u", &sctp_stream_id);
    g_free(name);

    buffer = gst_sample_get_buffer(sample);
    if (!buffer)
        goto end;

    if (!gst_buffer_map(buffer, &info, GST_MAP_READ))
        goto end;

    while ((meta = gst_buffer_iterate_meta(buffer, &state))) {
        if (meta->info->api == meta_info->api) {
            GstSctpReceiveMeta *sctp_receive_meta = (GstSctpReceiveMeta *)meta;
            ppid = sctp_receive_meta->ppid;
            break;
        }
    }

    switch (ppid) {
    case OWR_DATA_CHANNEL_PPID_CONTROL:
        handle_data_channel_control_message(transport_agent, info.data, info.size, sctp_stream_id);
        break;
    case OWR_DATA_CHANNEL_PPID_STRING:
        handle_data_channel_message(transport_agent, info.data, info.size, sctp_stream_id, FALSE);
        break;
    case OWR_DATA_CHANNEL_PPID_BINARY_PARTIAL:
        g_warning("PPID: DATA_CHANNEL_PPID_BINARY_PARTIAL - Deprecated - Not supported");
        /* TODO: Seems like chrome is using this.. maybe it should be supported to at least receive
         * this kind of messages even if it is deprecated?. */
        break;
    case OWR_DATA_CHANNEL_PPID_BINARY:
        handle_data_channel_message(transport_agent, info.data, info.size, sctp_stream_id, TRUE);
        break;
    case OWR_DATA_CHANNEL_PPID_STRING_PARTIAL:
        g_warning("PPID: DATA_CHANNEL_PPID_STRING_PARTIAL - Deprecated - Not supported");
        /* TODO: Seems like chrome is using this.. maybe it should be supported to at least receive
         * this kind of messages even if it is deprecated? */
        break;
    default:
        g_warning("Unsupported PPID received: %u", ppid);
        break;
    }

    flow_ret = GST_FLOW_OK;
end:
    if (info.data)
        gst_buffer_unmap(buffer, &info);
    gst_sample_unref(sample);
    return flow_ret;
}

static gboolean create_datachannel_appsrc(OwrTransportAgent *transport_agent,
    OwrDataChannel *data_channel)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    GstPadTemplate *pad_template;
    GstPad *sctpenc_sinkpad, *appsrc_srcpad;
    GstPadLinkReturn ret;
    gboolean sync_ok = TRUE;
    GstElement *data_src, *sctpenc, *send_output_bin;
    gchar *name;
    gboolean result = FALSE;
    GstCaps *caps;
    guint sctp_stream_id;
    DataChannel *data_channel_info;
    OwrDataSession *data_session;

    g_object_get(data_channel, "id", &sctp_stream_id, NULL);
    g_rw_lock_reader_lock(&priv->data_channels_rw_mutex);
    data_channel_info = (DataChannel *)g_hash_table_lookup(priv->data_channels,
        GUINT_TO_POINTER(sctp_stream_id));
    g_rw_lock_reader_unlock(&priv->data_channels_rw_mutex);
    g_assert(data_channel_info);

    data_session = OWR_DATA_SESSION(get_session(transport_agent, data_channel_info->session_id));
    g_return_val_if_fail(data_session, FALSE);

    name = g_strdup_printf("send-output-bin-%u", data_channel_info->session_id);
    send_output_bin = gst_bin_get_by_name(GST_BIN(priv->transport_bin), name);
    g_return_val_if_fail(send_output_bin, FALSE);
    g_free(name);

    name = _owr_data_session_get_encoder_name(data_session);
    sctpenc = gst_bin_get_by_name(GST_BIN(send_output_bin), name);
    g_free(name);
    g_return_val_if_fail(sctpenc, FALSE);

    pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(sctpenc),
        "sink_%u");
    g_assert(pad_template);

    caps = _owr_data_channel_create_caps(data_channel);
    name = g_strdup_printf("sink_%u", sctp_stream_id);
    sctpenc_sinkpad = gst_element_request_pad(sctpenc, pad_template, name, caps);
    g_free(name);
    if (!sctpenc_sinkpad) {
        /* This should never happend because of other checks */
        g_warning("Could not create channel. Channel probably already exist.");
        goto end;
    }

    name = g_strdup_printf("datasrc_%u", sctp_stream_id);
    data_src = gst_element_factory_make("appsrc", name);
    g_free(name);
    g_assert(data_src);
    g_object_set(data_src, "caps", caps, "is-live", TRUE, "min-latency", G_GINT64_CONSTANT(0),
        "do-timestamp", TRUE, NULL);

    data_channel_info->data_src = data_src;
    g_object_ref(data_src);
    g_object_set(data_src, "max-bytes", 0, "emit-signals", FALSE, NULL);

    gst_bin_add(GST_BIN(send_output_bin), data_src);
    appsrc_srcpad = gst_element_get_static_pad(data_src, "src");

    ret = gst_pad_link(appsrc_srcpad, sctpenc_sinkpad);
    gst_object_unref(sctpenc_sinkpad);
    gst_object_unref(appsrc_srcpad);
    g_warn_if_fail(GST_PAD_LINK_SUCCESSFUL(ret));
    sync_ok &= gst_element_sync_state_with_parent(data_src);
    g_warn_if_fail(sync_ok);
    if (!GST_PAD_LINK_SUCCESSFUL(ret) || !sync_ok)
        goto end;

    result = TRUE;
end:
    gst_object_unref(sctpenc);
    gst_object_unref(send_output_bin);
    return result;
}

static gboolean is_valid_sctp_stream_id(OwrTransportAgent *transport_agent, guint32 session_id,
    guint16 sctp_stream_id, gboolean remotly_initiated)
{
    gboolean is_client = FALSE;
    gboolean result = TRUE;
    OwrSession *session;

    session = get_session(transport_agent, session_id);
    g_return_val_if_fail(session, FALSE);
    g_object_get(session, "dtls-client-mode", &is_client, NULL);
    if ((sctp_stream_id == 65535) || (!(sctp_stream_id % 2) && !is_client)
        || ((sctp_stream_id % 2 == 1) && is_client)) {
        result = FALSE;
    }
    return remotly_initiated ? !result : result;
}


static guint8 * create_datachannel_open_request(OwrDataChannelChannelType channel_type,
    guint32 reliability_param, guint16 priority, const gchar *label, guint16 label_len,
    const gchar *protocol, guint16 protocol_len, guint32 *buf_size)
{
    guint8 *buf;

    *buf_size = 12 + label_len + protocol_len;
    buf = g_malloc(*buf_size);
    GST_WRITE_UINT8(buf, OWR_DATA_CHANNEL_MESSAGE_TYPE_OPEN_REQUEST);
    GST_WRITE_UINT8(buf + 1, channel_type);
    GST_WRITE_UINT16_BE(buf + 2, priority);
    GST_WRITE_UINT32_BE(buf + 4, reliability_param);
    GST_WRITE_UINT16_BE(buf + 8, label_len);
    GST_WRITE_UINT16_BE(buf + 10, protocol_len);
    memcpy(buf + 12, label, label_len);
    memcpy(buf + 12 + label_len, protocol, protocol_len);

    return buf;
}

static guint8 * create_datachannel_ack(guint32 *buf_size)
{
    guint8 *buf;

    buf = g_malloc(1);
    GST_WRITE_UINT8(buf, OWR_DATA_CHANNEL_MESSAGE_TYPE_ACK);
    *buf_size = 1;
    return buf;
}

static void handle_data_channel_control_message(OwrTransportAgent *transport_agent, guint8 *data,
    guint32 size, guint16 sctp_stream_id)
{
    OwrDataChannelMessageType message_type;

    if (!size) {
        g_warning("Invalid size of data channel control message: %u, expected > 0", size);
        return;
    }

    message_type = GST_READ_UINT8(data);
    if (message_type == OWR_DATA_CHANNEL_MESSAGE_TYPE_OPEN_REQUEST)
        handle_data_channel_open_request(transport_agent, data, size, sctp_stream_id);
    else if (message_type == OWR_DATA_CHANNEL_MESSAGE_TYPE_ACK)
        handle_data_channel_ack(transport_agent, data, size, sctp_stream_id);
    else
        g_warning("Received invalid data channel control message");
}

static void handle_data_channel_open_request(OwrTransportAgent *transport_agent, guint8 *data,
    guint32 size, guint16 sctp_stream_id)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    OwrDataChannelChannelType channel_type;
    guint32 reliability_param;
    guint16 priority;
    DataChannel *data_channel_info;
    guint label_len, protocol_len;
    OwrDataSession *data_session;
    GHashTable *args;

    if (size < 12) {
        g_warning("Invalid size of data channel control message: %u, expected > 12", size);
        return;
    }

    g_rw_lock_reader_lock(&priv->data_channels_rw_mutex);
    data_channel_info = (DataChannel *)g_hash_table_lookup(priv->data_channels,
        GUINT_TO_POINTER(sctp_stream_id));
    g_rw_lock_reader_unlock(&priv->data_channels_rw_mutex);

    channel_type = GST_READ_UINT8(data + 1);
    priority = GST_READ_UINT16_BE(data + 2);
    reliability_param = GST_READ_UINT32_BE(data + 4);
    label_len = GST_READ_UINT16_BE(data + 8);
    protocol_len = GST_READ_UINT16_BE(data + 10);

    if (size != (guint32)(12 + label_len + protocol_len)) {
        g_warning("Invalid size of data channel control message: %u, expected %u", size,
            (12 + label_len + protocol_len));
    }

    data_channel_info->label = g_strndup((const gchar *) data + 12, label_len);
    data_channel_info->protocol= g_strndup((const gchar *) data + 12 + label_len, protocol_len);

    OWR_UNUSED(priority);
    data_channel_info->negotiated = FALSE;
    data_channel_info->ordered = !(channel_type & 0x80);
    data_channel_info->max_packet_life_time = -1;
    data_channel_info->max_packet_retransmits = -1;
    data_channel_info->ctrl_bytes_sent = 0;
    if (channel_type == OWR_DATA_CHANNEL_CHANNEL_TYPE_PARTIAL_RELIABLE_REMIX
        || channel_type == OWR_DATA_CHANNEL_CHANNEL_TYPE_PARTIAL_RELIABLE_REMIX_UNORDERED)
        data_channel_info->max_packet_retransmits = reliability_param;
    else if (channel_type == OWR_DATA_CHANNEL_CHANNEL_TYPE_PARTIAL_RELIABLE_TIMED
        || channel_type == OWR_DATA_CHANNEL_CHANNEL_TYPE_PARTIAL_RELIABLE_TIMED_UNORDERED)
        data_channel_info->max_packet_life_time = reliability_param;

    g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Received data channel open request for data channel (%u)"
        ":\nordered = %u\nmax_packets_life_time = %d\nmax_packet_retransmits = %d\nprotocols = %s"
        "\nnegotiated=%u\nlabel= %s\n", data_channel_info->id, data_channel_info->ordered,
        data_channel_info->max_packet_life_time, data_channel_info->max_packet_retransmits,
        data_channel_info->protocol, data_channel_info->negotiated,
        data_channel_info->label);

    data_session = OWR_DATA_SESSION(get_session(transport_agent, data_channel_info->session_id));
    g_object_ref(data_session);
    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(data_session));
    g_hash_table_insert(args, "session", data_session);
    g_hash_table_insert(args, "ordered", GUINT_TO_POINTER(data_channel_info->ordered));
    g_hash_table_insert(args, "max_packet_life_time",
        GINT_TO_POINTER(data_channel_info->max_packet_life_time));
    g_hash_table_insert(args, "max_packet_retransmits",
        GINT_TO_POINTER(data_channel_info->max_packet_retransmits));
    g_hash_table_insert(args, "protocol", g_strdup(data_channel_info->protocol));
    g_hash_table_insert(args, "negotiated", GUINT_TO_POINTER(data_channel_info->negotiated));
    g_hash_table_insert(args, "id", GUINT_TO_POINTER(data_channel_info->id));
    g_hash_table_insert(args, "label", g_strdup(data_channel_info->label));

    _owr_schedule_with_hash_table((GSourceFunc)emit_data_channel_requested, args);
}

static gboolean emit_data_channel_requested(GHashTable *args)
{
    OwrDataSession *data_session;
    gboolean ordered, negotiated;
    guint id;
    gint max_packet_life_time, max_packet_retransmits;
    gchar *label, *protocol;

    data_session = g_hash_table_lookup(args, "session");
    ordered = GPOINTER_TO_UINT(g_hash_table_lookup(args, "ordered"));
    max_packet_life_time = GPOINTER_TO_INT(g_hash_table_lookup(args, "max_packet_life_time"));
    max_packet_retransmits = GPOINTER_TO_INT(g_hash_table_lookup(args, "max_packet_retransmits"));
    protocol = g_hash_table_lookup(args, "protocol");
    negotiated = GPOINTER_TO_UINT(g_hash_table_lookup(args, "negotiated"));
    id = GPOINTER_TO_UINT(g_hash_table_lookup(args, "id"));
    label = g_hash_table_lookup(args, "label");

    g_signal_emit_by_name(data_session, "on-data-channel-requested", ordered, max_packet_life_time,
        max_packet_retransmits, protocol, negotiated, id, label);
    g_object_unref(data_session);
    g_free(label);
    g_free(protocol);
    g_hash_table_unref(args);

    return FALSE;
}

static void handle_data_channel_ack(OwrTransportAgent *transport_agent, guint8 *data, guint32 size,
    guint16 sctp_stream_id)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    DataChannel *data_channel_info;
    OwrDataSession *data_session;
    OwrDataChannel *data_channel;
    OWR_UNUSED(data);
    OWR_UNUSED(size);

    g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "Received ACK for data channel %u\n", sctp_stream_id);
    g_rw_lock_reader_lock(&priv->data_channels_rw_mutex);
    data_channel_info = g_hash_table_lookup(priv->data_channels, GUINT_TO_POINTER(sctp_stream_id));
    g_assert(data_channel_info);
    g_rw_lock_reader_unlock(&priv->data_channels_rw_mutex);

    g_rw_lock_writer_lock(&data_channel_info->rw_mutex);
    data_session = OWR_DATA_SESSION(get_session(transport_agent, data_channel_info->session_id));
    g_assert(OWR_IS_DATA_SESSION(data_session));
    data_channel = _owr_data_session_get_datachannel(data_session, data_channel_info->id);
    g_assert(OWR_IS_DATA_CHANNEL(data_channel));
    data_channel_info->state = OWR_DATA_CHANNEL_STATE_OPEN;
    g_rw_lock_writer_unlock(&data_channel_info->rw_mutex);

    _owr_data_channel_set_ready_state(data_channel, OWR_DATA_CHANNEL_READY_STATE_OPEN);
}

static void handle_data_channel_message(OwrTransportAgent *transport_agent, guint8 *data,
    guint32 size, guint16 sctp_stream_id, gboolean is_binary)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    DataChannel *data_channel_info;
    OwrDataSession *data_session;
    OwrDataChannel *owr_data_channel;
    gchar *message;
    GHashTable *args;

    g_rw_lock_reader_lock(&priv->data_channels_rw_mutex);
    data_channel_info = (DataChannel *)g_hash_table_lookup(priv->data_channels,
        GUINT_TO_POINTER(sctp_stream_id));
    g_rw_lock_reader_unlock(&priv->data_channels_rw_mutex);
    g_assert(data_channel_info);

    g_rw_lock_reader_lock(&data_channel_info->rw_mutex);
    if (data_channel_info->state != OWR_DATA_CHANNEL_STATE_OPEN) {
        /* This should never happen */
        g_critical("Received message before datachannel was established.");
        goto end;
    }

    data_session = OWR_DATA_SESSION(get_session(transport_agent, data_channel_info->session_id));
    owr_data_channel = _owr_data_session_get_datachannel(data_session, data_channel_info->id);
    g_assert(owr_data_channel);
    g_rw_lock_reader_unlock(&data_channel_info->rw_mutex);

    message = g_malloc(size + (is_binary ? 0 : 1));
    memcpy(message, data, size);

    if (!is_binary)
        message[size] = '\0';

    g_object_ref(owr_data_channel);
    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(owr_data_channel));
    g_hash_table_insert(args, "data_channel", owr_data_channel);
    g_hash_table_insert(args, "is_binary", GUINT_TO_POINTER(is_binary));
    g_hash_table_insert(args, "message", message);
    g_hash_table_insert(args, "size", GUINT_TO_POINTER(size));
    _owr_schedule_with_hash_table((GSourceFunc)emit_incoming_data, args);


end:
    return;
}

static gboolean emit_incoming_data(GHashTable *args)
{
    OwrDataChannel *owr_data_channel;
    gboolean is_binary;
    gchar *message;
    guint size;

    owr_data_channel = g_hash_table_lookup(args, "data_channel");
    is_binary = GPOINTER_TO_UINT(g_hash_table_lookup(args, "is_binary"));
    message = g_hash_table_lookup(args, "message");
    size = GPOINTER_TO_UINT(g_hash_table_lookup(args, "size"));

    if (is_binary)
        g_signal_emit_by_name(owr_data_channel, "on-binary-data", message, size);
    else
        g_signal_emit_by_name(owr_data_channel, "on-data", message);

    g_object_unref(owr_data_channel);
    g_hash_table_unref(args);
    g_free(message);
    return FALSE;
}

static void on_new_datachannel(OwrTransportAgent *transport_agent, OwrDataChannel *data_channel,
    OwrDataSession *data_session)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    DataChannel *data_channel_info;
    guint id;
    gboolean negotiated, remote_initiated;

    if (!priv->data_session_established)
        return;

    g_object_get(data_channel, "id", &id, NULL);
    g_rw_lock_reader_lock(&priv->data_channels_rw_mutex);
    data_channel_info = (DataChannel *)g_hash_table_lookup(priv->data_channels,
        GUINT_TO_POINTER(id));
    g_rw_lock_reader_unlock(&priv->data_channels_rw_mutex);

    remote_initiated = !!data_channel_info;
    if (!remote_initiated) {
        guint session_id = get_stream_id(transport_agent, OWR_SESSION(data_session));
        if (!create_datachannel(transport_agent, session_id, data_channel)) {
            g_warning("Failed to create new datachannel");
            goto end;
        }
    } else {
        /* This is when application has gotten a new datachannel from other side and it has been
         * added to the session from the application. */
        gboolean ordered, negotiated;
        gint max_packet_life_time, max_packet_retransmits;
        gchar *protocol, *label;

        g_object_get(data_channel, "ordered", &ordered, "max-packet-life-time", &max_packet_life_time,
            "max-retransmits", &max_packet_retransmits, "protocol", &protocol, "negotiated", &negotiated,
            "label", &label, "id", &id, NULL);
        /* It should not bee needed to get the id property again, but for some reason it is.. */

        /* Check so that the channel is identical to the requested one */
        g_rw_lock_reader_lock(&data_channel_info->rw_mutex);
        if (ordered != data_channel_info->ordered
            || negotiated != data_channel_info->negotiated
            || max_packet_life_time != data_channel_info->max_packet_life_time
            || max_packet_retransmits != data_channel_info->max_packet_retransmits
            || g_strcmp0(protocol, data_channel_info->protocol)
            || g_strcmp0(label, data_channel_info->label)
            || id != data_channel_info->id) {
            g_critical("The added datachannel does not match the remote datachannel");
            g_free(protocol);
            g_free(label);
            goto end;
        }
        g_free(protocol);
        g_free(label);

        g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "New data channel (%u) added\n", id);
        g_rw_lock_reader_unlock(&data_channel_info->rw_mutex);
    }

    _owr_data_channel_set_on_send(data_channel, g_cclosure_new_object_swap(
        G_CALLBACK(on_datachannel_send), G_OBJECT(transport_agent)));
    _owr_data_channel_set_on_request_bytes_sent(data_channel,
        g_cclosure_new_object_swap(G_CALLBACK(on_datachannel_request_bytes_sent),
        G_OBJECT(transport_agent)));
    _owr_data_channel_set_on_close(data_channel, g_cclosure_new_object_swap(
        G_CALLBACK(on_datachannel_close), G_OBJECT(transport_agent)));

    g_object_get(data_channel, "negotiated", &negotiated, NULL);
    if (negotiated || remote_initiated) {
        if (remote_initiated)
            complete_data_channel_and_ack(transport_agent, data_channel);

        g_object_get(data_channel, "id", &id, NULL);
        g_rw_lock_reader_lock(&priv->data_channels_rw_mutex);
        data_channel_info = (DataChannel *)g_hash_table_lookup(priv->data_channels,
            GUINT_TO_POINTER(id));
        g_rw_lock_reader_unlock(&priv->data_channels_rw_mutex);

        g_rw_lock_writer_lock(&data_channel_info->rw_mutex);
        data_channel_info->state = OWR_DATA_CHANNEL_STATE_OPEN;
        g_rw_lock_writer_unlock(&data_channel_info->rw_mutex);

        _owr_data_channel_set_ready_state(data_channel, OWR_DATA_CHANNEL_READY_STATE_OPEN);
    }

end:
    return;
}

static void complete_data_channel_and_ack(OwrTransportAgent *transport_agent,
    OwrDataChannel *data_channel)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    guint8 *ackbuf;
    GstBuffer *gstbuf;
    guint32 buf_size;
    GstFlowReturn flow_ret;
    guint id;
    DataChannel *data_channel_info;

    if (!create_datachannel_appsrc(transport_agent, data_channel))
        g_warning("Could not create appsrc");

    g_object_get(data_channel, "id", &id, NULL);
    g_rw_lock_reader_lock(&priv->data_channels_rw_mutex);
    data_channel_info = (DataChannel *)g_hash_table_lookup(priv->data_channels,
        GUINT_TO_POINTER(id));
    g_rw_lock_reader_unlock(&priv->data_channels_rw_mutex);

    /* Return ACK */
    ackbuf = create_datachannel_ack(&buf_size);
    gstbuf = gst_buffer_new_wrapped(ackbuf, buf_size);
    gst_sctp_buffer_add_send_meta(gstbuf, OWR_DATA_CHANNEL_PPID_CONTROL, TRUE,
        GST_SCTP_SEND_META_PARTIAL_RELIABILITY_NONE, 0);
    data_channel_info->ctrl_bytes_sent += buf_size;
    flow_ret = gst_app_src_push_buffer(GST_APP_SRC(data_channel_info->data_src), gstbuf);
    if (flow_ret != GST_FLOW_OK)
        g_critical("Failed to push data buffer: %s", gst_flow_get_name(flow_ret));
}

static void on_datachannel_send(OwrTransportAgent *transport_agent, guint8 *data, guint len,
    gboolean is_binary, OwrDataChannel *data_channel)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;

    DataChannel *data_channel_info;
    guint id;
    GstBuffer *gstbuf;
    OwrDataChannelPPID ppid;
    GstSctpSendMetaPartiallyReliability pr;
    guint32 pr_param;
    GstFlowReturn flow_ret;
    GstElement *data_src;

    g_return_if_fail(data);
    g_return_if_fail(data_channel);

    g_object_get(data_channel, "id", &id, NULL);
    g_rw_lock_reader_lock(&priv->data_channels_rw_mutex);
    data_channel_info = g_hash_table_lookup(priv->data_channels, GUINT_TO_POINTER(id));
    g_rw_lock_reader_unlock(&priv->data_channels_rw_mutex);
    gstbuf = gst_buffer_new_wrapped(data, len);

    g_rw_lock_reader_lock(&data_channel_info->rw_mutex);
    ppid = is_binary ? OWR_DATA_CHANNEL_PPID_BINARY : OWR_DATA_CHANNEL_PPID_STRING;
    pr = GST_SCTP_SEND_META_PARTIAL_RELIABILITY_NONE;
    pr_param = 0;
    if (data_channel_info->max_packet_life_time == -1
        && data_channel_info->max_packet_retransmits == -1) {
        pr = GST_SCTP_SEND_META_PARTIAL_RELIABILITY_NONE;
        pr_param = 0;
    } else if (data_channel_info->max_packet_life_time != -1) {
        pr = GST_SCTP_SEND_META_PARTIAL_RELIABILITY_TTL;
        pr_param = data_channel_info->max_packet_life_time;
    } else if (data_channel_info->max_packet_retransmits != -1) {
        pr = GST_SCTP_SEND_META_PARTIAL_RELIABILITY_RTX;
        pr_param = data_channel_info->max_packet_retransmits;
    }

    gst_sctp_buffer_add_send_meta(gstbuf, ppid, data_channel_info->ordered,
        pr, pr_param);
    data_src = data_channel_info->data_src;
    g_rw_lock_reader_unlock(&data_channel_info->rw_mutex);
    flow_ret = gst_app_src_push_buffer(GST_APP_SRC(data_src), gstbuf);
    g_warn_if_fail(flow_ret == GST_FLOW_OK);
}

static guint64 on_datachannel_request_bytes_sent(OwrTransportAgent *transport_agent,
    OwrDataChannel *data_channel)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    guint data_channel_id;
    guint64 bytes_sent = 0;
    GstElement *sctpenc, *send_output_bin;
    DataChannel *data_channel_info;
    OwrDataSession *data_session;
    gchar *name;
    guint ctrl_bytes_sent, session_id;

    g_object_get(data_channel, "id", &data_channel_id, NULL);
    g_rw_lock_reader_lock(&priv->data_channels_rw_mutex);
    data_channel_info = (DataChannel *)g_hash_table_lookup(priv->data_channels,
        GUINT_TO_POINTER(data_channel_id));
    g_rw_lock_reader_unlock(&priv->data_channels_rw_mutex);

    g_rw_lock_reader_lock(&data_channel_info->rw_mutex);
    ctrl_bytes_sent = data_channel_info->ctrl_bytes_sent;
    session_id = data_channel_info->session_id;
    g_rw_lock_reader_unlock(&data_channel_info->rw_mutex);

    name = g_strdup_printf("send-output-bin-%u", session_id);
    send_output_bin = gst_bin_get_by_name(GST_BIN(priv->transport_bin), name);
    g_free(name);

    data_session = OWR_DATA_SESSION(get_session(transport_agent, session_id));
    name = _owr_data_session_get_encoder_name(data_session);
    sctpenc = gst_bin_get_by_name(GST_BIN(send_output_bin), name);
    g_free(name);
    g_assert(sctpenc);
    g_signal_emit_by_name(sctpenc, "bytes-sent", data_channel_id, &bytes_sent);

    gst_object_unref(send_output_bin);
    gst_object_unref(sctpenc);

    return bytes_sent - ctrl_bytes_sent;
}

static void maybe_close_data_channel(OwrTransportAgent *transport_agent,
    DataChannel *data_channel_info)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    gboolean close = FALSE;

    g_rw_lock_writer_lock(&data_channel_info->rw_mutex);
    close = (!data_channel_info->data_sink && !data_channel_info->data_src);
    if (close) {
        OwrDataSession *data_session;
        OwrDataChannel *data_channel;

        data_session = OWR_DATA_SESSION(get_session(transport_agent,
            data_channel_info->session_id));
        data_channel = _owr_data_session_get_datachannel(data_session, data_channel_info->id);
        _owr_data_channel_set_ready_state(data_channel, OWR_DATA_CHANNEL_READY_STATE_CLOSED);
        g_hash_table_steal(priv->data_channels, GUINT_TO_POINTER(data_channel_info->id));
        data_channel_free(data_channel_info);
    }
    g_rw_lock_writer_unlock(&data_channel_info->rw_mutex);
}

static void on_datachannel_close(OwrTransportAgent *transport_agent,
    OwrDataChannel *data_channel)
{
    OwrTransportAgentPrivate *priv = transport_agent->priv;
    guint id;
    DataChannel *data_channel_info;
    GstPad *appsink_sinkpad, *appsrc_srcpad, *sctpdec_srcpad;
    GstPad *sctpenc_sinkpad;
    GstElement *sctpdec, *sctpenc, *send_bin, *receive_bin;

    _owr_data_channel_set_ready_state(data_channel, OWR_DATA_CHANNEL_READY_STATE_CLOSING);

    g_object_get(data_channel, "id", &id, NULL);
    g_rw_lock_writer_lock(&priv->data_channels_rw_mutex);
    data_channel_info = g_hash_table_lookup(priv->data_channels, GUINT_TO_POINTER(id));
    g_assert(data_channel_info);
    data_channel_info->state = OWR_DATA_CHANNEL_STATE_CLOSING;
    g_rw_lock_writer_unlock(&priv->data_channels_rw_mutex);

    appsink_sinkpad = gst_element_get_static_pad(data_channel_info->data_sink, "sink");
    appsrc_srcpad = gst_element_get_static_pad(data_channel_info->data_src, "src");
    sctpdec_srcpad = gst_pad_get_peer(appsink_sinkpad);
    sctpenc_sinkpad = gst_pad_get_peer(appsrc_srcpad);

    /* Remove decoder part */
    sctpdec = gst_pad_get_parent_element(sctpdec_srcpad);
    receive_bin = GST_ELEMENT(gst_element_get_parent(sctpdec));

    gst_element_set_state(data_channel_info->data_sink, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(receive_bin), data_channel_info->data_sink);
    gst_object_unref(receive_bin);

    g_rw_lock_writer_lock(&data_channel_info->rw_mutex);
    gst_object_unref(data_channel_info->data_sink);
    data_channel_info->data_sink = NULL;
    g_rw_lock_writer_unlock(&data_channel_info->rw_mutex);

    gst_pad_unlink(sctpdec_srcpad, appsink_sinkpad);
    g_signal_emit_by_name(sctpdec, "reset-stream", GUINT_TO_POINTER(id));
    gst_object_unref(sctpdec);

    /* Remove encoder part */
    sctpenc = gst_pad_get_parent_element(sctpenc_sinkpad);
    send_bin = GST_ELEMENT(gst_element_get_parent(sctpenc));

    gst_element_set_state(data_channel_info->data_src, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(send_bin), data_channel_info->data_src);
    gst_object_unref(send_bin);

    g_rw_lock_writer_lock(&data_channel_info->rw_mutex);
    gst_object_unref(data_channel_info->data_src);
    data_channel_info->data_src = NULL;
    g_rw_lock_writer_unlock(&data_channel_info->rw_mutex);

    gst_pad_unlink(appsrc_srcpad, sctpenc_sinkpad);
    gst_element_release_request_pad(sctpenc, sctpenc_sinkpad);
    gst_object_unref(sctpenc);

    maybe_close_data_channel(transport_agent, data_channel_info);

    gst_object_unref(appsrc_srcpad);
    gst_object_unref(appsink_sinkpad);
    gst_object_unref(sctpdec_srcpad);
    gst_object_unref(sctpenc_sinkpad);
}

static gboolean is_same_session(gpointer stream_id_p, OwrSession *session1, OwrSession *session2)
{
    OWR_UNUSED(stream_id_p);
    return session1 == session2;
}

gchar * owr_transport_agent_get_dot_data(OwrTransportAgent *transport_agent)
{
    g_return_val_if_fail(OWR_IS_TRANSPORT_AGENT(transport_agent), NULL);
    g_return_val_if_fail(transport_agent->priv->pipeline, NULL);

#if GST_CHECK_VERSION(1, 5, 0)
    return gst_debug_bin_to_dot_data(GST_BIN(transport_agent->priv->pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
#else
    return g_strdup("");
#endif
}


static void on_feedback_rtcp(GObject *session, guint type, guint fbtype, guint sender_ssrc,
    guint media_ssrc, GstBuffer *fci, OwrTransportAgent *transport_agent)
{
    g_return_if_fail(session);
    g_return_if_fail(transport_agent);

    OWR_UNUSED(sender_ssrc);

    if (type == GST_RTCP_TYPE_RTPFB && fbtype == GST_RTCP_RTPFB_TYPE_SCREAM) {
        GstElement *send_output_bin, *scream_queue = NULL;
        GstMapInfo info = {NULL, 0, NULL, 0, 0, {0}, {0}}; /*GST_MAP_INFO_INIT;*/
        guint session_id = GPOINTER_TO_UINT(g_object_get_data(session, "session_id"));

        gchar *name = g_strdup_printf("send-output-bin-%u", session_id);
        send_output_bin = gst_bin_get_by_name(GST_BIN(transport_agent->priv->transport_bin), name);
        g_free(name);
        scream_queue = gst_bin_get_by_name(GST_BIN(send_output_bin), "screamqueue");
        gst_object_unref(send_output_bin);

        /* Read feedback from FCI */
        if (gst_buffer_map(fci, &info, GST_MAP_READ)) {
            guint32 timestamp;
            guint16 highest_seq;
            guint8 *fci_buf, n_loss, n_ecn;
            gboolean qbit = FALSE;

            fci_buf = info.data;
            highest_seq = GST_READ_UINT16_BE(fci_buf);
            n_loss = GST_READ_UINT8(fci_buf + 2);
            n_ecn = GST_READ_UINT8(fci_buf + 3);
            timestamp = GST_READ_UINT32_BE(fci_buf + 4);
            /* TODO: Fix qbit */

            gst_buffer_unmap(fci, &info);
            g_signal_emit_by_name(scream_queue, "incoming-feedback", media_ssrc, timestamp, highest_seq, n_loss, n_ecn, qbit);
        }
        gst_object_unref(scream_queue);
    }
}


static GstPadProbeReturn probe_save_ts(GstPad *srcpad, GstPadProbeInfo *info, void *user_data)
{
    GstBuffer *buffer = NULL;
    OWR_UNUSED(user_data);
    OWR_UNUSED(srcpad);

    buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    _owr_buffer_add_arrival_time_meta(buffer, GST_BUFFER_DTS(buffer));

    return GST_PAD_PROBE_OK;
}



static gint compare_rtcp_scream(GHashTable *a, GHashTable *b)
{
    guint session_id_a, session_id_b, ssrc_a, ssrc_b;

    session_id_a = GPOINTER_TO_UINT(g_hash_table_lookup(a, "session-id"));
    ssrc_a = GPOINTER_TO_UINT(g_hash_table_lookup(a, "ssrc"));
    session_id_b = GPOINTER_TO_UINT(g_hash_table_lookup(b, "session-id"));
    ssrc_b = GPOINTER_TO_UINT(g_hash_table_lookup(b, "ssrc"));

    if (session_id_a == session_id_b && ssrc_a == ssrc_b)
        return 0;
    return -1;
}


static GstPadProbeReturn probe_rtp_info(GstPad *srcpad, GstPadProbeInfo *info, ScreamRx *scream_rx)
{
    GstBuffer *buffer = NULL;
    GstRTPBuffer rtp_buf = GST_RTP_BUFFER_INIT;
    guint64 arrival_time = GST_CLOCK_TIME_NONE;
    OwrTransportAgent *transport_agent = NULL;
    OwrTransportAgentPrivate *priv = NULL;
    guint session_id = 0;
    guint8 pt = 0;
    gboolean rtp_mapped = FALSE;
    GObject *rtp_session = NULL;

    transport_agent = scream_rx->transport_agent;
    session_id = scream_rx->session_id;

    g_assert(transport_agent);
    priv = transport_agent->priv;

    buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    if (scream_rx->rtx_pt == -2 || scream_rx->adapt) {
        if (!gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp_buf)) {
            g_warning("Failed to map RTP buffer");
            goto end;
        }

        rtp_mapped = TRUE;
        pt = gst_rtp_buffer_get_payload_type(&rtp_buf);
    }

    g_signal_emit_by_name(priv->rtpbin, "get-internal-session", session_id, &rtp_session);

    if (G_UNLIKELY(scream_rx->rtx_pt == -2)) {
        OwrMediaSession *media_session;
        OwrPayload *rx_payload;
        OwrAdaptationType adapt_type;
        media_session = OWR_MEDIA_SESSION(get_session(transport_agent, session_id));
        rx_payload = _owr_media_session_get_receive_payload(media_session, pt);
        g_object_get(rx_payload, "rtx-payload-type", &scream_rx->rtx_pt,
            "adaptation", &adapt_type, NULL);
        scream_rx->adapt = (adapt_type == OWR_ADAPTATION_TYPE_SCREAM);
        g_object_unref(media_session);
        g_object_unref(rx_payload);

	g_object_set(rtp_session, "rtcp-reduced-size", TRUE, NULL);
    }

    OWR_UNUSED(srcpad);

    if (scream_rx->adapt) {
        GstMeta *meta;
        const GstMetaInfo *meta_info = OWR_ARRIVAL_TIME_META_INFO;
        GHashTable *rtcp_info;
        guint16 seq = 0;
        guint ssrc = 0;
        GList *it;
        guint diff, tmp_highest_seq, tmp_seq;

        if ((meta = gst_buffer_get_meta(buffer, meta_info->api))) {
            OwrArrivalTimeMeta *atmeta = (OwrArrivalTimeMeta *) meta;
            arrival_time = atmeta->arrival_time;
        }

        if (arrival_time == GST_CLOCK_TIME_NONE) {
            GST_WARNING("No arrival time available for RTP packet");
            goto end;
        }

        ssrc = gst_rtp_buffer_get_ssrc(&rtp_buf);
        seq = gst_rtp_buffer_get_seq(&rtp_buf);

        if (pt == scream_rx->rtx_pt && scream_rx->adapt)
            goto end;

        tmp_seq = seq;
        tmp_highest_seq = scream_rx->highest_seq;
        if (!scream_rx->highest_seq && !scream_rx->ack_vec) { /* Initial condition */
            scream_rx->highest_seq = seq;
            tmp_highest_seq = scream_rx->highest_seq;
        } else if ((seq < scream_rx->highest_seq) && (scream_rx->highest_seq - seq > 20000))
            tmp_seq = (guint64)seq + 65536;
        else if ((seq > scream_rx->highest_seq) && (seq - scream_rx->highest_seq > 20000))
            tmp_highest_seq += 65536;

        /* in order */
        if (tmp_seq >= tmp_highest_seq) {
            diff = tmp_seq - tmp_highest_seq;
            if (diff) {
                if (diff >= 16)
                    scream_rx->ack_vec = 0x0000; /* ack_vec can be reduced to guint16, initialize with 0xffff */
                else {
                    // Fill with potential zeros
                    scream_rx->ack_vec = scream_rx->ack_vec >> diff;
                    // Add previous highest seq nr to ack vector
                    scream_rx->ack_vec = scream_rx->ack_vec | (1 << (16 - diff));
                }
            }

            scream_rx->highest_seq = seq;
        } else { /* out of order */
            diff = tmp_highest_seq - tmp_seq;
            if (diff < 16)
                scream_rx->ack_vec = scream_rx->ack_vec | (1 << (16 - diff));
        }
        if (!(scream_rx->ack_vec & (1 << (16-5)))) {
            /*
            * Detect lost packets with a little grace time to cater
            * for out-of-order delivery
            */
            scream_rx->n_loss++; /* n_loss is a guint8 , initialize to 0 */
        }

        /*
        * ECN is not implemented but we add this just to not forget it
        * in case ECN flies some day
        */
        scream_rx->n_ecn = 0;
        scream_rx->last_feedback_wallclock = (guint32)(arrival_time / 1000000);
        rtcp_info = g_hash_table_new(g_str_hash, g_str_equal);
        g_hash_table_insert(rtcp_info, "pt", GUINT_TO_POINTER(GST_RTCP_TYPE_RTPFB));
        g_hash_table_insert(rtcp_info, "fmt", GUINT_TO_POINTER(GST_RTCP_RTPFB_TYPE_SCREAM));
        g_hash_table_insert(rtcp_info, "ssrc", GUINT_TO_POINTER(ssrc));
        g_hash_table_insert(rtcp_info, "last-feedback-wallclock",
            GUINT_TO_POINTER(scream_rx->last_feedback_wallclock));
        g_hash_table_insert(rtcp_info, "highest-seq",
            GUINT_TO_POINTER(scream_rx->highest_seq));
        g_hash_table_insert(rtcp_info, "n-loss", GUINT_TO_POINTER(scream_rx->n_loss));
        g_hash_table_insert(rtcp_info, "n-ecn", GUINT_TO_POINTER(scream_rx->n_ecn));
        g_hash_table_insert(rtcp_info, "session-id", GUINT_TO_POINTER(session_id));

        GST_LOG_OBJECT(transport_agent, "queuing up scream feedback: %u, %u, %u, %u",
            scream_rx->highest_seq, scream_rx->n_loss, scream_rx->n_ecn,
            scream_rx->last_feedback_wallclock);

        g_mutex_lock(&priv->rtcp_lock);
        it = g_list_find_custom(priv->rtcp_list, (gpointer)rtcp_info,
            (GCompareFunc)compare_rtcp_scream);

        if (it) {
            g_hash_table_unref((GHashTable *)it->data);
            priv->rtcp_list = g_list_delete_link(priv->rtcp_list, it);
        }
        priv->rtcp_list = g_list_append(priv->rtcp_list, rtcp_info);
        OWR_UNUSED(it);
        g_mutex_unlock(&priv->rtcp_lock);
        g_signal_emit_by_name(rtp_session, "send-rtcp", 20000000);
    }

end:
    if (rtp_mapped)
        gst_rtp_buffer_unmap(&rtp_buf);
    if (rtp_session)
        g_object_unref(rtp_session);

    return GST_PAD_PROBE_OK;
}


static gboolean on_payload_adaptation_request(GstElement *screamqueue, guint pt,
    OwrMediaSession *media_session)
{
    OwrPayload *payload;
    guint pt_rtx;
    OwrAdaptationType adapt_type;

    OWR_UNUSED(screamqueue);
    payload = _owr_media_session_get_send_payload(media_session);
    g_assert(pt);
    g_object_get(payload, "rtx-payload-type", &pt_rtx, "adaptation", &adapt_type, NULL);
    g_object_unref(payload);
    /* Use adaptation for this payload if not retransmission */
    return (adapt_type == OWR_ADAPTATION_TYPE_SCREAM) && (pt != pt_rtx);
}
