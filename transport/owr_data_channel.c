/*
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_data_channel.h"

#include "owr_data_channel_private.h"
#include "owr_data_channel_protocol.h"
#include "owr_private.h"
#include "owr_utils.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN(_owrdatachannel_debug);
#define GST_CAT_DEFAULT _owrdatachannel_debug

#define DEFAULT_ORDERED TRUE
#define DEFAULT_MAX_PACKETS_LIFE_TIME -1
#define DEFAULT_MAX_RETRANSMITS -1
#define DEFAULT_PROTOCOL ""
#define DEFAULT_NEGOTIATED FALSE
#define DEFAULT_ID 0
#define DEFAULT_LABEL ""

#define MAX_MAX_PACKETS_LIFE_TIME 65535
#define MAX_MAX_RETRANSMITS 65535
#define MAX_CHUNK_SIZE G_MAXUSHORT

#define OWR_DATA_CHANNEL_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_DATA_CHANNEL, OwrDataChannelPrivate))

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface);

G_DEFINE_TYPE_WITH_CODE(OwrDataChannel, owr_data_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(OWR_TYPE_MESSAGE_ORIGIN, owr_message_origin_interface_init))

GType owr_data_channel_ready_state_get_type(void)
{
    static const GEnumValue values[] = {
        {OWR_DATA_CHANNEL_READY_STATE_CONNECTING, "Ready State connecting", "ready-state-connecting"},
        {OWR_DATA_CHANNEL_READY_STATE_OPEN, "Ready State open", "ready-state-open"},
        {OWR_DATA_CHANNEL_READY_STATE_CLOSING, "Ready State closing", "ready-state-closing"},
        {OWR_DATA_CHANNEL_READY_STATE_CLOSED, "Ready State closed", "ready-state-closed"},
        {0, NULL, NULL}
        };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *) & id)) {
        GType _id;
        _id = g_enum_register_static("OwrDataChannelReadyStates", values);
        g_once_init_leave((gsize *) & id, _id);
    }

    return id;
}

struct _OwrDataChannelPrivate {
    gboolean ordered;
    gint max_packet_life_time;
    gint max_retransmits;
    gchar *protocol;
    gboolean negotiated;
    guint16 id;
    gchar *label;
    GClosure *on_datachannel_send;
    GClosure *on_request_bytes_sent;
    GClosure *on_datachannel_close;
    OwrDataChannelReadyState ready_state;
    guint64 bytes_sent;
    OwrMessageOriginBusSet *message_origin_bus_set;
};

enum {
    SIGNAL_DATA_BINARY,
    SIGNAL_DATA,

    LAST_SIGNAL
};

enum {
    PROP_0,

    PROP_ORDERED,
    PROP_MAX_PACKET_LIFE_TIME,
    PROP_MAX_RETRANSMITS,
    PROP_PROTOCOL,
    PROP_NEGOTIATED,
    PROP_ID,
    PROP_LABEL,
    PROP_READY_STATE,
    PROP_BUFFERED_AMOUNT,

    N_PROPERTIES
};

static guint data_channel_signals[LAST_SIGNAL] = { 0 };
static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static gboolean data_channel_send(GHashTable *args);
static gboolean data_channel_close(GHashTable *args);
static guint get_buffered_amount(OwrDataChannel *data_channel);
static gboolean set_ready_state(GHashTable *args);

static void owr_data_channel_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    OwrDataChannelPrivate *priv;

    g_return_if_fail(object);
    g_return_if_fail(value);
    g_return_if_fail(pspec);

    priv = OWR_DATA_CHANNEL(object)->priv;

    switch (property_id) {
    case PROP_ORDERED:
        priv->ordered = g_value_get_boolean(value);
        break;
    case PROP_MAX_PACKET_LIFE_TIME:
        priv->max_packet_life_time = g_value_get_int(value);
        break;
    case PROP_MAX_RETRANSMITS:
        priv->max_retransmits = g_value_get_int(value);
        break;
    case PROP_PROTOCOL:
        if (priv->protocol)
            g_free(priv->protocol);
        priv->protocol = g_value_dup_string(value);
        if (!priv->protocol)
            priv->protocol = g_strdup(DEFAULT_PROTOCOL);
        break;
    case PROP_NEGOTIATED:
        priv->negotiated = g_value_get_boolean(value);
        break;
    case PROP_ID:
        priv->id = g_value_get_uint(value);
        break;
    case PROP_LABEL:
        if (priv->label)
            g_free(priv->label);
        priv->label = g_value_dup_string(value);
        if (!priv->label)
            priv->label = g_strdup(DEFAULT_LABEL);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_data_channel_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    OwrDataChannel *data_channel;
    OwrDataChannelPrivate *priv;

    g_return_if_fail(object);
    g_return_if_fail(value);
    g_return_if_fail(pspec);

    data_channel = OWR_DATA_CHANNEL(object);
    priv = data_channel->priv;

    switch (property_id) {
    case PROP_ORDERED:
        g_value_set_boolean(value, priv->ordered);
        break;
    case PROP_MAX_PACKET_LIFE_TIME:
        g_value_set_int(value, priv->max_packet_life_time);
        break;
    case PROP_MAX_RETRANSMITS:
        g_value_set_int(value, priv->max_retransmits);
        break;
    case PROP_PROTOCOL:
        g_value_set_string(value, priv->protocol);
        break;
    case PROP_NEGOTIATED:
        g_value_set_boolean(value, priv->negotiated);
        break;
    case PROP_ID:
        g_value_set_uint(value, priv->id);
        break;
    case PROP_LABEL:
        g_value_set_string(value, priv->label);
        break;
    case PROP_READY_STATE:
        g_value_set_enum(value, priv->ready_state);
        break;
    case PROP_BUFFERED_AMOUNT:
        g_value_set_uint(value, get_buffered_amount(data_channel));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_data_channel_finalize(GObject *object)
{
    OwrDataChannel *data_channel = OWR_DATA_CHANNEL(object);
    OwrDataChannelPrivate *priv = data_channel->priv;

    owr_message_origin_bus_set_free(priv->message_origin_bus_set);
    priv->message_origin_bus_set = NULL;

    if (priv->label)
        g_free(priv->label);
    if (priv->protocol)
        g_free(priv->protocol);

    _owr_data_channel_clear_closures(data_channel);

    G_OBJECT_CLASS(owr_data_channel_parent_class)->finalize(object);
}

static void owr_data_channel_class_init(OwrDataChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    g_type_class_add_private(klass, sizeof(OwrDataChannelPrivate));

    gobject_class->set_property = owr_data_channel_set_property;
    gobject_class->get_property = owr_data_channel_get_property;
    gobject_class->finalize = owr_data_channel_finalize;

    /**
     * OwrDataChannel::on-binary-data:
     * @data_channel:
     * @binary_data: (array length=length) (element-type guint8):
     * @length:
     */
    data_channel_signals[SIGNAL_DATA_BINARY] = g_signal_new("on-binary-data",
        G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
        G_STRUCT_OFFSET(OwrDataChannelClass, on_binary_data), NULL, NULL,
        g_cclosure_marshal_generic, G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);

    data_channel_signals[SIGNAL_DATA] = g_signal_new("on-data",
        G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
        G_STRUCT_OFFSET(OwrDataChannelClass, on_data), NULL, NULL,
        g_cclosure_marshal_generic, G_TYPE_NONE, 1, G_TYPE_STRING);

    obj_properties[PROP_ORDERED] = g_param_spec_boolean("ordered", "Ordered", "Send data ordered",
        DEFAULT_ORDERED, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    obj_properties[PROP_MAX_PACKET_LIFE_TIME] = g_param_spec_int("max-packet-life-time",
        "Max packet life time", "The maximum time to try to retransmit a packet",
        -1, MAX_MAX_PACKETS_LIFE_TIME, DEFAULT_MAX_PACKETS_LIFE_TIME,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    obj_properties[PROP_MAX_RETRANSMITS] = g_param_spec_int("max-retransmits",
        "Max retransmits", "The maximum number of retransmits for a packet",
        -1, MAX_MAX_RETRANSMITS, DEFAULT_MAX_RETRANSMITS,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    obj_properties[PROP_PROTOCOL] = g_param_spec_string("protocol", "DataChannel protocol",
        "Sub-protocol used for this channel", DEFAULT_PROTOCOL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_NEGOTIATED] = g_param_spec_boolean("negotiated", "Negotiated",
        "Datachannel already negotiated", DEFAULT_NEGOTIATED,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_ID] = g_param_spec_uint("id", "Id",
        "Channel id. Unless otherwise defined or negotiated, the id are picked based on the DTLS"
        " role; client picks even identifiers and server picks odd. However, the application is "
        "responsible for avoiding conflicts. In case of conflict, the channel should fail.",
        0, G_MAXUSHORT, DEFAULT_ID, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
        G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_LABEL] = g_param_spec_string("label", "Label",
        "The label of the channel.", DEFAULT_LABEL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    obj_properties[PROP_READY_STATE] = g_param_spec_enum("ready-state", "Ready state",
        "The ready state of the data channel", OWR_DATA_CHANNEL_READY_STATE_TYPE,
        OWR_DATA_CHANNEL_READY_STATE_CONNECTING, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_BUFFERED_AMOUNT] = g_param_spec_uint("buffered-amount", "Buffered amount",
        "The amount of buffered outgoing data on this data channel", 0, G_MAXUINT,
        0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static gpointer owr_media_renderer_get_bus_set(OwrMessageOrigin *origin)
{
    return OWR_DATA_CHANNEL(origin)->priv->message_origin_bus_set;
}

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface)
{
    interface->get_bus_set = owr_media_renderer_get_bus_set;
}

static void owr_data_channel_init(OwrDataChannel *data_channel)
{
    OwrDataChannelPrivate *priv = data_channel->priv = OWR_DATA_CHANNEL_GET_PRIVATE(data_channel);

    priv->ordered = DEFAULT_ORDERED;
    priv->max_packet_life_time = DEFAULT_MAX_PACKETS_LIFE_TIME;
    priv->max_retransmits = DEFAULT_MAX_RETRANSMITS;
    priv->protocol = g_strdup(DEFAULT_PROTOCOL);
    priv->negotiated = DEFAULT_NEGOTIATED;
    priv->id = DEFAULT_ID;
    priv->label = g_strdup(DEFAULT_LABEL);
    priv->bytes_sent = 0;

    priv->message_origin_bus_set = owr_message_origin_bus_set_new();
}

OwrDataChannel * owr_data_channel_new(gboolean ordered, gint max_packet_life_time,
    gint max_retransmits, const gchar *protocol, gboolean negotiated, guint16 id,
    const gchar *label)
{
    OwrDataChannel *data_channel;

    if (max_packet_life_time != -1 && max_retransmits != -1) {
        g_warning("Partial retransmission can only be used with EITHER max life time OR"
            "max retransmits set.");
        return NULL;
    }
    data_channel = g_object_new(OWR_TYPE_DATA_CHANNEL, "ordered", ordered,
        "max-packet-life-time", max_packet_life_time, "max-retransmits", max_retransmits,
        "protocol", protocol, "negotiated", negotiated, "id", id, "label", label, NULL);

    return data_channel;
}

void owr_data_channel_send(OwrDataChannel *data_channel, const gchar *data)
{
    GHashTable *args;
    guint8 *data_out;
    OwrDataChannelPrivate *priv = data_channel->priv;
    guint length;

    g_return_if_fail(data_channel);
    g_return_if_fail(data);

    length = strlen(data);
    g_return_if_fail(length <= MAX_CHUNK_SIZE);

    priv->bytes_sent += length;
    data_out = g_malloc(length);
    memcpy(data_out, data, length);
    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "data_channel", data_channel);
    g_hash_table_insert(args, "data", data_out);
    g_hash_table_insert(args, "length", GUINT_TO_POINTER(length));
    g_hash_table_insert(args, "is_binary", GUINT_TO_POINTER(FALSE));
    g_object_ref(data_channel);
    _owr_schedule_with_hash_table((GSourceFunc)data_channel_send, args);
}

/**
 * owr_data_channel_send_binary:
 * @data_channel:
 * @data: (array length=length):
 * @length:
 *
 */
void owr_data_channel_send_binary(OwrDataChannel *data_channel, const guint8 *data, guint16 length)
{
    GHashTable *args;
    guint8 *data_out;
    OwrDataChannelPrivate *priv = data_channel->priv;

    g_return_if_fail(data_channel);
    g_return_if_fail(data);

    priv->bytes_sent += length;
    data_out = g_malloc(length);
    memcpy(data_out, data, length);
    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "data_channel", data_channel);
    g_hash_table_insert(args, "data", data_out);
    g_hash_table_insert(args, "length", GUINT_TO_POINTER(length));
    g_hash_table_insert(args, "is_binary", GUINT_TO_POINTER(TRUE));
    g_object_ref(data_channel);
    _owr_schedule_with_hash_table((GSourceFunc)data_channel_send, args);
}

void owr_data_channel_close(OwrDataChannel *data_channel)
{
    GHashTable *args;

    g_return_if_fail(data_channel);

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "data_channel", data_channel);
    g_object_ref(data_channel);
    _owr_schedule_with_hash_table((GSourceFunc)data_channel_close, args);
}

/* Internal functions */

static gboolean data_channel_send(GHashTable *args)
{
    OwrDataChannelPrivate *priv;
    OwrDataChannel *data_channel;
    guint8 *data;
    guint length;
    gboolean is_binary;

    OWR_UNUSED(priv);
    OWR_UNUSED(data_channel);
    OWR_UNUSED(data);
    OWR_UNUSED(is_binary);

    data_channel = g_hash_table_lookup(args, "data_channel");
    data = g_hash_table_lookup(args, "data");
    length = GPOINTER_TO_UINT(g_hash_table_lookup(args, "length"));
    is_binary = GPOINTER_TO_UINT(g_hash_table_lookup(args, "is_binary"));
    priv = data_channel->priv;

    if (priv->on_datachannel_send) {
        GValue params[4] = { G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT };

        g_value_init(&params[0], OWR_TYPE_DATA_CHANNEL);
        g_value_set_object(&params[0], data_channel);
        g_value_init(&params[1], G_TYPE_POINTER);
        g_value_set_pointer(&params[1], data);
        g_value_init(&params[2], G_TYPE_UINT);
        g_value_set_uint(&params[2], length);
        g_value_init(&params[3], G_TYPE_BOOLEAN);
        g_value_set_boolean(&params[3], is_binary);

        g_closure_invoke(priv->on_datachannel_send, NULL, 4, (const GValue *)&params, NULL);

        g_value_unset(&params[0]);
        g_value_unset(&params[1]);
        g_value_unset(&params[2]);
        g_value_unset(&params[3]);
    } else
        g_free(data);

    g_hash_table_unref(args);
    g_object_unref(data_channel);

    return FALSE;
}

static gboolean data_channel_close(GHashTable *args)
{
    OwrDataChannelPrivate *priv;
    OwrDataChannel *data_channel;
    GValue params[1] = { G_VALUE_INIT };

    OWR_UNUSED(data_channel);

    data_channel = g_hash_table_lookup(args, "data_channel");
    priv = data_channel->priv;

    g_warn_if_fail(priv->on_datachannel_close);

    if (priv->on_datachannel_close) {
        g_value_init(&params[0], OWR_TYPE_DATA_CHANNEL);
        g_value_set_object(&params[0], data_channel);
        g_closure_invoke(priv->on_datachannel_close, NULL, 1, (const GValue *)&params, NULL);
        g_value_unset(&params[0]);
    }

    g_hash_table_unref(args);
    g_object_unref(data_channel);
    return FALSE;
}

static guint get_buffered_amount(OwrDataChannel *data_channel)
{
    OwrDataChannelPrivate *priv = data_channel->priv;
    guint64 bytes_sent = 0;

    if (priv->on_request_bytes_sent) {
        GValue ret_value = G_VALUE_INIT;
        GValue params[1] = { G_VALUE_INIT };

        g_value_init(&ret_value, G_TYPE_UINT64);
        g_value_init(&params[0], OWR_TYPE_DATA_CHANNEL);
        g_value_set_instance(&params[0], data_channel);

        g_closure_invoke(priv->on_request_bytes_sent, &ret_value, 1, (const GValue *)&params, NULL);

        bytes_sent = g_value_get_uint64(&ret_value);

        g_value_unset(&params[0]);
        g_value_unset(&ret_value);
    } else
        g_warning("on_request_bytes_sent closure not set. Cannot get buffered amount.");

    if (priv->bytes_sent < bytes_sent)
        bytes_sent = 0;
    else
        bytes_sent = priv->bytes_sent - bytes_sent;

    if (bytes_sent > G_MAXUINT)
        bytes_sent = G_MAXUINT;

    return (guint) bytes_sent;
}

static gboolean set_ready_state(GHashTable *args)
{
    OwrDataChannel *data_channel;
    OwrDataChannelPrivate *priv;
    OwrDataChannelReadyState state;

    data_channel = g_hash_table_lookup(args, "data_channel");
    state = GPOINTER_TO_UINT(g_hash_table_lookup(args, "state"));

    priv = data_channel->priv;
    if (state != priv->ready_state) {
        priv->ready_state = state;
        g_object_notify_by_pspec(G_OBJECT(data_channel), obj_properties[PROP_READY_STATE]);
    }
    g_object_unref(data_channel);
    g_hash_table_unref(args);

    return FALSE;
}

/* Private methods */

/**
 * _owr_data_channel_set_on_send:
 * @data_channel:
 * @on_datachannel_send: (transfer full):
 *
 */
void _owr_data_channel_set_on_send(OwrDataChannel *data_channel,
    GClosure *on_datachannel_send)
{
    OwrDataChannelPrivate *priv = data_channel->priv;

    if (priv->on_datachannel_send) {
        g_closure_invalidate(priv->on_datachannel_send);
        g_closure_unref(priv->on_datachannel_send);
    }
    priv->on_datachannel_send = on_datachannel_send;

    if (on_datachannel_send)
        g_closure_set_marshal(priv->on_datachannel_send, g_cclosure_marshal_generic);
}

/**
 * _owr_data_channel_set_on_request_bytes_sent:
 * @data_channel:
 * @on_request_bytes_sent: (transfer full):
 *
 */
void _owr_data_channel_set_on_request_bytes_sent(OwrDataChannel *data_channel,
    GClosure *on_request_bytes_sent)
{
    OwrDataChannelPrivate *priv = data_channel->priv;

    if (priv->on_request_bytes_sent) {
        g_closure_invalidate(priv->on_request_bytes_sent);
        g_closure_unref(priv->on_request_bytes_sent);
    }
    priv->on_request_bytes_sent = on_request_bytes_sent;

    if (on_request_bytes_sent)
        g_closure_set_marshal(priv->on_request_bytes_sent, g_cclosure_marshal_generic);
}

/**
 * _owr_data_channel_set_on_close:
 * @data_channel:
 * @on_datachannel_close: (transfer full):
 *
 */
void _owr_data_channel_set_on_close(OwrDataChannel *data_channel,
    GClosure *on_datachannel_close)
{
    OwrDataChannelPrivate *priv = data_channel->priv;

    if (priv->on_datachannel_close) {
        g_closure_invalidate(priv->on_datachannel_close);
        g_closure_unref(priv->on_datachannel_close);
    }

    priv->on_datachannel_close = on_datachannel_close;

    if (on_datachannel_close)
        g_closure_set_marshal(priv->on_datachannel_close, g_cclosure_marshal_generic);
}

void _owr_data_channel_clear_closures(OwrDataChannel *data_channel)
{
    OwrDataChannelPrivate *priv = data_channel->priv;

    if (priv->on_datachannel_send) {
        g_closure_invalidate(priv->on_datachannel_send);
        g_closure_unref(priv->on_datachannel_send);
        priv->on_datachannel_send = NULL;
    }
    if (priv->on_request_bytes_sent) {
        g_closure_invalidate(priv->on_request_bytes_sent);
        g_closure_unref(priv->on_request_bytes_sent);
        priv->on_request_bytes_sent = NULL;
    }

    if (priv->on_datachannel_close) {
        g_closure_invalidate(priv->on_datachannel_close);
        g_closure_unref(priv->on_datachannel_close);
        priv->on_datachannel_close = NULL;
    }
}

void _owr_data_channel_set_ready_state(OwrDataChannel *data_channel, OwrDataChannelReadyState state)
{
    GHashTable *args;

    g_object_ref(data_channel);

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "data_channel", data_channel);
    g_hash_table_insert(args, "state", GUINT_TO_POINTER(state));

    _owr_schedule_with_hash_table((GSourceFunc)set_ready_state, args);
}

