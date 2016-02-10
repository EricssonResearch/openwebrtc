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
\*\ OwrSession
/*/


/**
 * SECTION:owr_session
 * @short_description: OwrSession
 * @title: OwrSession
 *
 * OwrSession - Represents a connection used for a session of either media or data.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_session.h"

#include "owr_candidate_private.h"
#include "owr_private.h"
#include "owr_session_private.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN(_owrsession_debug);
#define GST_CAT_DEFAULT _owrsession_debug

#define DEFAULT_DTLS_CLIENT_MODE FALSE
#define DEFAULT_DTLS_CERTIFICATE "(auto)"
#define DEFAULT_DTLS_KEY "(auto)"
#define DEFAULT_ICE_STATE OWR_ICE_STATE_DISCONNECTED

#define OWR_SESSION_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_SESSION, OwrSessionPrivate))

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface);

G_DEFINE_TYPE_WITH_CODE(OwrSession, owr_session, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(OWR_TYPE_MESSAGE_ORIGIN, owr_message_origin_interface_init))

struct _OwrSessionPrivate {
    gboolean dtls_client_mode;
    gchar *dtls_certificate;
    gchar *dtls_key;
    gchar *dtls_peer_certificate;
    GSList *local_candidates;
    GSList *remote_candidates;
    GSList *forced_remote_candidates;
    OwrCandidate *local_candidate[OWR_COMPONENT_MAX],
                 *remote_candidate[OWR_COMPONENT_MAX];
    gboolean gathering_done;
    GClosure *on_remote_candidate;
    GClosure *on_local_candidate_change;
    OwrIceState ice_state, rtp_ice_state, rtcp_ice_state;
    OwrMessageOriginBusSet *message_origin_bus_set;
    guint rtp_port, rtcp_port;
};

enum {
    SIGNAL_ON_NEW_CANDIDATE,
    SIGNAL_ON_CANDIDATE_GATHERING_DONE,

    LAST_SIGNAL
};

enum {
    PROP_0,

    PROP_DTLS_CLIENT_MODE,
    PROP_DTLS_CERTIFICATE,
    PROP_DTLS_KEY,
    PROP_DTLS_PEER_CERTIFICATE,
    PROP_ICE_STATE,

    N_PROPERTIES
};

static guint session_signals[LAST_SIGNAL] = { 0 };
static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

GType owr_ice_state_get_type(void)
{
    static const GEnumValue types[] = {
        {OWR_ICE_STATE_DISCONNECTED, "ICE state disconnected", "disconnected"},
        {OWR_ICE_STATE_GATHERING, "ICE state gathering", "gathering"},
        {OWR_ICE_STATE_CONNECTING, "ICE state connecting", "connecting"},
        {OWR_ICE_STATE_CONNECTED, "ICE state connected", "connected"},
        {OWR_ICE_STATE_READY, "ICE state ready", "ready"},
        {OWR_ICE_STATE_FAILED, "ICE state failed", "failed"},
        {0, NULL, NULL}
    };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *)&id)) {
        GType _id = g_enum_register_static("OwrIceStates", types);
        g_once_init_leave((gsize *)&id, _id);
    }

    return id;
}

static gboolean add_remote_candidate(GHashTable *args);
static gboolean add_candidate_pair(GHashTable *args);
static void update_local_credentials(OwrCandidate *candidate, GParamSpec *pspec, OwrSession *session);


static void owr_session_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    OwrSessionPrivate *priv = OWR_SESSION(object)->priv;

    switch (property_id) {
    case PROP_DTLS_CLIENT_MODE:
        priv->dtls_client_mode = g_value_get_boolean(value);
        break;

    case PROP_DTLS_CERTIFICATE:
        if (priv->dtls_certificate)
            g_free(priv->dtls_certificate);
        priv->dtls_certificate = g_value_dup_string(value);
        if (priv->dtls_certificate && (!priv->dtls_certificate[0]
            || g_strstr_len(priv->dtls_certificate, 5, "null"))) {
            g_free(priv->dtls_certificate);
            priv->dtls_certificate = NULL;
        }
        GST_DEBUG_OBJECT(OWR_SESSION(object), "certificate generated: %s\n", priv->dtls_certificate);
        g_warn_if_fail(!priv->dtls_certificate
            || g_str_has_prefix(priv->dtls_certificate, "-----BEGIN CERTIFICATE-----"));
        break;

    case PROP_DTLS_KEY:
        if (priv->dtls_key)
            g_free(priv->dtls_key);
        priv->dtls_key = g_value_dup_string(value);
        if (priv->dtls_key && (!priv->dtls_key[0] || g_strstr_len(priv->dtls_key, 5, "null"))) {
            g_free(priv->dtls_key);
            priv->dtls_key = NULL;
        }
        g_warn_if_fail(!priv->dtls_key
            || g_str_has_prefix(priv->dtls_key, "-----BEGIN PRIVATE KEY-----")
            || g_str_has_prefix(priv->dtls_key, "-----BEGIN RSA PRIVATE KEY-----"));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_session_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    OwrSessionPrivate *priv = OWR_SESSION(object)->priv;

    switch (property_id) {
    case PROP_DTLS_CLIENT_MODE:
        g_value_set_boolean(value, priv->dtls_client_mode);
        break;

    case PROP_DTLS_CERTIFICATE:
        g_value_set_string(value, priv->dtls_certificate);
        break;

    case PROP_DTLS_KEY:
        g_value_set_string(value, priv->dtls_key);
        break;

    case PROP_DTLS_PEER_CERTIFICATE:
        g_value_set_string(value, priv->dtls_peer_certificate);
        break;

    case PROP_ICE_STATE:
        g_value_set_enum(value, priv->ice_state);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_session_on_new_candidate(OwrSession *session, OwrCandidate *candidate)
{
    OwrSessionPrivate *priv;

    g_return_if_fail(session);
    g_return_if_fail(candidate);

    priv = session->priv;
    g_warn_if_fail(!priv->gathering_done);
    priv->local_candidates = g_slist_append(priv->local_candidates, g_object_ref(candidate));

    g_signal_connect_object(G_OBJECT(candidate), "notify::ufrag",
        G_CALLBACK(update_local_credentials), session, 0);
    g_signal_connect_object(G_OBJECT(candidate), "notify::password",
        G_CALLBACK(update_local_credentials), session, 0);
}

static void owr_session_on_candidate_gathering_done(OwrSession *session)
{
    g_return_if_fail(session);

    session->priv->gathering_done = TRUE;
}

static void owr_session_finalize(GObject *object)
{
    OwrSession *session = OWR_SESSION(object);
    OwrSessionPrivate *priv = session->priv;

    _owr_session_clear_closures(session);

    if (priv->dtls_certificate)
        g_free(priv->dtls_certificate);
    if (priv->dtls_key)
        g_free(priv->dtls_key);
    if (priv->dtls_peer_certificate)
        g_free(priv->dtls_peer_certificate);

    g_slist_free_full(priv->local_candidates, (GDestroyNotify)g_object_unref);
    g_slist_free_full(priv->remote_candidates, (GDestroyNotify)g_object_unref);
    g_slist_free_full(priv->forced_remote_candidates, (GDestroyNotify)g_object_unref);

    owr_message_origin_bus_set_free(priv->message_origin_bus_set);
    priv->message_origin_bus_set = NULL;

    G_OBJECT_CLASS(owr_session_parent_class)->finalize(object);
}

static void owr_session_class_init(OwrSessionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrSessionPrivate));

    klass->on_new_candidate = owr_session_on_new_candidate;
    klass->on_candidate_gathering_done = owr_session_on_candidate_gathering_done;

    /**
    * OwrSession::on-new-candidate:
    * @session: the object which received the signal
    * @candidate: the candidate gathered
    *
    * Notify of a new gathered candidate for a #OwrSession.
    */
    session_signals[SIGNAL_ON_NEW_CANDIDATE] = g_signal_new("on-new-candidate",
        G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
        G_STRUCT_OFFSET(OwrSessionClass, on_new_candidate), NULL, NULL,
        NULL, G_TYPE_NONE, 1, OWR_TYPE_CANDIDATE);

    /**
    * OwrSession::on-candidate-gathering-done:
    * @session: the object which received the signal
    *
    * Notify that all candidates have been gathered for a #OwrSession
    */
    session_signals[SIGNAL_ON_CANDIDATE_GATHERING_DONE] =
        g_signal_new("on-candidate-gathering-done",
        G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
        G_STRUCT_OFFSET(OwrSessionClass, on_candidate_gathering_done), NULL, NULL,
        NULL, G_TYPE_NONE, 0);

    gobject_class->set_property = owr_session_set_property;
    gobject_class->get_property = owr_session_get_property;
    gobject_class->finalize = owr_session_finalize;

    obj_properties[PROP_DTLS_CLIENT_MODE] = g_param_spec_boolean("dtls-client-mode",
        "DTLS client mode", "TRUE if the DTLS connection should be setup using client role.",
        DEFAULT_DTLS_CLIENT_MODE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
        G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_DTLS_CERTIFICATE] = g_param_spec_string("dtls-certificate",
        "DTLS certificate", "The X509 certificate to be used by DTLS (in PEM format)."
        " Set to NULL or empty string to disable DTLS",
        DEFAULT_DTLS_CERTIFICATE, G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE |
        G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_DTLS_KEY] = g_param_spec_string("dtls-key",
        "DTLS key", "The RSA private key to be used by DTLS (in PEM format)",
        DEFAULT_DTLS_KEY, G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_DTLS_PEER_CERTIFICATE] = g_param_spec_string("dtls-peer-certificate",
        "DTLS peer certificate",
        "The X509 certificate of the remote peer, used by DTLS (in PEM format)",
        NULL, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_ICE_STATE] = g_param_spec_enum("ice-connection-state",
        "ICE connection state", "The state of the ICE connection",
        OWR_TYPE_ICE_STATE, DEFAULT_ICE_STATE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);

}

