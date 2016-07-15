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
\*\ OwrUtils
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_utils.h"

#include "owr_types.h"

/* To be extended once more codecs are supported */
static GList *h264_decoders = NULL;
static GList *h264_encoders = NULL;
static GList *vp8_decoders = NULL;
static GList *vp8_encoders = NULL;
static GList *vp9_decoders = NULL;
static GList *vp9_encoders = NULL;

static const gchar *OwrCodecTypeEncoderElementName[] = { NULL, "mulawenc", "alawenc", "opusenc", "openh264enc", "vp8enc", "vp9enc" };
static const gchar *OwrCodecTypeDecoderElementName[] = { NULL, "mulawdec", "alawdec", "opusdec", "openh264dec", "vp8dec", "vp9dec" };

static const gchar *OwrCodecTypeParserElementName[] = { NULL, NULL, NULL, NULL, "h264parse", NULL, NULL };

guint _owr_get_unique_uint_id()
{
    static guint id = 0;
    return g_atomic_int_add(&id, 1);
}

OwrCodecType _owr_caps_to_codec_type(GstCaps *caps)
{
    GstStructure *structure;

    structure = gst_caps_get_structure(caps, 0);
    if (gst_structure_has_name(structure, "video/x-raw")
        || gst_structure_has_name(structure, "audio/x-raw"))
        return OWR_CODEC_TYPE_NONE;
    if (gst_structure_has_name(structure, "audio/x-mulaw"))
        return OWR_CODEC_TYPE_PCMU;
    if (gst_structure_has_name(structure, "audio/x-alaw"))
        return OWR_CODEC_TYPE_PCMA;
    if (gst_structure_has_name(structure, "audio/x-opus"))
        return OWR_CODEC_TYPE_OPUS;
    if (gst_structure_has_name(structure, "video/x-h264"))
        return OWR_CODEC_TYPE_H264;
    if (gst_structure_has_name(structure, "video/x-vp8"))
        return OWR_CODEC_TYPE_VP8;
    if (gst_structure_has_name(structure, "video/x-vp9"))
        return OWR_CODEC_TYPE_VP9;

    GST_ERROR("Unknown caps: %" GST_PTR_FORMAT, (gpointer)caps);
    return OWR_CODEC_TYPE_NONE;
}

const gchar* _owr_codec_type_to_caps_mime(OwrMediaType media_type, OwrCodecType codec_type)
{
    switch (codec_type)
    {
    case OWR_CODEC_TYPE_NONE:
        switch (media_type)
        {
        case OWR_MEDIA_TYPE_AUDIO:
            return "audio/x-raw";
            break;
        case OWR_MEDIA_TYPE_VIDEO:
            return "video/x-raw";
            break;
        default:
            g_return_val_if_reached("audio/x-raw");
        }
        break;
    case OWR_CODEC_TYPE_PCMU:
        return "audio/x-mulaw";
        break;
    case OWR_CODEC_TYPE_PCMA:
        return "audio/x-alaw";
        break;
    case OWR_CODEC_TYPE_OPUS:
        return "audio/x-opus";
        break;
    case OWR_CODEC_TYPE_H264:
        return "video/x-h264";
        break;
    case OWR_CODEC_TYPE_VP8:
        return "video/x-vp8";
        break;
    case OWR_CODEC_TYPE_VP9:
      return "video/x-vp9";
      break;
    default:
        break;
    }
    g_return_val_if_reached("audio/x-raw");
}

gpointer _owr_detect_codecs(gpointer data)
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

    caps = gst_caps_new_empty_simple("video/x-vp9");
    vp9_decoders = gst_element_factory_list_filter(decoder_factories, caps, GST_PAD_SINK, FALSE);
    vp9_encoders = gst_element_factory_list_filter(encoder_factories, caps, GST_PAD_SRC, FALSE);
    gst_caps_unref(caps);

    gst_plugin_feature_list_free(decoder_factories);
    gst_plugin_feature_list_free(encoder_factories);

    h264_decoders = g_list_sort(h264_decoders, gst_plugin_feature_rank_compare_func);
    h264_encoders = g_list_sort(h264_encoders, gst_plugin_feature_rank_compare_func);
    vp8_decoders = g_list_sort(vp8_decoders, gst_plugin_feature_rank_compare_func);
    vp8_encoders = g_list_sort(vp8_encoders, gst_plugin_feature_rank_compare_func);
    vp9_decoders = g_list_sort(vp9_decoders, gst_plugin_feature_rank_compare_func);
    vp9_encoders = g_list_sort(vp9_encoders, gst_plugin_feature_rank_compare_func);

    return NULL;
}

const GList *_owr_get_detected_h264_encoders()
{
    return h264_encoders;
}

const GList *_owr_get_detected_vp8_encoders()
{
    return vp8_encoders;
}

const GList *_owr_get_detected_vp9_encoders()
{
    return vp9_encoders;
}

