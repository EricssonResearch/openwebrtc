/*
 * Copyright (c) 2014-2015, Ericsson AB. All rights reserved.
 * Copyright (c) 2014, Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
 *     Author: Arun Raghavan <arun@centricular.com>
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
#include "owr_utils.h"
#include "owr_video_payload.h"

#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

GST_DEBUG_CATEGORY_EXTERN(_owrpayload_debug);
#define GST_CAT_DEFAULT _owrpayload_debug

#define DEFAULT_MTU 1200
#define DEFAULT_BITRATE 0
#define DEFAULT_RTX_TIME 0    /* FIXME: what's a sane default here? */

#define LIMITED_WIDTH 640
#define LIMITED_HEIGHT 480
#define LIMITED_FRAMERATE 30.0

#define TARGET_BITS_PER_PIXEL 0.1 /* 640x360 15fps @ 768kbps =~ 0.2bpp */

#define OWR_PAYLOAD_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_PAYLOAD, OwrPayloadPrivate))

G_DEFINE_TYPE(OwrPayload, owr_payload, G_TYPE_OBJECT)

struct _OwrPayloadPrivate {
    OwrCodecType codec_type;
    guint payload_type;
    guint clock_rate;
    guint mtu;
    guint bitrate;
    gint rtx_payload_type;      /* -1 => no retransmission, else payload type for rtx pt map */
    guint rtx_time;             /* milliseconds */
    OwrAdaptationType adaptation;
};


enum {
    PROP_0,
    PROP_MEDIA_TYPE,
    PROP_CODEC_TYPE,
    PROP_PAYLOAD_TYPE,
    PROP_CLOCK_RATE,
    PROP_MTU,
    PROP_BITRATE,
    PROP_RTX_PAYLOAD_TYPE,
    PROP_RTX_TIME,
    PROP_ADAPTATION,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static guint get_unique_id();


static void owr_payload_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    OwrPayloadPrivate *priv;
    gint pt;

    g_return_if_fail(object);
    g_return_if_fail(value);
    g_return_if_fail(pspec);

    priv = OWR_PAYLOAD(object)->priv;

    switch (property_id) {
    case PROP_CODEC_TYPE:
        priv->codec_type = g_value_get_enum(value);
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
    case PROP_RTX_PAYLOAD_TYPE:
        pt = g_value_get_int(value);
        g_return_if_fail(pt == -1 || pt >= 96);
        priv->rtx_payload_type = pt;
        break;
    case PROP_RTX_TIME:
        priv->rtx_time = g_value_get_uint(value);
        break;
    case PROP_ADAPTATION:
        priv->adaptation = g_value_get_enum(value);
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
        g_assert_not_reached();
        break;
    case PROP_CODEC_TYPE:
        g_value_set_enum(value, priv->codec_type);
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
    case PROP_RTX_PAYLOAD_TYPE:
        g_value_set_int(value, priv->rtx_payload_type);
        break;
    case PROP_RTX_TIME:
        g_value_set_uint(value, priv->rtx_time);
        break;
    case PROP_ADAPTATION:
        g_value_set_enum(value, priv->adaptation);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

/* To be extended once more codecs are supported */
static GList *h264_decoders = NULL;
static GList *h264_encoders = NULL;
static GList *vp8_decoders = NULL;
static GList *vp8_encoders = NULL;

static gpointer owr_payload_detect_codecs(gpointer data)
{
    GList *decoder_factories;
    GList *encoder_factories;
    GstCaps *caps;

    OWR_UNUSED(data);

    decoder_factories = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_DECODER |
        GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO,
        GST_RANK_MARGINAL);
    encoder_factories = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_ENCODER |
        GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO,
        GST_RANK_MARGINAL);

    caps = gst_caps_new_empty_simple("video/x-h264");
    h264_decoders = gst_element_factory_list_filter(decoder_factories, caps, GST_PAD_SINK, FALSE);
    h264_encoders = gst_element_factory_list_filter(encoder_factories, caps, GST_PAD_SRC, FALSE);
    gst_caps_unref(caps);

    caps = gst_caps_new_empty_simple("video/x-vp8");
    vp8_decoders = gst_element_factory_list_filter(decoder_factories, caps, GST_PAD_SINK, FALSE);
    vp8_encoders = gst_element_factory_list_filter(encoder_factories, caps, GST_PAD_SRC, FALSE);
    gst_caps_unref(caps);

    gst_plugin_feature_list_free(decoder_factories);
    gst_plugin_feature_list_free(encoder_factories);

