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
\*\ OwrCandidate
/*/


/**
 * SECTION:owr_candidate
 * @short_description: A remote or local ICE connection candidate.
 * @title: OwrCandidate
 *
 * A remote or local ICE connection candidate. If ICE is not used a candidate can be forced (see OwrMediaSession).
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_candidate.h"

#include "owr_candidate_private.h"

#include <gst/gst.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN(_owrcandidate_debug);
#define GST_CAT_DEFAULT _owrcandidate_debug

#define OWR_CANDIDATE_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_CANDIDATE, OwrCandidatePrivate))

G_DEFINE_TYPE(OwrCandidate, owr_candidate, G_TYPE_OBJECT)

struct _OwrCandidatePrivate {
    OwrCandidateType type;
    OwrComponentType component_type;
    OwrTransportType transport_type;
    gchar *address;
    guint port;
    gchar *base_address;
    guint base_port;
    guint priority;
    gchar *foundation;
    gchar *ufrag;
    gchar *password;
};

enum {
    PROP_0,

    PROP_TYPE,
    PROP_COMPONENT_TYPE,
    PROP_TRANSPORT_TYPE,
    PROP_ADDRESS,
    PROP_PORT,
    PROP_BASE_ADDRESS,
    PROP_BASE_PORT,
    PROP_PRIORITY,
    PROP_FOUNDATION,
    PROP_UFRAG,
    PROP_PASSWORD,

    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static void owr_candidate_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    OwrCandidatePrivate *priv = OWR_CANDIDATE(object)->priv;

    switch (property_id) {
    case PROP_TYPE:
        priv->type = g_value_get_enum(value);
        break;

    case PROP_COMPONENT_TYPE:
        priv->component_type = g_value_get_enum(value);
        break;

    case PROP_TRANSPORT_TYPE:
        priv->transport_type = g_value_get_enum(value);
        break;

    case PROP_ADDRESS:
        if (priv->address)
            g_free(priv->address);
        priv->address = g_value_dup_string(value);
        break;

    case PROP_PORT:
        priv->port = g_value_get_uint(value);
        break;

    case PROP_BASE_ADDRESS:
        if (priv->base_address)
            g_free(priv->base_address);
        priv->base_address = g_value_dup_string(value);
        break;

    case PROP_BASE_PORT:
        priv->base_port = g_value_get_uint(value);
        break;

    case PROP_PRIORITY:
        priv->priority = g_value_get_uint(value);
        break;

    case PROP_FOUNDATION:
        if (priv->foundation)
            g_free(priv->foundation);
        priv->foundation = g_value_dup_string(value);
        break;

    case PROP_UFRAG:
        if (priv->ufrag)
            g_free(priv->ufrag);
        priv->ufrag = g_value_dup_string(value);
        break;

    case PROP_PASSWORD:
        if (priv->password)
            g_free(priv->password);
        priv->password = g_value_dup_string(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_candidate_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    OwrCandidatePrivate *priv = OWR_CANDIDATE(object)->priv;

    switch (property_id) {
    case PROP_TYPE:
        g_value_set_enum(value, priv->type);
        break;

    case PROP_COMPONENT_TYPE:
        g_value_set_enum(value, priv->component_type);
        break;

    case PROP_TRANSPORT_TYPE:
        g_value_set_enum(value, priv->transport_type);
        break;

    case PROP_ADDRESS:
        g_value_set_string(value, priv->address);
        break;

    case PROP_PORT:
        g_value_set_uint(value, priv->port);
        break;

    case PROP_BASE_ADDRESS:
        g_value_set_string(value, priv->base_address);
        break;

    case PROP_BASE_PORT:
        g_value_set_uint(value, priv->base_port);
        break;

    case PROP_PRIORITY:
        g_value_set_uint(value, priv->priority);
        break;

    case PROP_FOUNDATION:
        g_value_set_string(value, priv->foundation);
        break;

    case PROP_UFRAG:
        g_value_set_string(value, priv->ufrag);
        break;

    case PROP_PASSWORD:
        g_value_set_string(value, priv->password);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void owr_candidate_finalize(GObject *object)
{
    OwrCandidatePrivate *priv;

    priv = OWR_CANDIDATE(object)->priv;
    g_free(priv->address);
    g_free(priv->base_address);
    g_free(priv->foundation);
    g_free(priv->ufrag);
    g_free(priv->password);

    G_OBJECT_CLASS(owr_candidate_parent_class)->finalize(object);
}

static void owr_candidate_class_init(OwrCandidateClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrCandidatePrivate));

    gobject_class->set_property = owr_candidate_set_property;
    gobject_class->get_property = owr_candidate_get_property;
    gobject_class->finalize = owr_candidate_finalize;

    obj_properties[PROP_TYPE] = g_param_spec_enum("type", "Candidate type",
        "The type of candidate",
        OWR_TYPE_CANDIDATE_TYPE, OWR_CANDIDATE_TYPE_HOST,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_COMPONENT_TYPE] = g_param_spec_enum("component-type", "Component type",
        "The stream component type (RTP/RTCP)",
        OWR_TYPE_COMPONENT_TYPE, OWR_COMPONENT_TYPE_RTP,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_TRANSPORT_TYPE] = g_param_spec_enum("transport-type", "Transport type",
        "The transport type (UDP or TCP (active/passive/simultaneous open))",
        OWR_TYPE_TRANSPORT_TYPE, OWR_TRANSPORT_TYPE_UDP,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_ADDRESS] = g_param_spec_string("address", "Candidate address",
        "The address of the candidate", "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_PORT] = g_param_spec_uint("port", "Candidate port",
        "The candidate port", 0, 65535, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_BASE_ADDRESS] = g_param_spec_string("base-address",
        "Candidate base address", "The base address of the candidate", "",
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_BASE_PORT] = g_param_spec_uint("base-port", "Candidate base port",
        "The candidate base port", 0, 65535, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_PRIORITY] = g_param_spec_uint("priority", "Candidate priority",
        "The candidate priority", 0, G_MAXUINT32, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_FOUNDATION] = g_param_spec_string("foundation", "Candidate foundation",
        "The foundation of the candidate", "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_UFRAG] = g_param_spec_string("ufrag", "Username fragment",
        "The username fragment to use with this candidate", "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_PASSWORD] = g_param_spec_string("password", "Password",
        "The password to use with this candidate", "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_candidate_init(OwrCandidate *candidate)
{
    OwrCandidatePrivate *priv;

    candidate->priv = priv = OWR_CANDIDATE_GET_PRIVATE(candidate);

    priv->type = OWR_CANDIDATE_TYPE_HOST;
    priv->component_type = OWR_COMPONENT_TYPE_RTP;
    priv->transport_type = OWR_TRANSPORT_TYPE_UDP;
    priv->address = g_new0(gchar, 1);
    priv->port = 0;
    priv->base_address = g_new0(gchar, 1);
    priv->base_port = 0;
    priv->priority = 0;
    priv->foundation = g_new0(gchar, 1);
    priv->ufrag = g_new0(gchar, 1);
    priv->password = g_new0(gchar, 1);
}

/**
 * owr_candidate_new:
 * @type: the type of candidate. See #OwrCandidateType.
 * @component_type: the type of component. See #OwrComponentType.
 *
 * Creates a new OwrCandidate
 */
