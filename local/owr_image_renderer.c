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
\*\ OwrImageRenderer
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_image_renderer.h"

#include "owr_image_renderer_private.h"
#include "owr_media_renderer_private.h"
#include "owr_private.h"

#include <gst/app/gstappsink.h>

#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

GST_DEBUG_CATEGORY_EXTERN(_owrimagerenderer_debug);
#define GST_CAT_DEFAULT _owrimagerenderer_debug

#define DEFAULT_WIDTH 0
#define DEFAULT_HEIGHT 0

#define DEFAULT_MAX_FRAMERATE 0.0

#define LIMITED_WIDTH 640
#define LIMITED_HEIGHT 480
#define LIMITED_FRAMERATE 15.0

#define OWR_IMAGE_RENDERER_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_IMAGE_RENDERER, OwrImageRendererPrivate))

G_DEFINE_TYPE(OwrImageRenderer, owr_image_renderer, OWR_TYPE_MEDIA_RENDERER)

static guint unique_bin_id = 0;

enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_MAX_FRAMERATE,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static void owr_image_renderer_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_image_renderer_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);
static void owr_image_renderer_constructed(GObject *object);

static GstElement *owr_image_renderer_get_element(OwrMediaRenderer *renderer);
static GstCaps *owr_image_renderer_get_caps(OwrMediaRenderer *renderer);

struct _OwrImageRendererPrivate {
    guint width;
    guint height;
    gdouble max_framerate;

    GstElement *appsink;
};

