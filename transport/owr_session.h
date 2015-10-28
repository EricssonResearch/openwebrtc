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

#ifndef __OWR_SESSION_H__
#define __OWR_SESSION_H__

#include "owr_candidate.h"

#include <glib-object.h>

G_BEGIN_DECLS

/* Note: OwrIceState mirrors NiceComponentState for our API */
typedef enum {
    OWR_ICE_STATE_DISCONNECTED,
    OWR_ICE_STATE_GATHERING,
    OWR_ICE_STATE_CONNECTING,
    OWR_ICE_STATE_CONNECTED,
    OWR_ICE_STATE_READY,
    OWR_ICE_STATE_FAILED,
    OWR_ICE_STATE_LAST
} OwrIceState;

#define OWR_TYPE_ICE_STATE (owr_ice_state_get_type())
GType owr_ice_state_get_type(void);

#define OWR_TYPE_SESSION            (owr_session_get_type())
#define OWR_SESSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_SESSION, OwrSession))
#define OWR_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_SESSION, OwrSessionClass))
#define OWR_IS_SESSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_SESSION))
#define OWR_IS_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_SESSION))
#define OWR_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_SESSION, OwrSessionClass))

typedef struct _OwrSession        OwrSession;
typedef struct _OwrSessionClass   OwrSessionClass;
typedef struct _OwrSessionPrivate OwrSessionPrivate;

struct _OwrSession {
    GObject parent_instance;

    /*< private >*/
    OwrSessionPrivate *priv;
};

/**
 * OwrSessionClass:
 * @parent_class: the GObject parent
 *
 * The session class.
 */
struct _OwrSessionClass {
    GObjectClass parent_class;

    /* signals */
    void (*on_new_candidate)(OwrSession *session, OwrCandidate *candidate);
    void (*on_candidate_gathering_done)(OwrSession *session);
};

GType owr_session_get_type(void) G_GNUC_CONST;


void owr_session_add_remote_candidate(OwrSession *session, OwrCandidate *candidate);
void owr_session_force_remote_candidate(OwrSession *session, OwrCandidate *candidate);
void owr_session_force_candidate_pair(OwrSession *session, OwrComponentType ctype,
        OwrCandidate *local_candidate, OwrCandidate *remote_candidate);
void owr_session_set_local_port(OwrSession *session, OwrComponentType ctype, guint port);

void _owr_session_get_candidate_pair(OwrSession *session, OwrComponentType ctype,
        OwrCandidate **local, OwrCandidate **remote);

G_END_DECLS

#endif /* __OWR_SESSION_H__ */