GstCaps * _owr_data_channel_create_caps(OwrDataChannel *data_channel)
{
    OwrDataChannelPrivate *priv = data_channel->priv;
    GstCaps *caps;
    guint ppid = OWR_DATA_CHANNEL_PPID_STRING;

    if (priv->max_packet_life_time != -1 && priv->max_retransmits != -1) {
        g_warning("Invalid parameters for creating caps");
        return NULL;
    }

    caps = gst_caps_new_simple("application/data", "ordered", G_TYPE_BOOLEAN, priv->ordered,
        "ppid", G_TYPE_UINT, ppid, NULL);
    if (priv->max_packet_life_time == -1 && priv->max_retransmits == -1) {
        gst_caps_set_simple(caps, "partially-reliability", G_TYPE_STRING, "none",
            "reliability-parameter", G_TYPE_UINT, 0, NULL);
    } else if (priv->max_retransmits >= 0) {
        gst_caps_set_simple(caps, "partially-reliability", G_TYPE_STRING, "rtx",
            "reliability-parameter", G_TYPE_UINT, priv->max_retransmits, NULL);
    } else if (priv->max_packet_life_time >= 0) {
        gst_caps_set_simple(caps, "partially-reliability", G_TYPE_STRING, "ttl",
            "reliability-parameter", G_TYPE_UINT, priv->max_packet_life_time, NULL);
    }

    return caps;
}
