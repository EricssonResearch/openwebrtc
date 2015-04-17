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
\*\ OwrMediaSource
/*/

#ifndef __OWR_MEDIA_SOURCE_H__
#define __OWR_MEDIA_SOURCE_H__

#include "owr_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define OWR_TYPE_MEDIA_SOURCE            (owr_media_source_get_type())
#define OWR_MEDIA_SOURCE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_MEDIA_SOURCE, OwrMediaSource))
#define OWR_MEDIA_SOURCE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_MEDIA_SOURCE, OwrMediaSourceClass))
#define OWR_IS_MEDIA_SOURCE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_MEDIA_SOURCE))
#define OWR_IS_MEDIA_SOURCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_MEDIA_SOURCE))
#define OWR_MEDIA_SOURCE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_MEDIA_SOURCE, OwrMediaSourceClass))

typedef struct _OwrMediaSource        OwrMediaSource;
typedef struct _OwrMediaSourceClass   OwrMediaSourceClass;
typedef struct _OwrMediaSourcePrivate OwrMediaSourcePrivate;

struct _OwrMediaSource {
    GObject parent_instance;

    /*< private >*/
    OwrMediaSourcePrivate *priv;
    GMutex lock;
};

struct _OwrMediaSourceClass {
    GObjectClass parent_class;

    /*< private >*/
    void *(*request_source)(OwrMediaSource *media_source, void *caps);
    void  (*release_source)(OwrMediaSource *media_source, void *source);
};

GType owr_media_source_get_type(void) G_GNUC_CONST;
gchar * owr_media_source_get_dot_data(OwrMediaSource *source);

G_END_DECLS

#endif /* __OWR_MEDIA_SOURCE_H__ */