OwrCandidate * owr_candidate_new(OwrCandidateType type, OwrComponentType component_type)
{
    return g_object_new(OWR_TYPE_CANDIDATE, "type", type, "component_type", component_type, NULL);
}

OwrCandidate * _owr_candidate_new_from_nice_candidate(NiceCandidate *nice_candidate)
{
    OwrCandidate *owr_candidate;
    OwrCandidateType candidate_type = -1;
    OwrComponentType component_type = -1;
    OwrTransportType transport_type = -1;
    gchar *address;
    guint port;

    g_return_val_if_fail(nice_candidate, NULL);

    switch (nice_candidate->type) {
    case NICE_CANDIDATE_TYPE_HOST:
        candidate_type = OWR_CANDIDATE_TYPE_HOST;
        break;

    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
        candidate_type = OWR_CANDIDATE_TYPE_SERVER_REFLEXIVE;
        break;

    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
        candidate_type = OWR_CANDIDATE_TYPE_PEER_REFLEXIVE;
        break;

    case NICE_CANDIDATE_TYPE_RELAYED:
        candidate_type = OWR_CANDIDATE_TYPE_RELAY;
        break;
    }
    g_return_val_if_fail(candidate_type != (OwrCandidateType)-1, NULL);

    switch (nice_candidate->component_id) {
    case NICE_COMPONENT_TYPE_RTP:
        component_type = OWR_COMPONENT_TYPE_RTP;
        break;

    case NICE_COMPONENT_TYPE_RTCP:
        component_type = OWR_COMPONENT_TYPE_RTCP;
        break;
    }
    g_return_val_if_fail(component_type != (OwrComponentType)-1, NULL);

    switch (nice_candidate->transport) {
    case NICE_CANDIDATE_TRANSPORT_UDP:
        transport_type = OWR_TRANSPORT_TYPE_UDP;
        break;

    case NICE_CANDIDATE_TRANSPORT_TCP_ACTIVE:
        transport_type = OWR_TRANSPORT_TYPE_TCP_ACTIVE;
        break;

    case NICE_CANDIDATE_TRANSPORT_TCP_PASSIVE:
        transport_type = OWR_TRANSPORT_TYPE_TCP_PASSIVE;
        break;

    case NICE_CANDIDATE_TRANSPORT_TCP_SO:
        transport_type = OWR_TRANSPORT_TYPE_TCP_SO;
        break;
    }
    g_return_val_if_fail(transport_type != (OwrTransportType)-1, NULL);

    owr_candidate = owr_candidate_new(candidate_type, component_type);

    g_object_set(G_OBJECT(owr_candidate), "transport-type", transport_type, NULL);

    address = g_new0(gchar, NICE_ADDRESS_STRING_LEN);
    nice_address_to_string(&nice_candidate->addr, address);
    g_object_set(G_OBJECT(owr_candidate), "address", address, NULL);
    g_free(address);

    port = nice_address_get_port(&nice_candidate->addr);
    g_object_set(G_OBJECT(owr_candidate), "port", port, NULL);

    address = g_new0(gchar, NICE_ADDRESS_STRING_LEN);
    nice_address_to_string(&nice_candidate->base_addr, address);
    g_object_set(G_OBJECT(owr_candidate), "base-address", address, NULL);
    g_free(address);

    port = nice_address_get_port(&nice_candidate->base_addr);
    g_object_set(G_OBJECT(owr_candidate), "base-port", port, NULL);

    g_object_set(G_OBJECT(owr_candidate), "priority", nice_candidate->priority, NULL);
    g_object_set(G_OBJECT(owr_candidate), "foundation", nice_candidate->foundation, NULL);

    g_object_set(G_OBJECT(owr_candidate), "ufrag", nice_candidate->username, NULL);
    g_object_set(G_OBJECT(owr_candidate), "password", nice_candidate->password, NULL);

    return owr_candidate;
}

