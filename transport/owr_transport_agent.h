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
\*\ OwrTransportAgent
/*/

#ifndef __OWR_TRANSPORT_AGENT_H__
#define __OWR_TRANSPORT_AGENT_H__

#include "owr_session.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum _OwrHelperServerType {
    OWR_HELPER_SERVER_TYPE_STUN,
    OWR_HELPER_SERVER_TYPE_TURN_UDP,
    OWR_HELPER_SERVER_TYPE_TURN_TCP,
    OWR_HELPER_SERVER_TYPE_TURN_TLS
} OwrHelperServerType;

#define OWR_TYPE_TRANSPORT_AGENT            (owr_transport_agent_get_type())
#define OWR_TRANSPORT_AGENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_TRANSPORT_AGENT, OwrTransportAgent))
#define OWR_TRANSPORT_AGENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_TRANSPORT_AGENT, OwrTransportAgentClass))
#define OWR_IS_TRANSPORT_AGENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_TRANSPORT_AGENT))
#define OWR_IS_TRANSPORT_AGENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_TRANSPORT_AGENT))
#define OWR_TRANSPORT_AGENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_TRANSPORT_AGENT, OwrTransportAgentClass))

typedef struct _OwrTransportAgent        OwrTransportAgent;
typedef struct _OwrTransportAgentClass   OwrTransportAgentClass;
typedef struct _OwrTransportAgentPrivate OwrTransportAgentPrivate;

struct _OwrTransportAgent {
    GObject parent_instance;

    /*< private >*/
    OwrTransportAgentPrivate *priv;
};

struct _OwrTransportAgentClass {
    GObjectClass parent_class;

};

GType owr_transport_agent_get_type(void) G_GNUC_CONST;

OwrTransportAgent * owr_transport_agent_new(gboolean ice_controlling_mode);
void owr_transport_agent_add_helper_server(OwrTransportAgent *transport_agent, OwrHelperServerType type,
    const gchar *address, guint port, const gchar *username, const gchar *password);
void owr_transport_agent_add_local_address(OwrTransportAgent *transport_agent, const gchar *local_address);
void owr_transport_agent_set_local_port_range(OwrTransportAgent *transport_agent, guint min_port, guint max_port);
void owr_transport_agent_add_session(OwrTransportAgent *agent, OwrSession *session);
gchar * owr_transport_agent_get_dot_data(OwrTransportAgent *transport_agent);

G_END_DECLS

#endif /* __OWR_TRANSPORT_AGENT_H__ */