static gpointer owr_session_get_bus_set(OwrMessageOrigin *origin)
{
    return OWR_SESSION(origin)->priv->message_origin_bus_set;
}

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface)
{
    interface->get_bus_set = owr_session_get_bus_set;
}

static void owr_session_init(OwrSession *session)
{
    OwrSessionPrivate *priv;

    session->priv = priv = OWR_SESSION_GET_PRIVATE(session);
    priv->dtls_client_mode = DEFAULT_DTLS_CLIENT_MODE;
    priv->dtls_certificate = g_strdup(DEFAULT_DTLS_CERTIFICATE);
    priv->dtls_key = g_strdup(DEFAULT_DTLS_KEY);
    priv->ice_state = DEFAULT_ICE_STATE;
    priv->rtp_ice_state = DEFAULT_ICE_STATE;
    priv->rtcp_ice_state = DEFAULT_ICE_STATE;
    priv->dtls_peer_certificate = NULL;
    priv->local_candidates = NULL;
    priv->remote_candidates = NULL;
    priv->forced_remote_candidates = NULL;
    priv->gathering_done = FALSE;
    priv->on_remote_candidate = NULL;
    priv->message_origin_bus_set = owr_message_origin_bus_set_new();
}


static gchar *owr_ice_state_get_name(OwrIceState state)
{
    GEnumClass *enum_class;
    GEnumValue *enum_value;
    gchar *name;

    enum_class = G_ENUM_CLASS(g_type_class_ref(OWR_TYPE_ICE_STATE));
    enum_value = g_enum_get_value(enum_class, state);
    name = g_strdup(enum_value ? enum_value->value_nick : "unknown");
    g_type_class_unref(enum_class);

    return name;
}