NiceCandidate * _owr_candidate_to_nice_candidate(OwrCandidate *candidate)
{
    OwrCandidatePrivate *priv;
    NiceCandidate *nice_candidate;
    NiceCandidateType candidate_type;
    NiceComponentType component_type;
    NiceCandidateTransport transport;

    g_return_val_if_fail(candidate, NULL);

    priv = candidate->priv;

    switch (priv->type) {
    case OWR_CANDIDATE_TYPE_HOST:
        candidate_type = NICE_CANDIDATE_TYPE_HOST;
        break;

    case OWR_CANDIDATE_TYPE_SERVER_REFLEXIVE:
        candidate_type = NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
        break;

    case OWR_CANDIDATE_TYPE_PEER_REFLEXIVE:
        candidate_type = NICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
        break;

    case OWR_CANDIDATE_TYPE_RELAY:
        candidate_type = NICE_CANDIDATE_TYPE_RELAYED;
        break;

    default:
        g_return_val_if_reached(NULL);
    }

    switch (priv->component_type) {
    case OWR_COMPONENT_TYPE_RTP:
        component_type = NICE_COMPONENT_TYPE_RTP;
        break;

    case OWR_COMPONENT_TYPE_RTCP:
        component_type = NICE_COMPONENT_TYPE_RTCP;
        break;

    default:
        g_return_val_if_reached(NULL);
    }

    switch (priv->transport_type) {
    case OWR_TRANSPORT_TYPE_UDP:
        transport = NICE_CANDIDATE_TRANSPORT_UDP;
        break;

    case OWR_TRANSPORT_TYPE_TCP_ACTIVE:
        transport = NICE_CANDIDATE_TRANSPORT_TCP_ACTIVE;
        break;

    case OWR_TRANSPORT_TYPE_TCP_PASSIVE:
        transport = NICE_CANDIDATE_TRANSPORT_TCP_PASSIVE;
        break;

    case OWR_TRANSPORT_TYPE_TCP_SO:
        transport = NICE_CANDIDATE_TRANSPORT_TCP_SO;
        break;

    default:
        g_return_val_if_reached(NULL);
    }

    g_return_val_if_fail(priv->address && strlen(priv->address) > 0, NULL);
    g_return_val_if_fail(priv->port || transport == NICE_CANDIDATE_TRANSPORT_TCP_ACTIVE, NULL);

    nice_candidate = nice_candidate_new(candidate_type);
    nice_candidate->transport = NICE_CANDIDATE_TRANSPORT_UDP;
    nice_candidate->component_id = component_type;
    nice_candidate->transport = transport;

    nice_address_set_from_string(&nice_candidate->addr, priv->address);
    nice_address_set_port(&nice_candidate->addr, priv->port);

    if (priv->base_address && strlen(priv->base_address) > 0)
        nice_address_set_from_string(&nice_candidate->base_addr, priv->base_address);
    if (priv->base_port)
        nice_address_set_port(&nice_candidate->base_addr, priv->base_port);

    if (priv->foundation && strlen(priv->foundation) > 0) {
        g_strlcpy((gchar *)&nice_candidate->foundation, priv->foundation,
            MIN(NICE_CANDIDATE_MAX_FOUNDATION, 1 + strlen(priv->foundation)));
    }

    nice_candidate->priority = candidate->priv->priority;

    if (priv->ufrag && strlen(priv->ufrag) > 0)
        nice_candidate->username = g_strdup(priv->ufrag);
    if (priv->password && strlen(priv->password) > 0)
        nice_candidate->password = g_strdup(priv->password);

    return nice_candidate;
}

