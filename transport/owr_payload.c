/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
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
\*\ OwrPayload
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_payload.h"

#include "owr_audio_payload.h"
#include "owr_payload_private.h"
#include "owr_types.h"
#include "owr_video_payload.h"

#define DEFAULT_MTU 1200
#define DEFAULT_BITRATE 0

#define LIMITED_WIDTH 640
#define LIMITED_HEIGHT 480
#define LIMITED_FRAMERATE 30.0

#define TARGET_BITS_PER_PIXEL 0.2 /* 640x360 15fps @ 768kbps =~ 0.2bpp */

#define OWR_PAYLOAD_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_PAYLOAD, OwrPayloadPrivate))

G_DEFINE_TYPE(OwrPayload, owr_payload, G_TYPE_OBJECT)

struct _OwrPayloadPrivate {
    OwrMediaType media_type;
    OwrCodecType codec_type;
    guint payload_type;
    guint clock_rate;
    guint mtu;
    guint bitrate;
};


enum {
    PROP_0,
    PROP_MEDIA_TYPE,
    PROP_CODEC_TYPE,
    PROP_PAYLOAD_TYPE,
    PROP_CLOCK_RATE,
    PROP_MTU,
    PROP_BITRATE,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static guint get_unique_id();


static void owr_payload_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    OwrPayloadPrivate *priv;

    g_return_if_fail(object);
    g_return_if_fail(value);
    g_return_if_fail(pspec);

    priv = OWR_PAYLOAD(object)->priv;