static gboolean is_multiplexing_rtcp(OwrSession *session)
{
    GParamSpec *pspec;
    gboolean rtcp_mux = TRUE;

    pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(session), "rtcp-mux");
    if (pspec && G_PARAM_SPEC_VALUE_TYPE(pspec) == G_TYPE_BOOLEAN)
        g_object_get(session, "rtcp-mux", &rtcp_mux, NULL);

    return rtcp_mux;
}

static void schedule_add_remote_candidate(OwrSession *session, OwrCandidate *candidate, gboolean f)
{
    GHashTable *args;

    g_return_if_fail(OWR_IS_SESSION(session));
    g_return_if_fail(OWR_IS_CANDIDATE(candidate));

    if (_owr_candidate_get_component_type(candidate) == OWR_COMPONENT_TYPE_RTCP
        && is_multiplexing_rtcp(session)) {
        g_warning("Trying to add an RTCP candidate to an RTP/RTCP multiplexing session. Aborting");
        return;
    }

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(session));
    g_hash_table_insert(args, "session", session);
    g_hash_table_insert(args, "candidate", candidate);
    g_hash_table_insert(args, "forced", GINT_TO_POINTER(f));
    g_object_ref(session);
    g_object_ref(candidate);

    _owr_schedule_with_hash_table((GSourceFunc)add_remote_candidate, args);
}

/**
 * owr_session_add_remote_candidate:
 * @session: the session on which the candidate will be added.
 * @candidate: (transfer none): the candidate to add
 *
 * Adds a remote candidate for this session.
 *
 */
void owr_session_add_remote_candidate(OwrSession *session, OwrCandidate *candidate)
{
    schedule_add_remote_candidate(session, candidate, FALSE);
}

