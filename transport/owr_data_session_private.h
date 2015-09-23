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

#ifndef __OWR_DATA_SESSION_PRIVATE_H__
#define __OWR_DATA_SESSION_PRIVATE_H__

#include "owr_media_session.h"

#include <gst/gst.h>

#ifndef __GTK_DOC_IGNORE__

G_BEGIN_DECLS

void _owr_data_session_clear_closures(OwrDataSession *data_session);
GstElement * _owr_data_session_create_decoder(OwrDataSession *data_session);
GstElement * _owr_data_session_create_encoder(OwrDataSession *data_session);
void _owr_data_session_set_on_datachannel_added(OwrDataSession *data_session,
    GClosure *on_datachannel_added);
void _owr_data_session_set_on_sctp_port_set(OwrDataSession *data_session, GClosure *on_port_set);
OwrDataChannel * _owr_data_session_get_datachannel(OwrDataSession *data_session, guint16 id);
GList * _owr_data_session_get_datachannels(OwrDataSession *data_session);
gchar * _owr_data_session_get_encoder_name(OwrDataSession *data_session);
gchar * _owr_data_session_get_decoder_name(OwrDataSession *data_session);

G_END_DECLS

#endif /* #ifndef __GTK_DOC_IGNORE__ */

#endif /* __OWR_MEDIA_SESSION_PRIVATE_H__ */
