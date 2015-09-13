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

/*/
\*\ OwrDataSession
/*/


/**
 * SECTION:owr_data_session
 * @short_description: OwrDataSession
 * @title: OwrDataSession
 *
 * OwrDataSession - Represents one incoming and one outgoing data stream.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_data_session.h"

#include "owr_data_channel.h"
#include "owr_data_channel_private.h"
#include "owr_data_session_private.h"
#include "owr_private.h"
#include "owr_session_private.h"
#include "owr_utils.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN(_owrdatasession_debug);
#define GST_CAT_DEFAULT _owrdatasession_debug


#define OWR_DATA_SESSION_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_DATA_SESSION, OwrDataSessionPrivate))

G_DEFINE_TYPE(OwrDataSession, owr_data_session, OWR_TYPE_SESSION)

#define DEFAULT_SCTP_LOCAL_PORT 0
#define DEFAULT_SCTP_REMOTE_PORT 0

#define SCTP_PORT_MIN 0
#define SCTP_PORT_MAX 65534

struct _OwrDataSessionPrivate {
    guint16 local_sctp_port;
    guint16 remote_sctp_port;
    gboolean use_sock_stream;

    GHashTable *data_channels;
    GClosure *on_datachannel_added;
    guint sctp_association_id;
};

enum {
    SIGNAL_NEW_DATACHANNEL,

    LAST_SIGNAL
};

#define DEFAULT_RTCP_MUX FALSE

enum {
    PROP_0,

    PROP_SCTP_LOCAL_PORT,
    PROP_SCTP_REMOTE_PORT,
    PROP_SOCK_STREAM,

    N_PROPERTIES
};

static guint data_session_signals[LAST_SIGNAL] = { 0 };
static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static gboolean add_data_channel(GHashTable *args);
static guint get_next_association_id(void);

static void owr_data_session_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    OwrDataSession *data_session = OWR_DATA_SESSION(object);
    OwrDataSessionPrivate *priv = data_session->priv;
    GObjectClass *parent_class;

    switch (property_id) {
    case PROP_SCTP_LOCAL_PORT:
        priv->local_sctp_port = g_value_get_uint(value);
        break;
    case PROP_SCTP_REMOTE_PORT:
        priv->remote_sctp_port = g_value_get_uint(value);
        break;
    case PROP_SOCK_STREAM:
        priv->use_sock_stream = g_value_get_boolean(value);
        break;
    default:
        /* FIXME: Fix this like in the pipeline-refactoring branch */
        parent_class = g_type_class_peek_parent(OWR_DATA_SESSION_GET_CLASS(object));
        parent_class->set_property(object, property_id, value, pspec);
        break;
    }
}

static void owr_data_session_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    OwrDataSessionPrivate *priv = OWR_DATA_SESSION(object)->priv;
    GObjectClass *parent_class;

    switch (property_id) {
    case PROP_SCTP_LOCAL_PORT:
        g_value_set_uint(value, priv->local_sctp_port);
        break;
    case PROP_SCTP_REMOTE_PORT:
        g_value_set_uint(value, priv->remote_sctp_port);
        break;
    case PROP_SOCK_STREAM:
        g_value_set_boolean(value, priv->use_sock_stream);
        break;
    default:
        /* FIXME: Fix this like in the pipeline-refactoring branch */
        parent_class = g_type_class_peek_parent(OWR_DATA_SESSION_GET_CLASS(object));
        parent_class->get_property(object, property_id, value, pspec);
        break;
    }
}

static void owr_data_session_finalize(GObject *object)
{
    OwrDataSession *data_session = OWR_DATA_SESSION(object);
    OwrDataSessionPrivate *priv = data_session->priv;

    _owr_data_session_clear_closures(data_session);
    g_hash_table_unref(priv->data_channels);

    G_OBJECT_CLASS(owr_data_session_parent_class)->finalize(object);
}

