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
\*\ OwrGstAudioRenderer
/*/

#ifndef __OWR_GST_AUDIO_RENDERER_H__
#define __OWR_GST_AUDIO_RENDERER_H__

#include "owr_audio_renderer.h"

#include <gst/gst.h>

G_BEGIN_DECLS

#define OWR_GST_TYPE_AUDIO_RENDERER            (owr_gst_audio_renderer_get_type())
#define OWR_GST_AUDIO_RENDERER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_GST_TYPE_AUDIO_RENDERER, OwrGstAudioRenderer))
#define OWR_GST_AUDIO_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_GST_TYPE_AUDIO_RENDERER, OwrGstAudioRendererClass))
#define OWR_GST_IS_AUDIO_RENDERER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_GST_TYPE_AUDIO_RENDERER))
#define OWR_GST_IS_AUDIO_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_GST_TYPE_AUDIO_RENDERER))
#define OWR_GST_AUDIO_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_GST_TYPE_AUDIO_RENDERER, OwrGstAudioRendererClass))

typedef struct _OwrGstAudioRenderer        OwrGstAudioRenderer;
typedef struct _OwrGstAudioRendererClass   OwrGstAudioRendererClass;
typedef struct _OwrGstAudioRendererPrivate OwrGstAudioRendererPrivate;

struct _OwrGstAudioRenderer {
    OwrAudioRenderer parent_instance;

    /*< private >*/
    OwrGstAudioRendererPrivate *priv;
};

struct _OwrGstAudioRendererClass {
    OwrAudioRendererClass parent_class;
};

GType owr_gst_audio_renderer_get_type(void) G_GNUC_CONST;

OwrGstAudioRenderer *owr_gst_audio_renderer_new(GstElement* sink);

G_END_DECLS

#endif /* __OWR_GST_AUDIO_RENDERER_H__ */
