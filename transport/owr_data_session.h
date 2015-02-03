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
\*\ OwrDataSession
/*/

#ifndef __OWR_DATA_SESSION_H__
#define __OWR_DATA_SESSION_H__

#include "owr_data_channel.h"
#include "owr_session.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define OWR_TYPE_DATA_SESSION            (owr_data_session_get_type())
#define OWR_DATA_SESSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_DATA_SESSION, OwrDataSession))
#define OWR_DATA_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_DATA_SESSION, OwrDataSessionClass))
#define OWR_IS_DATA_SESSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_DATA_SESSION))
#define OWR_IS_DATA_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_DATA_SESSION))
#define OWR_DATA_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_DATA_SESSION, OwrDataSessionClass))

typedef struct _OwrDataSession        OwrDataSession;
typedef struct _OwrDataSessionClass   OwrDataSessionClass;
typedef struct _OwrDataSessionPrivate OwrDataSessionPrivate;

struct _OwrDataSession {
    OwrSession parent_instance;

    /*< private >*/
    OwrDataSessionPrivate *priv;
};

/**
 * OwrDataSessionClass:
 * @parent_class: the GObject parent
 *
 * The data session class.
 */
struct _OwrDataSessionClass {
    OwrSessionClass parent_class;

    /* signals */
    void (*on_data_channel_requested)(gboolean ordered, gint max_packet_life_time,
        gint max_retransmits, const gchar *protocol, gboolean negotiated, guint16 id,
        const gchar *label);
};

GType owr_data_session_get_type(void) G_GNUC_CONST;


OwrDataSession * owr_data_session_new(gboolean dtls_client_mode);

void owr_data_session_add_data_channel(OwrDataSession *data_session, OwrDataChannel *data_channel);

G_END_DECLS

#endif /* __OWR_DATA_SESSION_H__ */