static void owr_data_session_class_init(OwrDataSessionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrDataSessionPrivate));

    gobject_class->set_property = owr_data_session_set_property;
    gobject_class->get_property = owr_data_session_get_property;
    gobject_class->finalize = owr_data_session_finalize;

    data_session_signals[SIGNAL_NEW_DATACHANNEL] = g_signal_new("on-data-channel-requested",
        G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
        G_STRUCT_OFFSET(OwrDataSessionClass, on_data_channel_requested), NULL, NULL,
        g_cclosure_marshal_generic, G_TYPE_NONE, 7, G_TYPE_BOOLEAN, G_TYPE_INT, G_TYPE_INT,
        G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_STRING);

    obj_properties[PROP_SCTP_LOCAL_PORT] = g_param_spec_uint("sctp-local-port", "SCTP local port",
        "The SCTP port to receive messages", SCTP_PORT_MIN, SCTP_PORT_MAX, DEFAULT_SCTP_LOCAL_PORT,
        G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

    obj_properties[PROP_SCTP_REMOTE_PORT] = g_param_spec_uint("sctp-remote-port",
        "SCTP remote port", "The SCTP destination port", SCTP_PORT_MIN, SCTP_PORT_MAX,
        DEFAULT_SCTP_REMOTE_PORT,
        G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

    obj_properties[PROP_SOCK_STREAM] = g_param_spec_boolean("use-sock-stream", "Use sock stream",
        "When set to TRUE, a sequenced, reliable, connection-based connection is used."
        "When TRUE the partial reliability parameters of the channel is ignored.",
        FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);

}

static void owr_data_session_init(OwrDataSession *data_session)
{
    OwrDataSessionPrivate *priv;

    data_session->priv = priv = OWR_DATA_SESSION_GET_PRIVATE(data_session);

    priv->data_channels = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
        (GDestroyNotify)g_object_unref);
    priv->local_sctp_port = DEFAULT_SCTP_LOCAL_PORT;
    priv->remote_sctp_port = DEFAULT_SCTP_REMOTE_PORT;
    priv->on_datachannel_added = NULL;
    priv->sctp_association_id = get_next_association_id();
    priv->use_sock_stream = FALSE;
}

OwrDataSession * owr_data_session_new(gboolean dtls_client_mode)
{
    return g_object_new(OWR_TYPE_DATA_SESSION, "dtls-client-mode", dtls_client_mode, NULL);
}

/**
 * owr_data_session_add_data_channel:
 * @data_session:
 * @data_channel: (transfer full):
 *
 */
void owr_data_session_add_data_channel(OwrDataSession *data_session, OwrDataChannel *data_channel)
{
    GHashTable *args;

    g_return_if_fail(OWR_IS_DATA_SESSION(data_session));
    g_return_if_fail(OWR_IS_DATA_CHANNEL(data_channel));

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(data_session));
    g_hash_table_insert(args, "data_session", data_session);
    g_hash_table_insert(args, "data_channel", data_channel);
    g_object_ref(data_session);
    g_object_ref(data_channel);
    _owr_schedule_with_hash_table((GSourceFunc)add_data_channel, args);
}

/* Internal functions */

static gboolean add_data_channel(GHashTable *args)
{
    OwrDataSessionPrivate *priv;
    OwrDataSession *data_session;
    OwrDataChannel *data_channel;
    guint id;

    data_session = g_hash_table_lookup(args, "data_session");
    data_channel = g_hash_table_lookup(args, "data_channel");
    priv = data_session->priv;

    g_object_get(data_channel, "id", &id, NULL);
    if (g_hash_table_contains(priv->data_channels, GUINT_TO_POINTER(id))) {
        g_warning("A datachannel with channel id %u already exist. A new channel will not be "
            "created.", id);
        g_object_unref(data_channel);
        goto end;
    }

    g_hash_table_insert(priv->data_channels, GUINT_TO_POINTER(id), data_channel);
    if (priv->on_datachannel_added) {
        GValue params[2] = { G_VALUE_INIT, G_VALUE_INIT };

        g_value_init(&params[0], OWR_TYPE_DATA_SESSION);
        g_value_set_object(&params[0], data_session);
        g_value_init(&params[1], OWR_TYPE_DATA_CHANNEL);
        g_value_set_instance(&params[1], data_channel);

        g_closure_invoke(priv->on_datachannel_added, NULL, 2, (const GValue *)&params, NULL);

        g_value_unset(&params[0]);
        g_value_unset(&params[1]);
    }

end:
    g_hash_table_unref(args);
    g_object_unref(data_session);

    return FALSE;
}