    switch (property_id) {
    case PROP_MEDIA_TYPE:
        priv->media_type = g_value_get_uint(value);
        break;
    case PROP_CODEC_TYPE:
        priv->codec_type = g_value_get_uint(value);
        break;
    case PROP_PAYLOAD_TYPE:
        priv->payload_type = g_value_get_uint(value);
        break;
    case PROP_CLOCK_RATE:
        priv->clock_rate = g_value_get_uint(value);
        break;
    case PROP_MTU:
        priv->mtu = g_value_get_uint(value);
        break;
    case PROP_BITRATE:
        priv->bitrate = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_payload_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    OwrPayloadPrivate *priv;

    g_return_if_fail(object);
    g_return_if_fail(value);
    g_return_if_fail(pspec);

    priv = OWR_PAYLOAD(object)->priv;

    switch (property_id) {
    case PROP_MEDIA_TYPE:
        g_value_set_uint(value, priv->media_type);
        break;
    case PROP_CODEC_TYPE:
        g_value_set_uint(value, priv->codec_type);
        break;
    case PROP_PAYLOAD_TYPE:
        g_value_set_uint(value, priv->payload_type);
        break;
    case PROP_CLOCK_RATE:
        g_value_set_uint(value, priv->clock_rate);
        break;
    case PROP_MTU:
        g_value_set_uint(value, priv->mtu);
        break;
    case PROP_BITRATE:
        g_value_set_uint(value, priv->bitrate);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}



static void owr_payload_class_init(OwrPayloadClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    g_type_class_add_private(klass, sizeof(OwrPayloadPrivate));

    gobject_class->set_property = owr_payload_set_property;
    gobject_class->get_property = owr_payload_get_property;

    obj_properties[PROP_MEDIA_TYPE] = g_param_spec_uint("media-type", "Media type",
        "The type of media",
        OWR_MEDIA_TYPE_AUDIO, OWR_MEDIA_TYPE_VIDEO, OWR_MEDIA_TYPE_AUDIO,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CODEC_TYPE] = g_param_spec_uint("codec-type", "Codec Type",
        "The type of codec",
        OWR_CODEC_TYPE_PCMU, OWR_CODEC_TYPE_VP8, OWR_CODEC_TYPE_PCMU,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_PAYLOAD_TYPE] = g_param_spec_uint("payload-type", "Payload type number",
        "The payload type number",
        0, 127, 0,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CLOCK_RATE] = g_param_spec_uint("clock-rate", "Clock-rate",
        "The clock-rate",
        0, 90000, 8000,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_BITRATE] = g_param_spec_uint("bitrate", "encoder bitrate",
        "The bitrate (bits per second) for the encoder. (0 - automatic)",
        0, G_MAXUINT, DEFAULT_BITRATE,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_MTU] = g_param_spec_uint("mtu", "MTU",
        "The maximum size of one RTP packet (in bytes)",
        0, G_MAXUINT, DEFAULT_MTU,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_payload_init(OwrPayload *payload)
{
    payload->priv = OWR_PAYLOAD_GET_PRIVATE(payload);
    payload->priv->mtu = DEFAULT_MTU;
    payload->priv->bitrate = DEFAULT_BITRATE;
}


OwrPayload * owr_payload_new(OwrMediaType media_type, OwrCodecType codec_type, guint payload_type, guint clock_rate)
{
    OwrPayload *payload = g_object_new(OWR_TYPE_PAYLOAD, "media-type", media_type, "codec-type",
        codec_type, "payload-type", payload_type, "clock-rate", clock_rate, NULL);
    return payload;
}



/* Private methods */


static gchar *OwrCodecTypeEncoderElementName[] = {"none", "mulawenc", "alawenc", "opusenc", "openh264enc", "vp8enc"};
static gchar *OwrCodecTypeDecoderElementName[] = {"none", "mulawdec", "alawdec", "opusdec", "openh264dec", "vp8dec"};
static gchar *OwrCodecTypePayElementName[] = {"none", "rtppcmupay", "rtppcmapay", "rtpopuspay", "rtph264pay", "rtpvp8pay"};
static gchar *OwrCodecTypeDepayElementName[] = {"none", "rtppcmudepay", "rtppcmadepay", "rtpopusdepay", "rtph264depay", "rtpvp8depay"};

static guint evaluate_bitrate_from_payload(OwrPayload *payload)
{
    guint bitrate;

    if (!payload->priv->bitrate) {
        guint width = 0, height = 0;
        gdouble framerate = 0.0;

        g_object_get(payload, "width", &width, "height", &height,
            "framerate", &framerate, NULL);
        width = width > 0 ? width : LIMITED_WIDTH;
        height = height > 0 ? height : LIMITED_HEIGHT;
        framerate = framerate > 0.0 ? framerate : LIMITED_FRAMERATE;

        bitrate = width * height * framerate * TARGET_BITS_PER_PIXEL;
    } else
        bitrate = payload->priv->bitrate;

    return bitrate;
}

GstElement * _owr_payload_create_encoder(OwrPayload *payload)
{
    GstElement *encoder = NULL;
    gchar *element_name = NULL;

    g_return_val_if_fail(payload, NULL);

    element_name = g_strdup_printf("encoder_%s_%u", OwrCodecTypeEncoderElementName[payload->priv->codec_type], get_unique_id());
    encoder = gst_element_factory_make(OwrCodecTypeEncoderElementName[payload->priv->codec_type], element_name);
    g_free(element_name);
    g_return_val_if_fail(encoder, NULL);

    switch (payload->priv->codec_type) {
    case OWR_CODEC_TYPE_OPUS:
        break;

    case OWR_CODEC_TYPE_H264:
        g_object_set(encoder, "gop-size", 0, NULL);
        gst_util_set_object_arg(G_OBJECT(encoder), "rate-control", "bitrate");
        g_object_bind_property(payload, "bitrate", encoder, "bitrate", G_BINDING_SYNC_CREATE);
        g_object_set(payload, "bitrate", evaluate_bitrate_from_payload(payload), NULL);
        break;

    case OWR_CODEC_TYPE_VP8:
        g_object_set(encoder, "end-usage", 1, "deadline", G_GINT64_CONSTANT(1), "lag-in-frames", 0,
            "error-resilient", 1, "keyframe-mode", 0, NULL);
        g_object_bind_property(payload, "bitrate", encoder, "target-bitrate", G_BINDING_SYNC_CREATE);
        g_object_set(payload, "bitrate", evaluate_bitrate_from_payload(payload), NULL);
        break;
    default:
        break;
    }

    return encoder;
}


GstElement * _owr_payload_create_decoder(OwrPayload *payload)
{
    GstElement * decoder = NULL;
    gchar *element_name = NULL;

    g_return_val_if_fail(payload, NULL);

    element_name = g_strdup_printf("decoder_%s_%u", OwrCodecTypeDecoderElementName[payload->priv->codec_type], get_unique_id());
    decoder = gst_element_factory_make(OwrCodecTypeDecoderElementName[payload->priv->codec_type], element_name);

    switch (payload->priv->codec_type) {
    case OWR_CODEC_TYPE_OPUS:
        break;
    case OWR_CODEC_TYPE_H264:
        g_object_set(decoder, "drop-on-errors", TRUE, NULL);
        break;
    case OWR_CODEC_TYPE_VP8:
        break;
    default:
        break;
    }

    g_free(element_name);
    return decoder;

}



GstElement * _owr_payload_create_payload_packetizer(OwrPayload *payload)
{
    GstElement * pay = NULL;
    gchar *element_name = NULL;

    g_return_val_if_fail(payload, NULL);

    element_name = g_strdup_printf("pay_%s_%u", OwrCodecTypePayElementName[payload->priv->codec_type], get_unique_id());
    pay = gst_element_factory_make(OwrCodecTypePayElementName[payload->priv->codec_type], element_name);
    g_free(element_name);

    g_object_bind_property(payload, "mtu", pay, "mtu", G_BINDING_SYNC_CREATE);

    switch (payload->priv->media_type) {
    case OWR_MEDIA_TYPE_AUDIO:
        if (OWR_IS_AUDIO_PAYLOAD(payload)) {
            g_object_bind_property(OWR_AUDIO_PAYLOAD(payload), "ptime", pay, "min-ptime", G_BINDING_SYNC_CREATE);
            g_object_bind_property(OWR_AUDIO_PAYLOAD(payload), "ptime", pay, "max-ptime", G_BINDING_SYNC_CREATE);
        }

        break;

    case OWR_MEDIA_TYPE_VIDEO:
        if (payload->priv->codec_type == OWR_CODEC_TYPE_H264)
            g_object_set(pay, "config-interval", 1, NULL);
        break;

    default:
        g_warn_if_reached();
    }

    return pay;
}


GstElement * _owr_payload_create_payload_depacketizer(OwrPayload *payload)
{
    GstElement * depay = NULL;
    gchar *element_name = NULL;

    g_return_val_if_fail(payload, NULL);

    element_name = g_strdup_printf("depay_%s_%u", OwrCodecTypeDepayElementName[payload->priv->codec_type], get_unique_id());
    depay = gst_element_factory_make(OwrCodecTypeDepayElementName[payload->priv->codec_type], element_name);

    g_free(element_name);
    return depay;
}


OwrMediaType _owr_payload_get_media_type(OwrPayload *payload)
{
    return payload->priv->media_type;
}


GstCaps * _owr_payload_create_rtp_caps(OwrPayload *payload)
{
    OwrPayloadPrivate *priv;
    GstCaps *caps = NULL;
    GEnumClass *enum_class;
    GEnumValue *enum_value;
    gchar *encoding_name;
    gboolean ccm_fir = FALSE, nack_pli = FALSE;

    g_return_val_if_fail(payload, NULL);
    priv = payload->priv;

    switch (priv->codec_type) {
    case OWR_CODEC_TYPE_PCMU:
        encoding_name = "PCMU";
        break;

    case OWR_CODEC_TYPE_PCMA:
        encoding_name = "PCMA";
        break;

    case OWR_CODEC_TYPE_OPUS:
        encoding_name = "X-GST-OPUS-DRAFT-SPITTKA-00";
        break;

    case OWR_CODEC_TYPE_H264:
        encoding_name = "H264";
        break;

    case OWR_CODEC_TYPE_VP8:
        encoding_name = "VP8-DRAFT-IETF-01";
        break;

    default:
        g_return_val_if_reached(NULL);
    }

    caps = gst_caps_new_simple("application/x-rtp",
        "encoding-name", G_TYPE_STRING, encoding_name,
        "payload", G_TYPE_INT, priv->payload_type,
        "clock-rate", G_TYPE_INT, priv->clock_rate,
        NULL);
    enum_class = G_ENUM_CLASS(g_type_class_ref(OWR_TYPE_MEDIA_TYPE));
    enum_value = g_enum_get_value(enum_class, priv->media_type);
    if (enum_value)
        gst_caps_set_simple(caps, "media", G_TYPE_STRING, enum_value->value_nick, NULL);
    g_type_class_unref(enum_class);

    if (OWR_IS_VIDEO_PAYLOAD(payload)) {
        g_object_get(OWR_VIDEO_PAYLOAD(payload), "ccm-fir", &ccm_fir, "nack-pli", &nack_pli, NULL);
        gst_caps_set_simple(caps, "rtcp-fb-ccm-fir", G_TYPE_BOOLEAN, ccm_fir, "rtcp-fb-nack-pli",
            G_TYPE_BOOLEAN, nack_pli, NULL);
    } else if (priv->media_type == OWR_MEDIA_TYPE_AUDIO) {
        guint channels = 0;
        if (OWR_IS_AUDIO_PAYLOAD(payload))
            g_object_get(OWR_AUDIO_PAYLOAD(payload), "channels", &channels, NULL);
        if (channels > 0) {
            gst_caps_set_simple(caps,
                "channels", G_TYPE_INT, channels,
                NULL);
        }
    }

    return caps;
}


GstCaps * _owr_payload_create_raw_caps(OwrPayload *payload)
{
    OwrPayloadPrivate *priv;
    GstCaps *caps = NULL;
    guint channels = 0;
    guint width = 0, height = 0;
    gdouble framerate = 0.0;
    gint fps_n = 0, fps_d = 1;

    g_return_val_if_fail(payload, NULL);
    priv = payload->priv;

    switch (priv->media_type) {
    case OWR_MEDIA_TYPE_AUDIO:
        if (OWR_IS_AUDIO_PAYLOAD(payload))
            g_object_get(OWR_AUDIO_PAYLOAD(payload), "channels", &channels, NULL);
        caps = gst_caps_new_simple("audio/x-raw",
            "format", G_TYPE_STRING, "S16LE",
            "layout", G_TYPE_STRING, "interleaved",
            "rate", G_TYPE_INT, priv->clock_rate,
            NULL);
        if (channels > 0) {
            gst_caps_set_simple(caps,
                "channels", G_TYPE_INT, channels,
                NULL);
        }
        break;

    case OWR_MEDIA_TYPE_VIDEO:
        if (OWR_IS_VIDEO_PAYLOAD(payload)) {
            g_object_get(OWR_VIDEO_PAYLOAD(payload),
                "width", &width,
                "height", &height,
                "framerate", &framerate,
                NULL);
        }
        caps = gst_caps_new_empty_simple("video/x-raw");
        gst_caps_set_simple(caps, "width", G_TYPE_INT, width > 0 ? width : LIMITED_WIDTH, NULL);
        gst_caps_set_simple(caps, "height", G_TYPE_INT, height > 0 ? height : LIMITED_HEIGHT, NULL);

        framerate = framerate > 0.0 ? framerate : LIMITED_FRAMERATE;
        gst_util_double_to_fraction(framerate, &fps_n, &fps_d);
        gst_caps_set_simple(caps, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
        break;
    default:
        g_return_val_if_reached(NULL);
    }

    return caps;
}



GstCaps * _owr_payload_create_encoded_caps(OwrPayload *payload)
{
    GstCaps *caps = NULL;

    g_return_val_if_fail(OWR_IS_PAYLOAD(payload), NULL);

    switch (payload->priv->codec_type) {
    case OWR_CODEC_TYPE_H264:
        caps = gst_caps_new_simple("video/x-h264",
            "stream-format", G_TYPE_STRING, "byte-stream",
            "alignment", G_TYPE_STRING, "nal",
            NULL);
        break;
    case OWR_CODEC_TYPE_VP8:
        caps = gst_caps_new_simple("video/x-vp8", NULL, NULL);
        break;
    default:
        caps = gst_caps_new_simple("ANY", NULL, NULL);
    }

    return caps;
}



/* local functions */

static guint get_unique_id()
{
    static guint id = 0;
    return id++;
}