GstElement *_owr_try_codecs(const GList *codecs, const gchar *name_prefix)
{
    GList *l;
    gchar *element_name;

    for (l = (GList*) codecs; l; l = l->next) {
        GstElementFactory *f = l->data;
        GstElement *e;

        element_name = g_strdup_printf("%s_%s_%u", name_prefix,
            gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f)),
                _owr_get_unique_uint_id());

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

GstElement * _owr_create_decoder(OwrCodecType codec_type)
{
    GstElement * decoder = NULL;
    gchar *element_name = NULL;

    switch (codec_type) {
    case OWR_CODEC_TYPE_H264:
        decoder = _owr_try_codecs(h264_decoders, "decoder");
        g_return_val_if_fail(decoder, NULL);
        break;
    case OWR_CODEC_TYPE_VP8:
        decoder = _owr_try_codecs(vp8_decoders, "decoder");
        g_return_val_if_fail(decoder, NULL);
        break;
    case OWR_CODEC_TYPE_VP9:
        decoder = _owr_try_codecs(vp9_decoders, "decoder");
        g_return_val_if_fail(decoder, NULL);
        break;
    default:
        element_name = g_strdup_printf("decoder_%s_%u", OwrCodecTypeDecoderElementName[codec_type], _owr_get_unique_uint_id());
        decoder = gst_element_factory_make(OwrCodecTypeDecoderElementName[codec_type], element_name);
        g_free(element_name);
        g_return_val_if_fail(decoder, NULL);
        break;
    }

    return decoder;
}

GstElement * _owr_create_parser(OwrCodecType codec_type)
{
    GstElement * parser = NULL;
    gchar *element_name = NULL;

    if (!OwrCodecTypeParserElementName[codec_type])
        return NULL;

    element_name = g_strdup_printf("parser_%s_%u", OwrCodecTypeParserElementName[codec_type], _owr_get_unique_uint_id());
    parser = gst_element_factory_make(OwrCodecTypeParserElementName[codec_type], element_name);
    g_free(element_name);

    switch (codec_type) {
    case OWR_CODEC_TYPE_H264:
        g_object_set(parser, "disable-passthrough", TRUE, NULL);
        break;
    default:
        break;
    }
    return parser;
}

const gchar* _owr_get_encoder_name(OwrCodecType codec_type)
{
    return OwrCodecTypeEncoderElementName[codec_type];
}

void _owr_bin_link_and_sync_elements(GstBin *bin, gboolean *out_link_ok, gboolean *out_sync_ok, GstElement **out_first, GstElement **out_last)
{
    GList *bin_elements, *current;
    gboolean link_ok = TRUE, sync_ok = TRUE;

    g_assert(bin);

    bin_elements = g_list_last(bin->children);
    g_assert(bin_elements);
    for (current = bin_elements; current && current->prev && link_ok && sync_ok; current = g_list_previous(current)) {
        if (out_link_ok)
            link_ok &= gst_element_link(current->data, current->prev->data);
        if (out_sync_ok)
            sync_ok &= gst_element_sync_state_with_parent(current->data);
    }
    if (out_sync_ok && link_ok && sync_ok && current && !current->prev)
        sync_ok &= gst_element_sync_state_with_parent(current->data);

    if (out_link_ok)
        *out_link_ok = link_ok;

    if (out_sync_ok)
        *out_sync_ok = sync_ok;

    if (link_ok && sync_ok) {
        if (out_first)
            *out_first = bin_elements->data;
        if (out_last)
            *out_last = current->data;
    }
}

typedef struct {
    GClosure *callback;
    GList *list;
    GCopyFunc item_copy;
    GDestroyNotify item_destroy;
    GMutex mutex;
} CallbackMergeContext;

/*
 * Call the closure with the list as single argument.
 * @callback: (scope sync) (transfer full): the callback to be called with the list
 * @list: (transfer none): any list
 */
void _owr_utils_call_closure_with_list(GClosure *callback, GList *list)
{
    GValue values[1] = { G_VALUE_INIT };

    g_return_if_fail(callback);

    g_value_init(&values[0], G_TYPE_POINTER);
    g_value_set_pointer(&values[0], list);

    g_closure_invoke(callback, NULL, 1, values, NULL);
    g_closure_unref(callback);

    g_value_unset(&values[0]);
}

static void callback_merger_on_destroy_data(CallbackMergeContext *context, GClosure *closure)
{
    OWR_UNUSED(closure);

    _owr_utils_call_closure_with_list(context->callback, context->list);

    g_mutex_clear(&context->mutex);

    if (context->item_destroy)
        g_list_free_full(context->list, context->item_destroy);
    else
        g_list_free(context->list);
    g_free(context);
}

static void callback_merger(GList *list, CallbackMergeContext *context)
{
    g_mutex_lock(&context->mutex);
    context->list = g_list_concat(context->list,
        g_list_copy_deep (list, context->item_copy, context));
    g_mutex_unlock(&context->mutex);
}

