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
\*\ OwrAudioPayload
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_audio_payload.h"

#include "owr_types.h"

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN(_owraudiopayload_debug);
#define GST_CAT_DEFAULT _owraudiopayload_debug

#define DEFAULT_CHANNELS 0
#define DEFAULT_PTIME 40000000L

#define OWR_AUDIO_PAYLOAD_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_AUDIO_PAYLOAD, OwrAudioPayloadPrivate))

G_DEFINE_TYPE(OwrAudioPayload, owr_audio_payload, OWR_TYPE_PAYLOAD)

struct _OwrAudioPayloadPrivate {
    guint channels;
    guint ptime;
};

enum {
    PROP_0,

    PROP_CHANNELS,
    PROP_PTIME,

    N_PROPERTIES,

    /* override properties */
    PROP_MEDIA_TYPE
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static void owr_audio_payload_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    OwrAudioPayloadPrivate *priv;

    g_return_if_fail(object);
    g_return_if_fail(value);
    g_return_if_fail(pspec);

    priv = OWR_AUDIO_PAYLOAD(object)->priv;

    switch (property_id) {
    case PROP_CHANNELS:
        priv->channels = g_value_get_uint(value);
        break;

    case PROP_PTIME:
        priv->ptime = g_value_get_uint(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_audio_payload_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    OwrAudioPayloadPrivate *priv;

    g_return_if_fail(object);
    g_return_if_fail(value);
    g_return_if_fail(pspec);

    priv = OWR_AUDIO_PAYLOAD(object)->priv;

    switch (property_id) {
    case PROP_CHANNELS:
        g_value_set_uint(value, priv->channels);
        break;

    case PROP_PTIME:
        g_value_set_uint(value, priv->ptime);
        break;

    case PROP_MEDIA_TYPE:
        g_value_set_enum(value, OWR_MEDIA_TYPE_AUDIO);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_audio_payload_class_init(OwrAudioPayloadClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    g_type_class_add_private(klass, sizeof(OwrAudioPayloadPrivate));

    gobject_class->set_property = owr_audio_payload_set_property;
    gobject_class->get_property = owr_audio_payload_get_property;

    g_object_class_override_property(gobject_class, PROP_MEDIA_TYPE, "media-type");

    obj_properties[PROP_CHANNELS] = g_param_spec_uint(
        "channels",
        "Channels",
        "The number of audio channels (0 means whatever is provided by the source)",
        0 /* min */,
        G_MAXUINT /* max */,
        DEFAULT_CHANNELS,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_PTIME] = g_param_spec_uint(
        "ptime",
        "Packetization time",
        "The packetization time in nanoseconds",
        0 /* min */,
        G_MAXUINT /* max */,
        DEFAULT_PTIME,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_audio_payload_init(OwrAudioPayload *audio_payload)
{
    audio_payload->priv = OWR_AUDIO_PAYLOAD_GET_PRIVATE(audio_payload);
    audio_payload->priv->channels = DEFAULT_CHANNELS;
    audio_payload->priv->ptime = DEFAULT_PTIME;
}

OwrPayload * owr_audio_payload_new(OwrCodecType codec_type, guint payload_type, guint clock_rate, guint channels)
{
    OwrPayload *payload = g_object_new(OWR_TYPE_AUDIO_PAYLOAD,
        "codec-type", codec_type,
        "payload-type", payload_type,
        "clock-rate", clock_rate,
        "channels", channels,
        NULL);
    return payload;
}
