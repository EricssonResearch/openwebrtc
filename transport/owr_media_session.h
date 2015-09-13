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
\*\ OwrMediaSession
/*/

#ifndef __OWR_MEDIA_SESSION_H__
#define __OWR_MEDIA_SESSION_H__

#include "owr_media_source.h"
#include "owr_payload.h"
#include "owr_remote_media_source.h"
#include "owr_session.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define OWR_TYPE_MEDIA_SESSION            (owr_media_session_get_type())
#define OWR_MEDIA_SESSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_MEDIA_SESSION, OwrMediaSession))
#define OWR_MEDIA_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_MEDIA_SESSION, OwrMediaSessionClass))
#define OWR_IS_MEDIA_SESSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_MEDIA_SESSION))
#define OWR_IS_MEDIA_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_MEDIA_SESSION))
#define OWR_MEDIA_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_MEDIA_SESSION, OwrMediaSessionClass))

typedef struct _OwrMediaSession        OwrMediaSession;
typedef struct _OwrMediaSessionClass   OwrMediaSessionClass;
typedef struct _OwrMediaSessionPrivate OwrMediaSessionPrivate;

struct _OwrMediaSession {
    OwrSession parent_instance;

    /*< private >*/
    OwrMediaSessionPrivate *priv;
};

/**
 * OwrMediaSessionClass:
 * @parent_class: the GObject parent
 * @on_new_stats: emitted when there's new stats
 * @on_incoming_source: emitted to notify of a new incoming source
 *
 * The session class.
 */
struct _OwrMediaSessionClass {
    OwrSessionClass parent_class;

    /* signals */
    void (*on_new_stats)(OwrMediaSession *media_session, GHashTable *stats);
    void (*on_incoming_source)(OwrMediaSession *media_session, OwrRemoteMediaSource *source);
};

GType owr_media_session_get_type(void) G_GNUC_CONST;


OwrMediaSession * owr_media_session_new(gboolean dtls_client_mode);
void owr_media_session_add_receive_payload(OwrMediaSession *media_session, OwrPayload *payload);
void owr_media_session_set_send_payload(OwrMediaSession *media_session, OwrPayload *payload);
void owr_media_session_set_send_source(OwrMediaSession *media_session, OwrMediaSource *source);

G_END_DECLS

#endif /* __OWR_MEDIA_SESSION_H__ */