/*
 * @final_callback: (transfer full):
 * @list_item_copy: (allow none): used to copy the list items
 * @list_item_destroy: (allow none): used to free the list items after calling @final_callback
 *
 * Returns a closure which should be called with a single GList argument.
 * When the refcount of the closure reaches 0, final_callback is called
 * with a concatenation of all the lists that were sent to the closure.
 */
GClosure *_owr_utils_list_closure_merger_new(GClosure *final_callback,
    GCopyFunc list_item_copy,
    GDestroyNotify list_item_destroy)
{
    CallbackMergeContext *context;
    GClosure *merger;

    context = g_new0(CallbackMergeContext, 1);

    context->callback = final_callback;
    context->item_copy = list_item_copy;
    context->item_destroy = list_item_destroy;
    context->list = NULL;
    g_mutex_init(&context->mutex);

    merger = g_cclosure_new(G_CALLBACK(callback_merger), context, (GClosureNotify) callback_merger_on_destroy_data);
    g_closure_set_marshal(merger, g_cclosure_marshal_generic);

    return merger;
}

gboolean _owr_gst_caps_foreach(const GstCaps *caps, OwrGstCapsForeachFunc func, gpointer user_data)
{
    guint i, n;
    GstCapsFeatures *features;
    GstStructure *structure;
    gboolean ret;

    g_return_val_if_fail(GST_IS_CAPS(caps), FALSE);
    g_return_val_if_fail(func != NULL, FALSE);

    n = gst_caps_get_size(caps);

    for (i = 0; i < n; i++) {
        features = gst_caps_get_features(caps, i);
        structure = gst_caps_get_structure(caps, i);
        if (features && structure) {
            ret = func(features, structure, user_data);
            if (G_UNLIKELY(!ret))
                return FALSE;
	}
    }

    return TRUE;
}

void _owr_deep_notify(GObject *object, GstObject *orig,
    GParamSpec *pspec, gpointer user_data)
{
    GValue value = G_VALUE_INIT;
    gchar *str = NULL;
    GstObject *it;
    gchar *prevpath, *path;

    OWR_UNUSED(user_data);
    OWR_UNUSED(object);

    path = g_strdup("");

    for (it = orig; GST_IS_OBJECT(it); it = GST_OBJECT_PARENT(it)) {
        prevpath = path;
        path = g_strjoin("/", GST_OBJECT_NAME(it), prevpath, NULL);
        g_free(prevpath);
    }

    if (pspec->flags & G_PARAM_READABLE) {
        g_value_init(&value, pspec->value_type);
        g_object_get_property(G_OBJECT(orig), pspec->name, &value);

        if (G_VALUE_TYPE(&value) == GST_TYPE_CAPS)
            str = gst_caps_to_string(gst_value_get_caps(&value));
        else if (G_VALUE_HOLDS_STRING(&value))
            str = g_value_dup_string(&value);
        else
            str = gst_value_serialize(&value);

        GST_INFO_OBJECT(object, "%s%s = %s\n", path, pspec->name, str);
        g_free(str);
        g_value_unset(&value);
    } else
        GST_INFO_OBJECT(object, "Parameter %s not readable in %s.", pspec->name, path);

    g_free(path);
}

int _owr_rotation_and_mirror_to_video_flip_method(guint rotation, gboolean mirror)
{
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    static gint method_table[] = {2, 3, 0, 1, 4, 7, 5, 6};
#else
    static gint method_table[] = {0, 1, 2, 3, 5, 6, 4, 7};
#endif
    g_return_val_if_fail(rotation < 4, 0);

    if (mirror) {
        return method_table[rotation + 4];
    } else {
        return method_table[rotation];
    }
}

void _owr_update_flip_method(GObject *source, GParamSpec *pspec, GstElement *flip)
{
    guint rotation = 0;
    gboolean mirror = FALSE;
    gint flip_method;

    g_assert(GST_IS_ELEMENT(flip));

    g_object_get(source, "rotation", &rotation, "mirror", &mirror, NULL);
    flip_method = _owr_rotation_and_mirror_to_video_flip_method(rotation, mirror);
    g_object_set(flip, pspec ? pspec->name : "method", flip_method, NULL);
}

static void value_slice_free(gpointer value)
{
    g_value_unset(value);
    g_slice_free(GValue, value);
}

/**
 * _owr_value_table_new:
 * Returns: a #GHashTable
 *
 * Creates a table new #GHashTable configured for storing #GValue values
 * Values should be added using _owr_value_table_add()
 */
GHashTable *_owr_value_table_new()
{
    return g_hash_table_new_full(g_str_hash, g_str_equal, g_free, value_slice_free);
}

/**
 * _owr_value_table_add:
 * @table: a #GHashTable
 * @key: the key
 * @type: a GType that the value is initialized to
 *
 * Inserts a new key and value into the hash table using g_hash_table_insert()
 * The value is initialized with @type and returned, so that it's value can be set
 */
GValue *_owr_value_table_add(GHashTable *table, const gchar *key, GType type)
{
    GValue *value;
    value = g_slice_new0(GValue);
    g_value_init(value, type);
    g_hash_table_insert(table, g_strdup(key), value);
    return value;
}
