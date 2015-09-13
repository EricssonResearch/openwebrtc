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
\*\ OwrGstMediaSource
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_gst_media_source.h"

#define OWR_GST_MEDIA_SOURCE_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_GST_TYPE_MEDIA_SOURCE, OwrGstMediaSourcePrivate))

G_DEFINE_TYPE(OwrGstMediaSource, owr_gst_media_source, OWR_TYPE_MEDIA_SOURCE)

enum {
    PROP_0,
    PROP_SOURCE,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static void owr_gst_media_source_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_gst_media_source_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static GstElement *owr_gst_media_source_request_source(OwrMediaSource *media_source, GstCaps *caps);

struct _OwrGstMediaSourcePrivate {
    GstElement *source;
};

static void owr_gst_media_source_dispose(GObject *object)
{
    OwrGstMediaSource *renderer = OWR_GST_MEDIA_SOURCE(object);
    OwrGstMediaSourcePrivate *priv = renderer->priv;

    if (priv->source) {
        gst_object_unref(priv->source);
        priv->source = NULL;
    }

    G_OBJECT_CLASS(owr_gst_media_source_parent_class)->dispose(object);
}

static void owr_gst_media_source_class_init(OwrGstMediaSourceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    OwrMediaSourceClass *media_source_class = OWR_MEDIA_SOURCE_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrGstMediaSourcePrivate));

    obj_properties[PROP_SOURCE] = g_param_spec_object("source", "source",
        "Audio or video source to use", G_TYPE_OBJECT,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = owr_gst_media_source_set_property;
    gobject_class->get_property = owr_gst_media_source_get_property;

    gobject_class->dispose = owr_gst_media_source_dispose;

    media_source_class->request_source = (void *(*)(OwrMediaSource *, void *))owr_gst_media_source_request_source;

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_gst_media_source_init(OwrGstMediaSource *source)
{
    OwrGstMediaSourcePrivate *priv;
    source->priv = priv = OWR_GST_MEDIA_SOURCE_GET_PRIVATE(source);

    priv->source = NULL;
}

static void owr_gst_media_source_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrGstMediaSourcePrivate *priv;
    GstElement *source;

    g_return_if_fail(object);
    priv = OWR_GST_MEDIA_SOURCE_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_SOURCE:
        source = g_value_get_object(value);
        if (!GST_IS_ELEMENT(source))
            break;
        if (priv->source)
            gst_object_unref(priv->source);
        priv->source = source;
        gst_object_ref_sink(source);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_gst_media_source_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrGstMediaSourcePrivate *priv;

    g_return_if_fail(object);
    priv = OWR_GST_MEDIA_SOURCE_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_SOURCE:
        g_value_set_object(value, priv->source);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

/**
 * owr_gst_media_source_new: (constructor)
 * @media_type:
 * @source_type:
 * @source:
 *
 * Returns: The new #OwrGstMediaSource
 */
OwrGstMediaSource *owr_gst_media_source_new(OwrMediaType media_type, OwrSourceType source_type, GstElement *source)
{
    return g_object_new(OWR_GST_TYPE_MEDIA_SOURCE,
        "media-type", media_type,
        "type", source_type,
        "source", source,
        NULL);
}

static GstElement *owr_gst_media_source_request_source(OwrMediaSource *media_source, GstCaps *caps)
{
    OwrGstMediaSourcePrivate *priv = OWR_GST_MEDIA_SOURCE(media_source)->priv;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
    GstStructure *structure;

    g_assert(priv->source);
    g_assert(caps);

    g_object_get(media_source, "media-type", &media_type, NULL);
    if (media_type == OWR_MEDIA_TYPE_UNKNOWN) {
        GST_ERROR_OBJECT(media_source,
                "Cannot connect source with unknown media type to other component");
        return NULL;
    }

    structure = gst_caps_get_structure(caps, 0);
    switch (media_type) {
    case OWR_MEDIA_TYPE_AUDIO:
        g_return_val_if_fail(gst_structure_has_name(structure, "audio/x-raw"), NULL);
        break;
    case OWR_MEDIA_TYPE_VIDEO:
        g_return_val_if_fail(gst_structure_has_name(structure, "video/x-raw"), NULL);
        break;
    case OWR_MEDIA_TYPE_UNKNOWN:
    default:
        g_assert_not_reached();
        return NULL;
    }
    g_return_val_if_fail(priv->source, NULL);

    return priv->source;
}
