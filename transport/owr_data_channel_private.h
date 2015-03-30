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

#ifndef __OWR_DATA_CHANNEL_PRIVATE_H__
#define __OWR_DATA_CHANNEL_PRIVATE_H__

#include <glib.h>

#include <gst/gst.h>

#ifndef __GTK_DOC_IGNORE__

G_BEGIN_DECLS

/*< private >*/
void _owr_data_channel_set_on_send(OwrDataChannel *data_channel,
    GClosure *on_datachannel_send);
void _owr_data_channel_set_on_close(OwrDataChannel *data_channel,
    GClosure *on_datachannel_close);
void _owr_data_channel_set_ready_state(OwrDataChannel *data_channel, OwrDataChannelReadyState state);
void _owr_data_channel_set_on_request_bytes_sent(OwrDataChannel *data_channel,
    GClosure *on_request_bytes_sent);
void _owr_data_channel_clear_closures(OwrDataChannel *data_channel);
GstCaps * _owr_data_channel_create_caps(OwrDataChannel *data_channel);

G_END_DECLS

#endif /* __GTK_DOC_IGNORE__ */

#endif /* __OWR_DATA_CHANNEL_PRIVATE_H__ */
