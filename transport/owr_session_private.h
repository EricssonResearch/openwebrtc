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

#ifndef __OWR_SESSION_PRIVATE_H__
#define __OWR_SESSION_PRIVATE_H__

#include "owr_session.h"

#include "owr_types.h"

#ifndef __GTK_DOC_IGNORE__

G_BEGIN_DECLS

GSList * _owr_session_get_remote_candidates(OwrSession *session);
GSList * _owr_session_get_forced_remote_candidates(OwrSession *session);

guint _owr_session_get_local_port(OwrSession *session, OwrComponentType ctype);

void _owr_session_set_on_remote_candidate(OwrSession *session, GClosure *on_remote_candidate);
void _owr_session_set_on_local_candidate_change(OwrSession *session, GClosure *on_local_candidate_change);
void _owr_session_clear_closures(OwrSession *session);

void _owr_session_set_dtls_peer_certificate(OwrSession *, const gchar *certificate);

void _owr_session_emit_ice_state_changed(OwrSession *session, guint session_id,
	OwrComponentType component_type, OwrIceState state);

G_END_DECLS

#endif /* __GTK_DOC_IGNORE__ */

#endif /* __OWR_SESSION_PRIVATE_H__ */