    h264_decoders = g_list_sort(h264_decoders, gst_plugin_feature_rank_compare_func);
    h264_encoders = g_list_sort(h264_encoders, gst_plugin_feature_rank_compare_func);
    vp8_decoders = g_list_sort(vp8_decoders, gst_plugin_feature_rank_compare_func);
    vp8_encoders = g_list_sort(vp8_encoders, gst_plugin_feature_rank_compare_func);

    return NULL;
}


static void owr_payload_class_init(OwrPayloadClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    g_type_class_add_private(klass, sizeof(OwrPayloadPrivate));

    gobject_class->set_property = owr_payload_set_property;
    gobject_class->get_property = owr_payload_get_property;

    obj_properties[PROP_MEDIA_TYPE] = g_param_spec_enum("media-type", "Media type",
        "The type of media",
        OWR_TYPE_MEDIA_TYPE, OWR_MEDIA_TYPE_AUDIO,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CODEC_TYPE] = g_param_spec_enum("codec-type", "Codec Type",
        "The type of codec",
        OWR_TYPE_CODEC_TYPE, OWR_CODEC_TYPE_PCMU,
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

    obj_properties[PROP_RTX_PAYLOAD_TYPE] = g_param_spec_int("rtx-payload-type", "Retransmission payload type",
        "The payload type to use for retransmission. (-1 means no retransmission)",
        -1, 127, OWR_RTX_PAYLOAD_TYPE_DISABLED,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_RTX_TIME] = g_param_spec_uint("rtx-time", "Retransmission buffer time",
        "How long a packet should be kept in buffers for retransmission (milliseconds, 0 means use the default)",
        0, G_MAXUINT, DEFAULT_RTX_TIME,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_MTU] = g_param_spec_uint("mtu", "MTU",
        "The maximum size of one RTP packet (in bytes)",
        0, G_MAXUINT, DEFAULT_MTU,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_ADAPTATION] = g_param_spec_enum("adaptation", "Adaptation",
        "Type of adaptation to use",
        OWR_TYPE_ADAPTATION_TYPE, OWR_ADAPTATION_TYPE_DISABLED,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_payload_init(OwrPayload *payload)
{
    static GOnce g_once = G_ONCE_INIT;

    g_once(&g_once, owr_payload_detect_codecs, NULL);

    payload->priv = OWR_PAYLOAD_GET_PRIVATE(payload);
    payload->priv->mtu = DEFAULT_MTU;
    payload->priv->bitrate = DEFAULT_BITRATE;

    payload->priv->rtx_payload_type = -1;
    payload->priv->rtx_time = DEFAULT_RTX_TIME;
}


/* Private methods */


static const gchar *OwrCodecTypeEncoderElementName[] = {"none", "mulawenc", "alawenc", "opusenc", "openh264enc", "vp8enc"};
static const gchar *OwrCodecTypeDecoderElementName[] = {"none", "mulawdec", "alawdec", "opusdec", "openh264dec", "vp8dec"};
static const gchar *OwrCodecTypeParserElementName[] = {"none", "none", "none", "none", "h264parse", "none"};
static const gchar *OwrCodecTypePayElementName[] = {"none", "rtppcmupay", "rtppcmapay", "rtpopuspay", "rtph264pay", "rtpvp8pay"};
static const gchar *OwrCodecTypeDepayElementName[] = {"none", "rtppcmudepay", "rtppcmadepay", "rtpopusdepay", "rtph264depay", "rtpvp8depay"};

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

static GstElement * try_codecs(GList *codecs, const gchar *name_prefix)
{
    GList *l;
    gchar *element_name;

    for (l = codecs; l; l = l->next) {
        GstElementFactory *f = l->data;
        GstElement *e;

        element_name = g_strdup_printf("%s_%s_%u", name_prefix,
            gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f)),
                get_unique_id());

        e = gst_element_factory_create(f, element_name);
        g_free(element_name);

        if (!e)
            continue;

        /* Try setting to READY. If this fails the codec does not work, for
         * example because the hardware codec is currently busy
         */
        if (gst_element_set_state(e, GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS) {
            gst_element_set_state(e, GST_STATE_NULL);
            gst_object_unref(e);
            continue;
        }

        return e;
    }

    return NULL;
}

static gboolean binding_transform_to_kbps(GBinding *binding, const GValue *from_value, GValue *to_value, gpointer user_data)
{
    guint bitrate;

    OWR_UNUSED(binding);
    OWR_UNUSED(user_data);

    bitrate = g_value_get_uint(from_value);
    g_value_set_uint(to_value, bitrate / 1000);

    return TRUE;
}

GstElement * _owr_payload_create_encoder(OwrPayload *payload)
{
    GstElement *encoder = NULL;
    gchar *element_name = NULL;
    GstElementFactory *factory;
    const gchar *factory_name;
    gint cpu_used;

    g_return_val_if_fail(payload, NULL);

    switch (payload->priv->codec_type) {
    case OWR_CODEC_TYPE_H264:
        encoder = try_codecs(h264_encoders, "encoder");
        g_return_val_if_fail(encoder, NULL);

        factory = gst_element_get_factory(encoder);
        factory_name = gst_plugin_feature_get_name(factory);

        if (!strcmp(factory_name, "openh264enc")) {
            g_object_set(encoder, "gop-size", 0, NULL);
            gst_util_set_object_arg(G_OBJECT(encoder), "rate-control", "bitrate");
            gst_util_set_object_arg(G_OBJECT(encoder), "complexity", "low");
            g_object_bind_property(payload, "bitrate", encoder, "bitrate", G_BINDING_SYNC_CREATE);
        } else if (!strcmp(factory_name, "x264enc")) {
            g_object_bind_property_full(payload, "bitrate", encoder, "bitrate", G_BINDING_SYNC_CREATE,
                binding_transform_to_kbps, NULL, NULL, NULL);
            gst_util_set_object_arg(G_OBJECT(encoder), "speed-preset", "ultrafast");
            gst_util_set_object_arg(G_OBJECT(encoder), "tune", "fastdecode+zerolatency");
        } else if (!strcmp(factory_name, "vtenc_h264")) {
            g_object_bind_property_full(payload, "bitrate", encoder, "bitrate", G_BINDING_SYNC_CREATE,
                binding_transform_to_kbps, NULL, NULL, NULL);
            g_object_set(encoder,
                "allow-frame-reordering", FALSE,
                "realtime", TRUE,
#if defined(__APPLE__) && TARGET_OS_IPHONE
                "quality", 0.0,
#else
                "quality", 0.5,
#endif
                "max-keyframe-interval", G_MAXINT,
                NULL);
        } else {
            /* Assume bits/s instead of kbit/s */
            g_object_bind_property(payload, "bitrate", encoder, "bitrate", G_BINDING_SYNC_CREATE);
        }
        g_object_set(payload, "bitrate", evaluate_bitrate_from_payload(payload), NULL);
        break;

    case OWR_CODEC_TYPE_VP8:
        encoder = try_codecs(vp8_encoders, "encoder");
        g_return_val_if_fail(encoder, NULL);

#if (defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR) || defined(__ANDROID__)
        cpu_used = -12; /* Mobile */
#else
        cpu_used = -6; /* Desktop */
#endif
        /* values are inspired by webrtc.org values in vp8_impl.cc */
        g_object_set(encoder,
            "end-usage", 1, /* VPX_CBR */
            "deadline", G_GINT64_CONSTANT(1), /* VPX_DL_REALTIME */
            "cpu-used", cpu_used,
            "min-quantizer", 2,
            "buffer-initial-size", 300,
            "buffer-optimal-size", 300,
            "buffer-size", 400,
            "dropframe-threshold", 30,
            "lag-in-frames", 0,
            "timebase", 1, 90000,
            "error-resilient", 1,
            "keyframe-mode", 0, /* VPX_KF_DISABLED */
            NULL);

        g_object_bind_property(payload, "bitrate", encoder, "target-bitrate", G_BINDING_SYNC_CREATE);
        g_object_set(payload, "bitrate", evaluate_bitrate_from_payload(payload), NULL);
        break;
    default:
        element_name = g_strdup_printf("encoder_%s_%u", OwrCodecTypeEncoderElementName[payload->priv->codec_type], get_unique_id());
        encoder = gst_element_factory_make(OwrCodecTypeEncoderElementName[payload->priv->codec_type], element_name);
        g_free(element_name);
        g_return_val_if_fail(encoder, NULL);
        break;
    }

    return encoder;
}

GstElement * _owr_payload_create_decoder(OwrPayload *payload)
{
    GstElement * decoder = NULL;
    gchar *element_name = NULL;

    g_return_val_if_fail(payload, NULL);

    switch (payload->priv->codec_type) {
    case OWR_CODEC_TYPE_H264:
        decoder = try_codecs(h264_decoders, "decoder");
        g_return_val_if_fail(decoder, NULL);
        break;
    case OWR_CODEC_TYPE_VP8:
        decoder = try_codecs(vp8_decoders, "decoder");
        g_return_val_if_fail(decoder, NULL);
        break;
    default:
        element_name = g_strdup_printf("decoder_%s_%u", OwrCodecTypeDecoderElementName[payload->priv->codec_type], get_unique_id());
        decoder = gst_element_factory_make(OwrCodecTypeDecoderElementName[payload->priv->codec_type], element_name);
        g_free(element_name);
        g_return_val_if_fail(decoder, NULL);
        break;
    }

    return decoder;
}

GstElement * _owr_payload_create_parser(OwrPayload *payload)
{
    GstElement * parser = NULL;
    gchar *element_name = NULL;

    g_return_val_if_fail(payload, NULL);

    if (!g_strcmp0(OwrCodecTypeParserElementName[payload->priv->codec_type], "none"))
        return NULL;

    element_name = g_strdup_printf("parser_%s_%u", OwrCodecTypeParserElementName[payload->priv->codec_type], get_unique_id());
    parser = gst_element_factory_make(OwrCodecTypeParserElementName[payload->priv->codec_type], element_name);
    g_free(element_name);

    switch (payload->priv->codec_type) {
    case OWR_CODEC_TYPE_H264:
        g_object_set(parser, "disable-passthrough", TRUE, NULL);
        break;
    default:
        break;
    }
    return parser;
}

GstElement * _owr_payload_create_payload_packetizer(OwrPayload *payload)
{
    GstElement * pay = NULL;
    gchar *element_name = NULL;
    OwrMediaType media_type;

    g_return_val_if_fail(payload, NULL);

    element_name = g_strdup_printf("pay_%s_%u", OwrCodecTypePayElementName[payload->priv->codec_type], get_unique_id());
    pay = gst_element_factory_make(OwrCodecTypePayElementName[payload->priv->codec_type], element_name);
    g_free(element_name);

    g_object_bind_property(payload, "mtu", pay, "mtu", G_BINDING_SYNC_CREATE);

    g_object_get(payload, "media-type", &media_type, NULL);
    switch (media_type) {
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
    OwrMediaType media_type;

    g_object_get(payload, "media-type", &media_type, NULL);
    return media_type;
}


GstCaps * _owr_payload_create_rtp_caps(OwrPayload *payload)
{
    OwrPayloadPrivate *priv;
    OwrMediaType media_type;
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

    g_object_get(payload, "media-type", &media_type, NULL);
    enum_class = G_ENUM_CLASS(g_type_class_ref(OWR_TYPE_MEDIA_TYPE));
    enum_value = g_enum_get_value(enum_class, media_type);
    if (enum_value)
        gst_caps_set_simple(caps, "media", G_TYPE_STRING, enum_value->value_nick, NULL);
    g_type_class_unref(enum_class);

    if (OWR_IS_VIDEO_PAYLOAD(payload)) {
        g_object_get(OWR_VIDEO_PAYLOAD(payload), "ccm-fir", &ccm_fir, "nack-pli", &nack_pli, NULL);
        gst_caps_set_simple(caps, "rtcp-fb-ccm-fir", G_TYPE_BOOLEAN, ccm_fir, "rtcp-fb-nack-pli",
            G_TYPE_BOOLEAN, nack_pli, NULL);
    } else if (media_type == OWR_MEDIA_TYPE_AUDIO) {
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
    OwrMediaType media_type;
    GstCaps *caps = NULL;
    guint channels = 0;
    guint width = 0, height = 0;
    gdouble framerate = 0.0;
    gint fps_n = 0, fps_d = 1;

    g_return_val_if_fail(payload, NULL);
    priv = payload->priv;

    g_object_get(payload, "media-type", &media_type, NULL);

    switch (media_type) {
    case OWR_MEDIA_TYPE_AUDIO:
        if (OWR_IS_AUDIO_PAYLOAD(payload))
            g_object_get(OWR_AUDIO_PAYLOAD(payload), "channels", &channels, NULL);
        caps = gst_caps_new_simple("audio/x-raw",
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
#ifdef __APPLE__
        if (priv->codec_type == OWR_CODEC_TYPE_H264)
          gst_caps_set_features(caps, 0, gst_caps_features_new_any());
#endif
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
            "profile", G_TYPE_STRING, "baseline",
            NULL);
        caps = gst_caps_merge_structure(caps, gst_structure_new("video/x-h264",
            "profile", G_TYPE_STRING, "constrained-baseline", NULL));
        break;
    case OWR_CODEC_TYPE_VP8:
        caps = gst_caps_new_empty_simple("video/x-vp8");
        break;
    default:
        caps = gst_caps_new_any();
    }

    return caps;
}



/* local functions */

static guint get_unique_id()
{
    static guint id = 0;
    return g_atomic_int_add(&id, 1);
}