/**
 * owr_session_force_remote_candidate:
 * @session: The session on which the candidate will be forced.
 * @candidate: (transfer none): the candidate to forcibly set
 *
 * Forces the transport agent to use the given candidate. Calling this function will disable all
 * further ICE processing. Keep-alives will continue to be sent.
 */
void owr_session_force_remote_candidate(OwrSession *session, OwrCandidate *candidate)
{
    schedule_add_remote_candidate(session, candidate, TRUE);
}

/**
 * owr_session_force_candidate_pair:
 * @session: The session on which the candidate will be forced.
 * @local_candidate: (transfer none): the local candidate to forcibly set
 * @remote_candidate: (transfer none): the remote candidate to forcibly set
 *
 * Forces the transport agent to use the given candidate pair. Calling this
 * function will disable all further ICE processing. Keep-alives will continue
 * to be sent.
 */
void owr_session_force_candidate_pair(OwrSession *session, OwrComponentType ctype,
        OwrCandidate *local_candidate, OwrCandidate *remote_candidate)
{
    GHashTable *args;

    g_return_if_fail(OWR_IS_SESSION(session));
    g_return_if_fail(OWR_IS_CANDIDATE(local_candidate));
    g_return_if_fail(OWR_IS_CANDIDATE(remote_candidate));

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(session));
    g_hash_table_insert(args, "session", session);
    g_hash_table_insert(args, "component-type", GUINT_TO_POINTER(ctype));
    g_hash_table_insert(args, "local-candidate", local_candidate);
    g_hash_table_insert(args, "remote-candidate", remote_candidate);
    g_object_ref(session);
    g_object_ref(local_candidate);
    g_object_ref(remote_candidate);

    _owr_schedule_with_hash_table((GSourceFunc)add_candidate_pair, args);
}

/* Internal functions */

static gboolean add_remote_candidate(GHashTable *args)
{
    OwrSession *session;
    OwrSessionPrivate *priv;
    OwrCandidate *candidate;
    gboolean forced;
    GSList **candidates;
    GValue params[2] = { G_VALUE_INIT, G_VALUE_INIT };

    g_return_val_if_fail(args, FALSE);

    session = g_hash_table_lookup(args, "session");
    candidate = g_hash_table_lookup(args, "candidate");
    forced = GPOINTER_TO_INT(g_hash_table_lookup(args, "forced"));
    g_return_val_if_fail(session && candidate, FALSE);

    priv = session->priv;
    candidates = forced ? &priv->forced_remote_candidates : &priv->remote_candidates;

    if (g_slist_find(*candidates, candidate)) {
        g_warning("Fail: remote candidate already added.");
        goto end;
    }

    if (g_slist_find(priv->local_candidates, candidate)) {
        g_warning("Fail: candidate is local.");
        goto end;
    }

    *candidates = g_slist_append(*candidates, candidate);
    g_object_ref(candidate);

    if (priv->on_remote_candidate) {
        g_value_init(&params[0], OWR_TYPE_SESSION);
        g_value_set_object(&params[0], session);
        g_value_init(&params[1], G_TYPE_BOOLEAN);
        g_value_set_boolean(&params[1], forced);
        g_closure_invoke(priv->on_remote_candidate, NULL, 2, (const GValue *)&params, NULL);
        g_value_unset(&params[0]);
        g_value_unset(&params[1]);
    }

end:
    g_object_unref(candidate);
    g_object_unref(session);
    g_hash_table_unref(args);
    return FALSE;
}

static gboolean add_candidate_pair(GHashTable *args)
{
    OwrSession *session;
    OwrSessionPrivate *priv;
    OwrCandidate *local_candidate, *remote_candidate;
    OwrComponentType ctype;

    g_return_val_if_fail(args, FALSE);

    session = g_hash_table_lookup(args, "session");
    priv = session->priv;

    local_candidate = g_hash_table_lookup(args, "local-candidate");
    remote_candidate = g_hash_table_lookup(args, "remote-candidate");
    g_return_val_if_fail(session && local_candidate && remote_candidate, FALSE);

    ctype = GPOINTER_TO_UINT(g_hash_table_lookup(args, "component-type"));
    g_return_val_if_fail(ctype < OWR_COMPONENT_MAX, FALSE);

    if (priv->forced_remote_candidates) {
        g_warning("Fail: forced remote candidate already.");
        goto end;
    }

    if (!g_slist_find(priv->remote_candidates, remote_candidate)) {
        g_warning("Fail: remote candidate not added.");
        goto end;
    }

    if (g_slist_find(priv->local_candidates, remote_candidate)) {
        g_warning("Fail: candidate is local.");
        goto end;
    }

    if (!g_slist_find(priv->local_candidates, local_candidate)) {
        g_warning("Fail: local candidate not added.");
        goto end;
    }

    priv->local_candidate[ctype] = g_object_ref(local_candidate);
    priv->remote_candidate[ctype] = g_object_ref(remote_candidate);

end:
    g_object_unref(local_candidate);
    g_object_unref(remote_candidate);
    g_object_unref(session);
    g_hash_table_unref(args);
    return FALSE;
}

