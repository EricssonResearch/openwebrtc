/*
 * Copyright (c) 2015, Igalia S.L
 *     Author: Philippe Normand <philn@igalia.com>
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
\*\ OwrGstVideoRenderer
/*/

#ifndef __OWR_GST_VIDEO_RENDERER_H__
#define __OWR_GST_VIDEO_RENDERER_H__

#include "owr_video_renderer.h"

#include <gst/gst.h>

G_BEGIN_DECLS

#define OWR_GST_TYPE_VIDEO_RENDERER            (owr_gst_video_renderer_get_type())
#define OWR_GST_VIDEO_RENDERER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_GST_TYPE_VIDEO_RENDERER, OwrGstVideoRenderer))
#define OWR_GST_VIDEO_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_GST_TYPE_VIDEO_RENDERER, OwrGstVideoRendererClass))
#define OWR_GST_IS_VIDEO_RENDERER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_GST_TYPE_VIDEO_RENDERER))
#define OWR_GST_IS_VIDEO_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_GST_TYPE_VIDEO_RENDERER))
#define OWR_GST_VIDEO_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_GST_TYPE_VIDEO_RENDERER, OwrGstVideoRendererClass))

typedef struct _OwrGstVideoRenderer        OwrGstVideoRenderer;
typedef struct _OwrGstVideoRendererClass   OwrGstVideoRendererClass;
typedef struct _OwrGstVideoRendererPrivate OwrGstVideoRendererPrivate;

struct _OwrGstVideoRenderer {
    OwrVideoRenderer parent_instance;

    /*< private >*/
    OwrGstVideoRendererPrivate *priv;
};

struct _OwrGstVideoRendererClass {
    OwrVideoRendererClass parent_class;
};

GType owr_gst_video_renderer_get_type(void) G_GNUC_CONST;

OwrGstVideoRenderer *owr_gst_video_renderer_new(GstElement* sink);

G_END_DECLS

#endif /* __OWR_GST_VIDEO_RENDERER_H__ */
