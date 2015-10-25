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
\*\ OwrURISourceAgent
/*/

#ifndef __OWR_URI_SOURCE_AGENT_H__
#define __OWR_URI_SOURCE_AGENT_H__

#include "owr_types.h"
#include "owr_uri_source.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define OWR_TYPE_URI_SOURCE_AGENT            (owr_uri_source_agent_get_type())
#define OWR_URI_SOURCE_AGENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_URI_SOURCE_AGENT, OwrURISourceAgent))
#define OWR_URI_SOURCE_AGENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_URI_SOURCE_AGENT, OwrURISourceAgentClass))
#define OWR_IS_URI_SOURCE_AGENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_URI_SOURCE_AGENT))
#define OWR_IS_URI_SOURCE_AGENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_URI_SOURCE_AGENT))
#define OWR_URI_SOURCE_AGENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_URI_SOURCE_AGENT, OwrURISourceAgentClass))

typedef struct _OwrURISourceAgent        OwrURISourceAgent;
typedef struct _OwrURISourceAgentClass   OwrURISourceAgentClass;
typedef struct _OwrURISourceAgentPrivate OwrURISourceAgentPrivate;

struct _OwrURISourceAgent {
    GObject parent_instance;

    /*< private >*/
    OwrURISourceAgentPrivate *priv;
};

/**
 * OwrURISourceAgentClass:
 * @parent_class: the GObject parent
 * @on_new_source: emitted to notify of a new source
 *
 * The URI source agent class.
 */
struct _OwrURISourceAgentClass {
    GObjectClass parent_class;

    /* signals */
    void (*on_new_source)(OwrURISourceAgent *uri_source_agent, OwrMediaSource *source);
};

GType owr_uri_source_agent_get_type(void) G_GNUC_CONST;

OwrURISourceAgent * owr_uri_source_agent_new(const gchar *uri);
gchar * owr_uri_source_agent_get_dot_data(OwrURISourceAgent *uri_source_agent);

gboolean owr_uri_source_agent_play(OwrURISourceAgent *uri_source_agent);
gboolean owr_uri_source_agent_pause(OwrURISourceAgent *uri_source_agent);

G_END_DECLS

#endif /* __OWR_URI_SOURCE_AGENT_H__ */