OwrComponentType _owr_candidate_get_component_type(OwrCandidate *candidate)
{
    return candidate->priv->component_type;
}

GType owr_candidate_type_get_type(void)
{
    static const GEnumValue types[] = {
        {OWR_CANDIDATE_TYPE_HOST, "Host", "host"},
        {OWR_CANDIDATE_TYPE_SERVER_REFLEXIVE, "Server reflexive", "srflx"},
        {OWR_CANDIDATE_TYPE_PEER_REFLEXIVE, "Peer reflexive", "prflx"},
        {OWR_CANDIDATE_TYPE_RELAY, "Relay", "relay"},
        {0, NULL, NULL}
    };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *)&id)) {
        GType _id = g_enum_register_static("OwrCandidateTypes", types);
        g_once_init_leave((gsize *)&id, _id);
    }

    return id;
}

GType owr_component_type_get_type(void)
{
    static const GEnumValue types[] = {
        {OWR_COMPONENT_TYPE_RTP, "RTP", "rtp"},
        {OWR_COMPONENT_TYPE_RTCP, "RTCP", "rtcp"},
        {0, NULL, NULL}
    };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *)&id)) {
        GType _id = g_enum_register_static("OwrComponentTypes", types);
        g_once_init_leave((gsize *)&id, _id);
    }

    return id;
}

GType owr_transport_type_get_type(void)
{
    static const GEnumValue types[] = {
        {OWR_TRANSPORT_TYPE_UDP, "UDP", "UDP"},
        {OWR_TRANSPORT_TYPE_TCP_ACTIVE, "TCP active", "TCP-active"},
        {OWR_TRANSPORT_TYPE_TCP_PASSIVE, "TCP passive", "TCP-passive"},
        {OWR_TRANSPORT_TYPE_TCP_SO, "TCP so", "TCP-so"},
        {0, NULL, NULL}
    };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *)&id)) {
        GType _id = g_enum_register_static("OwrTransportTypes", types);
        g_once_init_leave((gsize *)&id, _id);
    }

    return id;
}