static guint get_next_association_id(void)
{
    static guint association_id = 0;
    return ++association_id;
}

/* Private methods */

void _owr_data_session_clear_closures(OwrDataSession *data_session)
{
    OwrDataSessionPrivate *priv = data_session->priv;

    if (priv->on_datachannel_added) {
        g_closure_invalidate(priv->on_datachannel_added);
        g_closure_unref(priv->on_datachannel_added);
        priv->on_datachannel_added = NULL;
    }

    _owr_session_clear_closures(OWR_SESSION(data_session));
}

GstElement * _owr_data_session_create_decoder(OwrDataSession *data_session)
{
    OwrDataSessionPrivate *priv = data_session->priv;
    GstElement *sctpdec;
    gchar *name;

    name = _owr_data_session_get_decoder_name(data_session);
    sctpdec = gst_element_factory_make("sctpdec", name);
    g_free(name);
    g_assert(sctpdec);
    g_object_set(sctpdec, "sctp-association-id", priv->sctp_association_id, NULL);
    g_object_bind_property(data_session, "sctp-local-port", sctpdec, "local-sctp-port",
        G_BINDING_SYNC_CREATE);

    return sctpdec;
}

GstElement * _owr_data_session_create_encoder(OwrDataSession *data_session)
{
    OwrDataSessionPrivate *priv = data_session->priv;
    GstElement *sctpenc;
    gchar *name;

    name = _owr_data_session_get_encoder_name(data_session);
    sctpenc = gst_element_factory_make("sctpenc", name);
    g_free(name);
    g_assert(sctpenc);
    g_object_set(sctpenc, "sctp-association-id", priv->sctp_association_id,
        "use-sock-stream", priv->use_sock_stream, NULL);
    g_object_bind_property(data_session, "sctp-remote-port", sctpenc, "remote-sctp-port",
        G_BINDING_SYNC_CREATE);

    return sctpenc;
}

/**
 * _owr_data_session_set_on_datachannel_added:
 * @data_session:
 * @on_datachannel_added: (transfer full):
 *
 */
void _owr_data_session_set_on_datachannel_added(OwrDataSession *data_session,
    GClosure *on_datachannel_added)
{
    OwrDataSessionPrivate *priv = data_session->priv;

    if (priv->on_datachannel_added) {
        g_closure_invalidate(priv->on_datachannel_added);
        g_closure_unref(priv->on_datachannel_added);
    }
    priv->on_datachannel_added = on_datachannel_added;

    if (on_datachannel_added)
        g_closure_set_marshal(priv->on_datachannel_added, g_cclosure_marshal_generic);
}

/**
 * _owr_data_session_get_datachannel:
 * @data_session:
 * @id:
 *
 * Returns: (transfer none):
 *
 */
OwrDataChannel * _owr_data_session_get_datachannel(OwrDataSession *data_session, guint16 id)
{
    OwrDataSessionPrivate *priv = data_session->priv;
    OwrDataChannel *data_channel;

    data_channel = OWR_DATA_CHANNEL(g_hash_table_lookup(priv->data_channels, GUINT_TO_POINTER(id)));
    return data_channel;
}

/**
 * _owr_data_session_get_datachannels:
 * @data_session:
 * @id:
 *
 * Returns: (transfer container) (element-type OwrDataChannel):
 *
 */
GList * _owr_data_session_get_datachannels(OwrDataSession *data_session)
{
    OwrDataSessionPrivate *priv = data_session->priv;
    GList *data_channels;

    data_channels = g_hash_table_get_values(priv->data_channels);

    return data_channels;
}

gchar * _owr_data_session_get_encoder_name(OwrDataSession *data_session)
{
    OwrDataSessionPrivate *priv = data_session->priv;

    gchar *name = g_strdup_printf("sctpenc_%u", priv->sctp_association_id);

    return name;
}

gchar * _owr_data_session_get_decoder_name(OwrDataSession *data_session)
{
    OwrDataSessionPrivate *priv = data_session->priv;

    gchar *name = g_strdup_printf("sctpdec_%u", priv->sctp_association_id);

    return name;
}
