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
\*\ OwrMediaSource
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_media_source.h"

#include "owr_media_source_private.h"
#include "owr_private.h"
#include "owr_types.h"
#include "owr_utils.h"

#include <gst/gst.h>


#define DEFAULT_NAME NULL
#define DEFAULT_MEDIA_TYPE OWR_MEDIA_TYPE_UNKNOWN
#define DEFAULT_TYPE OWR_SOURCE_TYPE_UNKNOWN

enum {
    PROP_0,
    PROP_NAME,
    PROP_MEDIA_TYPE,
    PROP_TYPE,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

#define OWR_MEDIA_SOURCE_GET_PRIVATE(obj) \
        (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_MEDIA_SOURCE, OwrMediaSourcePrivate))

G_DEFINE_TYPE(OwrMediaSource, owr_media_source, G_TYPE_OBJECT)

struct _OwrMediaSourcePrivate {
    gchar *name;
    OwrMediaType media_type;
    guint index;

    OwrSourceType type;
    OwrCodecType codec_type;

    GstElement *source_bin;
};

static void owr_media_source_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_media_source_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static void owr_media_source_finalize(GObject *object)
{
    OwrMediaSource *source = OWR_MEDIA_SOURCE(object);
    OwrMediaSourcePrivate *priv = source->priv;

    g_free(priv->name);

    if (priv->source_bin) {
        GstElement *source_bin = priv->source_bin;
        priv->source_bin = NULL;
        gst_element_set_state(source_bin, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(_owr_get_pipeline()), source_bin);
    }

    g_mutex_clear(&source->lock);

    G_OBJECT_CLASS(owr_media_source_parent_class)->finalize(object);
}

static void owr_media_source_class_init(OwrMediaSourceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrMediaSourcePrivate));

    obj_properties[PROP_NAME] = g_param_spec_string("name", "name",
        "Human readable and meaningful source name", DEFAULT_NAME,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_MEDIA_TYPE] = g_param_spec_enum("media-type", "media-type",
        "The type of media provided by this source",
        OWR_TYPE_MEDIA_TYPE, DEFAULT_MEDIA_TYPE,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_TYPE] = g_param_spec_enum("type", "type",
        "The type of source in terms of how it generates media",
        OWR_TYPE_SOURCE_TYPE, DEFAULT_TYPE,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = owr_media_source_set_property;
    gobject_class->get_property = owr_media_source_get_property;

    gobject_class->finalize = owr_media_source_finalize;

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_media_source_init(OwrMediaSource *source)
{
    OwrMediaSourcePrivate *priv;
    source->priv = priv = OWR_MEDIA_SOURCE_GET_PRIVATE(source);

    priv->name = DEFAULT_NAME;
    priv->media_type = DEFAULT_MEDIA_TYPE;
    priv->type = DEFAULT_TYPE;

    priv->index = (guint)-1;
    priv->source_bin = NULL;

    g_mutex_init(&source->lock);
}

static void owr_media_source_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrMediaSourcePrivate *priv;

    g_return_if_fail(object);
    priv = OWR_MEDIA_SOURCE_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_NAME:
        g_free(priv->name);
        priv->name = g_value_dup_string(value);
        break;
    case PROP_MEDIA_TYPE:
        priv->media_type = g_value_get_enum(value);
        break;
    case PROP_TYPE:
        priv->type = g_value_get_enum(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_media_source_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrMediaSourcePrivate *priv;

    g_return_if_fail(object);
    priv = OWR_MEDIA_SOURCE_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_NAME:
        g_value_set_string(value, priv->name);
        break;
    case PROP_MEDIA_TYPE:
        g_value_set_enum(value, priv->media_type);
        break;
    case PROP_TYPE:
        g_value_set_enum(value, priv->type);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}


/* call with the media_source lock
 * element is transfer none */
GstElement *_owr_media_source_get_element(OwrMediaSource *media_source)
{
    g_return_val_if_fail(OWR_IS_MEDIA_SOURCE(media_source), NULL);

    return media_source->priv->source_bin;
}

/* call with the media_source lock
 * element is transfer full */
void _owr_media_source_set_element(OwrMediaSource *media_source, GstElement *element)
{
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(media_source));
    g_return_if_fail(!element || GST_IS_ELEMENT(element));

    media_source->priv->source_bin = element;
}

/* caps is transfer none */
GstPad *_owr_media_source_get_pad(OwrMediaSource *media_source, GstCaps *caps)
{
    GstPad *ghostpad = NULL;

    g_return_val_if_fail(OWR_IS_MEDIA_SOURCE(media_source), NULL);

    g_mutex_lock(&media_source->lock);
    ghostpad = OWR_MEDIA_SOURCE_GET_CLASS(media_source)->get_pad(media_source, caps);
    g_mutex_unlock(&media_source->lock);

    return ghostpad;
}

void _owr_media_source_unlink(OwrMediaSource *media_source, GstPad *downstream_pad)
{
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(media_source));
    g_return_if_fail(GST_IS_PAD(downstream_pad));

    g_mutex_lock(&media_source->lock);
    OWR_MEDIA_SOURCE_GET_CLASS(media_source)->unlink(media_source, downstream_pad);
    g_mutex_unlock(&media_source->lock);
}

void _owr_media_source_set_type(OwrMediaSource *media_source, OwrSourceType type)
{
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(media_source));
    /* an enum is an int type so we can use atomic assignment */
    g_atomic_int_set(&media_source->priv->type, type);
}

OwrCodecType _owr_media_source_get_codec(OwrMediaSource *media_source)
{
    g_return_val_if_fail(OWR_IS_MEDIA_SOURCE(media_source), OWR_CODEC_TYPE_NONE);
    return media_source->priv->codec_type;
}

void _owr_media_source_set_codec(OwrMediaSource *media_source, OwrCodecType codec_type)
{
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(media_source));
    /* an enum is an int type so we can use atomic assignment */
    g_atomic_int_set(&media_source->priv->codec_type, codec_type);
}