static void owr_image_renderer_class_init(OwrImageRendererClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    OwrMediaRendererClass *media_renderer_class = OWR_MEDIA_RENDERER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrImageRendererPrivate));

    obj_properties[PROP_WIDTH] = g_param_spec_uint("width", "width",
        "Video width in pixels", 0, G_MAXUINT, DEFAULT_WIDTH,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_HEIGHT] = g_param_spec_uint("height", "height",
        "Video height in pixels", 0, G_MAXUINT, DEFAULT_HEIGHT,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_MAX_FRAMERATE] = g_param_spec_double("max-framerate", "max-framerate",
        "Maximum video frames per second", 0.0, G_MAXDOUBLE,
        DEFAULT_MAX_FRAMERATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = owr_image_renderer_set_property;
    gobject_class->get_property = owr_image_renderer_get_property;
    gobject_class->constructed = owr_image_renderer_constructed;

    media_renderer_class->get_caps = (void *(*)(OwrMediaRenderer *))owr_image_renderer_get_caps;

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_image_renderer_init(OwrImageRenderer *renderer)
{
    OwrImageRendererPrivate *priv;
    renderer->priv = priv = OWR_IMAGE_RENDERER_GET_PRIVATE(renderer);

    priv->width = DEFAULT_WIDTH;
    priv->height = DEFAULT_HEIGHT;
    priv->max_framerate = DEFAULT_MAX_FRAMERATE;
}

static void owr_image_renderer_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrImageRendererPrivate *priv;

    g_return_if_fail(object);
    priv = OWR_IMAGE_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
    /* FIXME - make changing properties cause reconfiguration */
    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint(value);
        break;
    case PROP_MAX_FRAMERATE:
        priv->max_framerate = g_value_get_double(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_image_renderer_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrImageRendererPrivate *priv;

    g_return_if_fail(object);
    priv = OWR_IMAGE_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_WIDTH:
        g_value_set_uint(value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, priv->height);
        break;
    case PROP_MAX_FRAMERATE:
        g_value_set_double(value, priv->max_framerate);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}


/**
 * owr_image_renderer_new: (constructor)
 *
 * Returns: The new #OwrImageRenderer
 */
OwrImageRenderer *owr_image_renderer_new(void)
{
    return g_object_new(OWR_TYPE_IMAGE_RENDERER,
        "media-type", OWR_MEDIA_TYPE_VIDEO,
        NULL);
}


#define LINK_ELEMENTS(a, b) \
    if (!gst_element_link(a, b)) \
        GST_ERROR("Failed to link " #a " -> " #b); \

static GstElement *owr_image_renderer_get_element(OwrMediaRenderer *renderer)
{
    OwrImageRenderer *image_renderer;
    OwrImageRendererPrivate *priv;
    GstElement *renderer_bin;
    GstElement *sink;
    GstPad *ghostpad, *sinkpad;
    gchar *bin_name;

    g_assert(renderer);
    image_renderer = OWR_IMAGE_RENDERER(renderer);
    priv = image_renderer->priv;
    g_assert(!priv->appsink);

    bin_name = g_strdup_printf("image-renderer-bin-%u", g_atomic_int_add(&unique_bin_id, 1));
    renderer_bin = gst_bin_new(bin_name);
    g_free(bin_name);

    sink = gst_element_factory_make("appsink", "image-renderer-appsink");
    g_assert(sink);
    priv->appsink = sink;

    g_object_set(sink, "max-buffers", 1, "drop", TRUE, "qos", TRUE, "enable-last-sample", FALSE, NULL);

    gst_bin_add_many(GST_BIN(renderer_bin), sink, NULL);

    sinkpad = gst_element_get_static_pad(sink, "sink");
    g_assert(sinkpad);
    ghostpad = gst_ghost_pad_new("sink", sinkpad);
    gst_pad_set_active(ghostpad, TRUE);
    gst_element_add_pad(renderer_bin, ghostpad);
    gst_object_unref(sinkpad);

    return renderer_bin;
}

static void owr_image_renderer_constructed(GObject *object)
{
    OwrMediaRenderer *renderer = OWR_MEDIA_RENDERER(object);

    _owr_media_renderer_set_sink(renderer, owr_image_renderer_get_element(renderer));

    G_OBJECT_CLASS(owr_image_renderer_parent_class)->constructed(object);
}

static GstCaps *owr_image_renderer_get_caps(OwrMediaRenderer *renderer)
{
    GstCaps *caps = NULL;
    guint width = 0, height = 0;
    gdouble max_framerate = 0.0;
    gint fps_n = 0, fps_d = 1;

    g_object_get(OWR_IMAGE_RENDERER(renderer),
        "width", &width,
        "height", &height,
        "max-framerate", &max_framerate,
        NULL);

    caps = gst_caps_new_empty_simple("video/x-raw");
    /* FIXME - add raw format property to image renderer */
    gst_caps_set_simple(caps, "format", G_TYPE_STRING, "BGRA", NULL);
    gst_caps_set_simple(caps, "width", G_TYPE_INT, width > 0 ? width : LIMITED_WIDTH, NULL);
    gst_caps_set_simple(caps, "height", G_TYPE_INT, height > 0 ? height : LIMITED_HEIGHT, NULL);

    max_framerate = max_framerate > 0.0 ? max_framerate : LIMITED_FRAMERATE;
    gst_util_double_to_fraction(max_framerate, &fps_n, &fps_d);
    GST_DEBUG_OBJECT(renderer, "Setting the framerate to %d/%d", fps_n, fps_d);
    gst_caps_set_simple(caps, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);

    return caps;
}

#define BMP_RESERVED 0
#define BMP_HEADER_SIZE 54
#define DIB_HEADER_SIZE 40
#define DIB_COLOR_PLANES 1
#define DIB_BITS_PER_PIXEL 32
#define DIB_COMPRESSION 0
#define DIB_HORIZONTAL_RESOLUTION 0
#define DIB_VERTICAL_RESOLUTION 0
#define DIB_COLORS_IN_PALETTE 0
#define DIB_IMPORTANT_COLORS 0

static void fill_bmp_header(guint8 *image_data, guint image_width, guint image_height)
{
    guint dib_image_row_size = ((DIB_BITS_PER_PIXEL * image_width + 31) / 32) * 4;
    guint dib_image_size = dib_image_row_size * image_height;
    guint8 *data = image_data;

    *data = 'B';
    data++;
    *data = 'M';
    data++;
    GST_WRITE_UINT32_LE(data, (BMP_HEADER_SIZE + dib_image_size));
    data += 4;
    GST_WRITE_UINT16_LE(data, BMP_RESERVED);
    data += 2;
    GST_WRITE_UINT16_LE(data, BMP_RESERVED);
    data += 2;
    GST_WRITE_UINT32_LE(data, BMP_HEADER_SIZE);
    data += 4;
    GST_WRITE_UINT32_LE(data, DIB_HEADER_SIZE);
    data += 4;
    GST_WRITE_UINT32_LE(data, image_width);
    data += 4;
    GST_WRITE_UINT32_LE(data, image_height);
    data += 4;
    GST_WRITE_UINT16_LE(data, DIB_COLOR_PLANES);
    data += 2;
    GST_WRITE_UINT16_LE(data, DIB_BITS_PER_PIXEL);
    data += 2;
    GST_WRITE_UINT32_LE(data, DIB_COMPRESSION);
    data += 4;
    GST_WRITE_UINT32_LE(data, dib_image_size);
    data += 4;
    GST_WRITE_UINT32_LE(data, DIB_HORIZONTAL_RESOLUTION);
    data += 4;
    GST_WRITE_UINT32_LE(data, DIB_VERTICAL_RESOLUTION);
    data += 4;
    GST_WRITE_UINT32_LE(data, DIB_COLORS_IN_PALETTE);
    data += 4;
    GST_WRITE_UINT32_LE(data, DIB_IMPORTANT_COLORS);
    data += 4;
}

/**
 * _owr_image_renderer_pull_bmp_image:
 * @image_renderer:
 *
 * Returns: (transfer full):
 */
GBytes * _owr_image_renderer_pull_bmp_image(OwrImageRenderer *image_renderer)
{
    GstCaps *caps;
    GstSample *sample;
    GstBuffer *buf = NULL;
    GstMapInfo info;
    GstStructure *s;
    guint bufsize, total_size, src_rowsize, dest_rowsize, image_width, image_height;
    guint8 *image_data, *src_data, *srcpos, *destpos;
    gboolean ret, disabled = FALSE;

    g_return_val_if_fail(OWR_IS_IMAGE_RENDERER(image_renderer), NULL);

    if (!image_renderer->priv->appsink)
        return NULL;

    sample = gst_app_sink_pull_sample(GST_APP_SINK(image_renderer->priv->appsink));
    if (!sample)
        return NULL;

    buf = gst_sample_get_buffer(sample);
    if (!buf) {
        gst_sample_unref(sample);
        return NULL;
    }

    caps = gst_sample_get_caps(sample);
    s = gst_caps_get_structure(caps, 0);
    ret = gst_structure_get_int(s, "width", (gint *)&image_width);
    ret |= gst_structure_get_int(s, "height", (gint *)&image_height);
    if (!ret) {
        g_critical("%s Could not get bmp video dimensions from configured caps on appsink",
            __FUNCTION__);
        image_width = 0;
        image_height = 0;
    }

    if (!gst_buffer_map(buf, &info, GST_MAP_READ))
        g_assert_not_reached();

    g_assert(info.data);

    g_object_get(image_renderer, "disabled", &disabled, NULL);
    bufsize = (guint) info.size;
    total_size = BMP_HEADER_SIZE + bufsize;
    image_data = disabled ? g_malloc0(total_size) : g_malloc(total_size);
    if (!image_data) {
        g_critical("%s Allocate mem failed (g_malloc(total_size))", __FUNCTION__);
        return NULL;
    }
    fill_bmp_header(image_data, image_width, image_height);

    src_rowsize = DIB_BITS_PER_PIXEL * image_width / 8;
    dest_rowsize = ((DIB_BITS_PER_PIXEL * image_width  + 31) / 32) * 4;
    src_data = info.data;
    destpos = image_data + total_size;

    if (!disabled) {
        for (srcpos = src_data; srcpos < src_data + bufsize; srcpos += src_rowsize) {
            destpos -= dest_rowsize;
            memcpy(destpos, srcpos, src_rowsize);
        }
    }

    gst_buffer_unmap(buf, &info);
    gst_sample_unref(sample);

    return g_bytes_new_take(image_data, total_size);
}
