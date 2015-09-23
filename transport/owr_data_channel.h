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
\*\ OwrDataChannel
/*/

#ifndef __OWR_DATA_CHANNEL_H__
#define __OWR_DATA_CHANNEL_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
    OWR_DATA_CHANNEL_READY_STATE_CONNECTING,
    OWR_DATA_CHANNEL_READY_STATE_OPEN,
    OWR_DATA_CHANNEL_READY_STATE_CLOSING,
    OWR_DATA_CHANNEL_READY_STATE_CLOSED
} OwrDataChannelReadyState;

#define OWR_DATA_CHANNEL_READY_STATE_TYPE (owr_data_channel_ready_state_get_type())
GType owr_data_channel_ready_state_get_type(void);

#define OWR_TYPE_DATA_CHANNEL            (owr_data_channel_get_type())
#define OWR_DATA_CHANNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_DATA_CHANNEL, OwrDataChannel))
#define OWR_DATA_CHANNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_DATA_CHANNEL, OwrDataChannelClass))
#define OWR_IS_DATA_CHANNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_DATA_CHANNEL))
#define OWR_IS_DATA_CHANNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_DATA_CHANNEL))
#define OWR_DATA_CHANNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_DATA_CHANNEL, OwrDataChannelClass))

typedef struct _OwrDataChannel        OwrDataChannel;
typedef struct _OwrDataChannelClass   OwrDataChannelClass;
typedef struct _OwrDataChannelPrivate OwrDataChannelPrivate;

struct _OwrDataChannel {
    GObject parent_instance;

    /*< private >*/
    OwrDataChannelPrivate *priv;
};

struct _OwrDataChannelClass {
    GObjectClass parent_class;

    void (*on_data)(const guint8 *data);
    void (*on_binary_data)(const guint8 *data, guint length);
};

GType owr_data_channel_get_type(void) G_GNUC_CONST;

OwrDataChannel * owr_data_channel_new(gboolean ordered, gint max_packet_life_time,
    gint max_retransmits, const gchar *protocol, gboolean negotiated, guint16 id,
    const gchar *label);
void owr_data_channel_send(OwrDataChannel *data_channel, const gchar *data);
void owr_data_channel_send_binary(OwrDataChannel *data_channel, const guint8 *data,
    guint16 length);
void owr_data_channel_close(OwrDataChannel *data_channel);

G_END_DECLS

#endif /* __OWR_DATA_CHANNEL_H__ */
