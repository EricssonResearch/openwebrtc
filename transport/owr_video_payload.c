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
\*\ OwrVideoPayload
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_video_payload.h"

#include "owr_types.h"

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN(_owrvideopayload_debug);
#define GST_CAT_DEFAULT _owrvideopayload_debug

#define DEFAULT_CCM_FIR FALSE
#define DEFAULT_NACK_PLI FALSE
#define DEFAULT_WIDTH 0
#define DEFAULT_HEIGHT 0
#define DEFAULT_FRAMERATE 0.0
#define DEFAULT_ROTATION 0
#define DEFAULT_MIRROR FALSE

#define OWR_VIDEO_PAYLOAD_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_VIDEO_PAYLOAD, OwrVideoPayloadPrivate))

G_DEFINE_TYPE(OwrVideoPayload, owr_video_payload, OWR_TYPE_PAYLOAD)

struct _OwrVideoPayloadPrivate {
    gboolean ccm_fir;
    gboolean nack_pli;
    guint width;
    guint height;
    gdouble framerate;
    gint rotation;
    gboolean mirror;
};


enum {
    PROP_0,

    PROP_CCM_FIR,
    PROP_NACK_PLI,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FRAMERATE,
    PROP_ROTATION,
    PROP_MIRROR,

    N_PROPERTIES,

    /* override properties */
    PROP_MEDIA_TYPE
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void owr_video_payload_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    OwrVideoPayloadPrivate *priv;

    g_return_if_fail(object);
    g_return_if_fail(value);
    g_return_if_fail(pspec);

    priv = OWR_VIDEO_PAYLOAD(object)->priv;

    switch (property_id) {
    case PROP_CCM_FIR:
        priv->ccm_fir = g_value_get_boolean(value);
        break;

    case PROP_NACK_PLI:
        priv->nack_pli = g_value_get_boolean(value);
        break;

    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;

    case PROP_HEIGHT:
        priv->height = g_value_get_uint(value);
        break;

    case PROP_FRAMERATE:
        priv->framerate = g_value_get_double(value);
        break;

    case PROP_ROTATION:
        priv->rotation = g_value_get_uint(value);
        break;

    case PROP_MIRROR:
        priv->mirror = g_value_get_boolean(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_video_payload_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    OwrVideoPayloadPrivate *priv;

    g_return_if_fail(object);
    g_return_if_fail(value);
    g_return_if_fail(pspec);

    priv = OWR_VIDEO_PAYLOAD(object)->priv;

    switch (property_id) {
    case PROP_CCM_FIR:
        g_value_set_boolean(value, priv->ccm_fir);
        break;

    case PROP_NACK_PLI:
        g_value_set_boolean(value, priv->nack_pli);
        break;

    case PROP_WIDTH:
        g_value_set_uint(value, priv->width);
        break;

    case PROP_HEIGHT:
        g_value_set_uint(value, priv->height);
        break;

    case PROP_FRAMERATE:
        g_value_set_double(value, priv->framerate);
        break;

    case PROP_ROTATION:
        g_value_set_uint(value, priv->rotation);
        break;

    case PROP_MIRROR:
        g_value_set_boolean(value, priv->mirror);
        break;

    case PROP_MEDIA_TYPE:
        g_value_set_enum(value, OWR_MEDIA_TYPE_VIDEO);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_video_payload_class_init(OwrVideoPayloadClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    g_type_class_add_private(klass, sizeof(OwrVideoPayloadPrivate));

    gobject_class->set_property = owr_video_payload_set_property;
    gobject_class->get_property = owr_video_payload_get_property;

    g_object_class_override_property(gobject_class, PROP_MEDIA_TYPE, "media-type");

    obj_properties[PROP_CCM_FIR] = g_param_spec_boolean(
        "ccm-fir", "CCM FIR",
        "Whether to support CCM FIR RTCP-FB messages",
        DEFAULT_CCM_FIR,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_NACK_PLI] = g_param_spec_boolean(
        "nack-pli", "NACK PLI",
        "Whether to support NACK PLI RTCP-FB messages",
        DEFAULT_NACK_PLI,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_WIDTH] = g_param_spec_uint("width", "width",
        "Video width in pixels",
        0, G_MAXUINT, DEFAULT_WIDTH,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_HEIGHT] = g_param_spec_uint("height", "height",
        "Video height in pixels",
        0, G_MAXUINT, DEFAULT_HEIGHT,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_FRAMERATE] = g_param_spec_double("framerate", "framerate",
        "Video frames per second",
        0.0, G_MAXDOUBLE, DEFAULT_FRAMERATE,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_ROTATION] = g_param_spec_uint("rotation", "rotation",
        "Clockwise video rotation in multiple of 90 degrees"
        " (NOTE: currently only works for send payloads)", 0, 3, DEFAULT_ROTATION,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_MIRROR] = g_param_spec_boolean("mirror", "mirror",
        "Whether the video should be mirrored around the y-axis "
        "(NOTE: currently only works for send payloads)", DEFAULT_MIRROR,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_video_payload_init(OwrVideoPayload *video_payload)
{
    video_payload->priv = OWR_VIDEO_PAYLOAD_GET_PRIVATE(video_payload);
    video_payload->priv->ccm_fir = DEFAULT_CCM_FIR;
    video_payload->priv->nack_pli = DEFAULT_NACK_PLI;
    video_payload->priv->width = DEFAULT_WIDTH;
    video_payload->priv->height = DEFAULT_HEIGHT;
    video_payload->priv->framerate = DEFAULT_FRAMERATE;
    video_payload->priv->rotation = DEFAULT_ROTATION;
    video_payload->priv->mirror = DEFAULT_MIRROR;
}

OwrPayload * owr_video_payload_new(OwrCodecType codec_type, guint payload_type, guint clock_rate,
    gboolean ccm_fir, gboolean nack_pli)
{
    OwrPayload *payload = g_object_new(OWR_TYPE_VIDEO_PAYLOAD,
        "codec-type", codec_type,
        "payload-type", payload_type,
        "clock-rate", clock_rate,
        "ccm-fir", ccm_fir,
        "nack-pli", nack_pli,
        NULL);
    return payload;
}