void _owr_session_get_candidate_pair(OwrSession *session, OwrComponentType ctype,
        OwrCandidate **local, OwrCandidate **remote)
{
    OwrSessionPrivate *priv;

    g_return_if_fail(ctype < OWR_COMPONENT_MAX);

    priv = session->priv;

    *local = priv->local_candidate[ctype];
    *remote = priv->remote_candidate[ctype];
}

void owr_session_set_local_port(OwrSession *session, OwrComponentType ctype, guint port)
{
    g_return_if_fail(ctype < OWR_COMPONENT_MAX);

    if (ctype == OWR_COMPONENT_TYPE_RTP)
        session->priv->rtp_port = port;
    else
        session->priv->rtcp_port = port;
}

guint _owr_session_get_local_port(OwrSession *session, OwrComponentType ctype)
{
    g_return_val_if_fail(ctype < OWR_COMPONENT_MAX, 0);

    if (ctype == OWR_COMPONENT_TYPE_RTP)
        return session->priv->rtp_port;
    else
        return session->priv->rtcp_port;
}

static void update_local_credentials(OwrCandidate *candidate, GParamSpec *pspec, OwrSession *session)
{
    OwrSessionPrivate *priv;
    GSList *item;
    GObject *local_candidate;
    gchar *ufrag = NULL, *password = NULL;
    GValue params[2] = { G_VALUE_INIT, G_VALUE_INIT };

    g_return_if_fail(OWR_IS_CANDIDATE(candidate));
    g_return_if_fail(G_IS_PARAM_SPEC(pspec));
    g_return_if_fail(OWR_IS_SESSION(session));
    priv = session->priv;

    g_object_get(G_OBJECT(candidate), "ufrag", &ufrag, "password", &password, NULL);

    for (item = priv->local_candidates; item; item = item->next) {
        local_candidate = G_OBJECT(item->data);
        if (local_candidate == G_OBJECT(candidate))
            continue;
        g_signal_handlers_block_matched(local_candidate, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
            0, 0, NULL, G_CALLBACK(update_local_credentials), session);
        g_object_set(local_candidate, "ufrag", ufrag, "password", password, NULL);
        g_signal_handlers_unblock_matched(local_candidate, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
            0, 0, NULL, G_CALLBACK(update_local_credentials), session);
    }

    g_free(ufrag);
    g_free(password);

    if (priv->on_local_candidate_change) {
        g_value_init(&params[0], OWR_TYPE_SESSION);
        g_value_set_object(&params[0], session);
        g_value_init(&params[1], OWR_TYPE_CANDIDATE);
        g_value_set_object(&params[1], candidate);
        g_closure_invoke(priv->on_local_candidate_change, NULL, 2, (const GValue *)&params, NULL);
        g_value_unset(&params[0]);
        g_value_unset(&params[1]);
    }
}

void _owr_session_clear_closures(OwrSession *session)
{
    if (session->priv->on_remote_candidate) {
        g_closure_invalidate(session->priv->on_remote_candidate);
        g_closure_unref(session->priv->on_remote_candidate);
        session->priv->on_remote_candidate = NULL;
    }
    if (session->priv->on_local_candidate_change) {
        g_closure_invalidate(session->priv->on_local_candidate_change);
        g_closure_unref(session->priv->on_local_candidate_change);
        session->priv->on_local_candidate_change = NULL;
    }
}

/* Private methods */
/**
 * _owr_session_get_remote_candidates:
 * @session:
 *
 * Must be called from the main thread
 *
 * Returns: (transfer none) (element-type OwrCandidate):
 *
 */
GSList * _owr_session_get_remote_candidates(OwrSession *session)
{
    g_return_val_if_fail(session, NULL);

    return session->priv->remote_candidates;
}

/**
 * _owr_session_get_forced_remote_candidates:
 * @session:
 *
 * Must be called from the main thread
 *
 * Returns: (transfer none) (element-type OwrCandidate):
 *
 */
