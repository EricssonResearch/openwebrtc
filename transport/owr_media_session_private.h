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

#ifndef __OWR_MEDIA_SESSION_PRIVATE_H__
#define __OWR_MEDIA_SESSION_PRIVATE_H__

#include "owr_media_session.h"

#include "owr_media_source.h"

#include <gst/gst.h>

#ifndef __GTK_DOC_IGNORE__

G_BEGIN_DECLS

OwrPayload * _owr_media_session_get_receive_payload(OwrMediaSession *media_session, guint32 payload_type);
OwrPayload * _owr_media_session_get_send_payload(OwrMediaSession *media_session);
OwrMediaSource * _owr_media_session_get_send_source(OwrMediaSession *media_session);

gboolean _owr_media_session_want_receive_rtx(OwrMediaSession *media_session);
GstStructure * _owr_media_session_get_receive_rtx_pt_map(OwrMediaSession *media_session);

void _owr_media_session_set_on_send_payload(OwrMediaSession *media_session, GClosure *on_send_payload);
void _owr_media_session_set_on_send_source(OwrMediaSession *media_session, GClosure *on_send_source);
void _owr_media_session_clear_closures(OwrMediaSession *media_session);

GstBuffer * _owr_media_session_get_srtp_key_buffer(OwrMediaSession *media_session, const gchar *keyname);

G_END_DECLS

#endif /* __GTK_DOC_IGNORE__ */

#endif /* __OWR_MEDIA_SESSION_PRIVATE_H__ */
