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
\*\ OwrImageServer
/*/

#ifndef __OWR_IMAGE_SERVER_H__
#define __OWR_IMAGE_SERVER_H__

#include "owr_image_renderer.h"

G_BEGIN_DECLS

#define OWR_TYPE_IMAGE_SERVER            (owr_image_server_get_type())
#define OWR_IMAGE_SERVER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_IMAGE_SERVER, OwrImageServer))
#define OWR_IMAGE_SERVER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_IMAGE_SERVER, OwrImageServerClass))
#define OWR_IS_IMAGE_SERVER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_IMAGE_SERVER))
#define OWR_IS_IMAGE_SERVER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_IMAGE_SERVER))
#define OWR_IMAGE_SERVER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_IMAGE_SERVER, OwrImageServerClass))

typedef struct _OwrImageServer        OwrImageServer;
typedef struct _OwrImageServerClass   OwrImageServerClass;
typedef struct _OwrImageServerPrivate OwrImageServerPrivate;

struct _OwrImageServer {
    GObject parent_instance;

    /*< private >*/
    OwrImageServerPrivate *priv;
};

struct _OwrImageServerClass {
    GObjectClass parent_class;
};

GType owr_image_server_get_type(void) G_GNUC_CONST;

OwrImageServer *owr_image_server_new(guint port);

void owr_image_server_add_image_renderer(OwrImageServer *image_server,
    OwrImageRenderer *image_renderer, const gchar *tag);

void owr_image_server_remove_image_renderer(OwrImageServer *image_server, const gchar *tag);

G_END_DECLS

#endif /* __OWR_IMAGE_SERVER_H__ */