GSList * _owr_session_get_forced_remote_candidates(OwrSession *session)
{
    g_return_val_if_fail(OWR_IS_SESSION(session), NULL);

    return session->priv->forced_remote_candidates;
}

/**
 * _owr_session_set_on_remote_candidate:
 * @session:
 * @on_remote_candidate: (transfer full):
 *
 */
void _owr_session_set_on_remote_candidate(OwrSession *session, GClosure *on_remote_candidate)
{
    g_return_if_fail(session);
    g_return_if_fail(on_remote_candidate);

    if (session->priv->on_remote_candidate)
        g_closure_unref(session->priv->on_remote_candidate);
    session->priv->on_remote_candidate = on_remote_candidate;
    g_closure_set_marshal(session->priv->on_remote_candidate, g_cclosure_marshal_generic);
}

void _owr_session_set_on_local_candidate_change(OwrSession *session, GClosure *on_local_candidate_change)
{
    g_return_if_fail(OWR_IS_SESSION(session));
    g_return_if_fail(on_local_candidate_change);

    if (session->priv->on_local_candidate_change)
        g_closure_unref(session->priv->on_local_candidate_change);
    session->priv->on_local_candidate_change = on_local_candidate_change;
    g_closure_set_marshal(session->priv->on_local_candidate_change, g_cclosure_marshal_generic);
}

void _owr_session_set_dtls_peer_certificate(OwrSession *session,
    const gchar *certificate)
{
    OwrSessionPrivate *priv;
    g_return_if_fail(OWR_IS_SESSION(session));

    priv = session->priv;
    if (priv->dtls_peer_certificate)
        g_free(priv->dtls_peer_certificate);
    priv->dtls_peer_certificate = g_strdup(certificate);
    g_warn_if_fail(!priv->dtls_peer_certificate
        || g_str_has_prefix(priv->dtls_peer_certificate, "-----BEGIN CERTIFICATE-----"));
    g_object_notify(G_OBJECT(session), "dtls-peer-certificate");
}

static OwrIceState owr_session_aggregate_ice_state(OwrIceState rtp_ice_state,
    OwrIceState rtcp_ice_state)
{
    if (rtp_ice_state == OWR_ICE_STATE_FAILED || rtcp_ice_state == OWR_ICE_STATE_FAILED)
        return OWR_ICE_STATE_FAILED;

    return rtp_ice_state < rtcp_ice_state ? rtp_ice_state : rtcp_ice_state;
}

void _owr_session_emit_ice_state_changed(OwrSession *session, guint session_id,
    OwrComponentType component_type, OwrIceState state)
{
    OwrIceState old_state, new_state;
    gchar *old_state_name, *new_state_name;
    gboolean rtcp_mux = is_multiplexing_rtcp(session);

    if (rtcp_mux) {
        old_state = session->priv->rtp_ice_state;
    } else {
        old_state = owr_session_aggregate_ice_state(session->priv->rtp_ice_state,
            session->priv->rtcp_ice_state);
    }

    if (component_type == OWR_COMPONENT_TYPE_RTP)
        session->priv->rtp_ice_state = state;
    else
        session->priv->rtcp_ice_state = state;

    if (rtcp_mux) {
        new_state = session->priv->rtp_ice_state;
    } else {
        new_state = owr_session_aggregate_ice_state(session->priv->rtp_ice_state,
            session->priv->rtcp_ice_state);
    }

    if (old_state == new_state)
        return;

    old_state_name = owr_ice_state_get_name(old_state);
    new_state_name = owr_ice_state_get_name(new_state);

    if (new_state == OWR_ICE_STATE_FAILED) {
        GST_ERROR_OBJECT(session, "Session %u, ICE failed to establish a connection!\n"
            "ICE state changed from %s to %s",
            session_id, old_state_name, new_state_name);
    } else if (new_state == OWR_ICE_STATE_CONNECTED || new_state == OWR_ICE_STATE_READY) {
        GST_INFO_OBJECT(session, "Session %u, ICE state changed from %s to %s",
            session_id, old_state_name, new_state_name);
    } else {
        GST_DEBUG_OBJECT(session, "Session %u, ICE state changed from %s to %s",
            session_id, old_state_name, new_state_name);
    }
    g_free(old_state_name);
    g_free(new_state_name);

    session->priv->ice_state = new_state;
    g_object_notify_by_pspec(G_OBJECT(session), obj_properties[PROP_ICE_STATE]);
}
