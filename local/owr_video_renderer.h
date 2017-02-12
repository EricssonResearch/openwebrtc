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
\*\ OwrVideoRenderer
/*/

#ifndef __OWR_VIDEO_RENDERER_H__
#define __OWR_VIDEO_RENDERER_H__

#include "owr_media_renderer.h"

G_BEGIN_DECLS

#define OWR_TYPE_VIDEO_RENDERER            (owr_video_renderer_get_type())
#define OWR_VIDEO_RENDERER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_VIDEO_RENDERER, OwrVideoRenderer))
#define OWR_VIDEO_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_VIDEO_RENDERER, OwrVideoRendererClass))
#define OWR_IS_VIDEO_RENDERER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_VIDEO_RENDERER))
#define OWR_IS_VIDEO_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_VIDEO_RENDERER))
#define OWR_VIDEO_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_VIDEO_RENDERER, OwrVideoRendererClass))

typedef struct _OwrVideoRenderer        OwrVideoRenderer;
typedef struct _OwrVideoRendererClass   OwrVideoRendererClass;
typedef struct _OwrVideoRendererPrivate OwrVideoRendererPrivate;

struct _OwrVideoRenderer {
    OwrMediaRenderer parent_instance;

    /*< private >*/
    OwrVideoRendererPrivate *priv;
};

struct _OwrVideoRendererClass {
    OwrMediaRendererClass parent_class;
};

GType owr_video_renderer_get_type(void) G_GNUC_CONST;

OwrVideoRenderer *owr_video_renderer_new(const gchar *tag);

typedef struct _GstContext OwrGstContext;
typedef OwrGstContext * (* OwrVideoRendererRequestContextCallback) (const gchar *context_type, gpointer user_data);
void owr_video_renderer_set_request_context_callback(OwrVideoRenderer *renderer, OwrVideoRendererRequestContextCallback callback, gpointer user_data, GDestroyNotify destroy_data);

G_END_DECLS

#endif /* __OWR_VIDEO_RENDERER_H__ */
