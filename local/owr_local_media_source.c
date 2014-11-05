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
\*\ OwrLocalMediaSource
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_local_media_source.h"
#include "owr_local_media_source_private.h"

#include "owr_media_source.h"
#include "owr_media_source_private.h"
#include "owr_private.h"
#include "owr_types.h"
#include "owr_utils.h"

#include <gst/gst.h>

#include <stdio.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && !TARGET_IPHONE_SIMULATOR
#define AUDIO_SRC "osxaudiosrc"
#define VIDEO_SRC "avfvideosrc"

#elif defined(__ANDROID__)
#define AUDIO_SRC "openslessrc"
#define VIDEO_SRC "androidvideosource"

#elif defined(__linux__)
#define AUDIO_SRC  "pulsesrc"
#define VIDEO_SRC  "v4l2src"

#else
#define AUDIO_SRC  "audiotestsrc"
#define VIDEO_SRC  "videotestsrc"
#endif

static guint unique_bin_id = 0;

#define OWR_LOCAL_MEDIA_SOURCE_GET_PRIVATE(obj) \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_LOCAL_MEDIA_SOURCE, OwrLocalMediaSourcePrivate))

G_DEFINE_TYPE(OwrLocalMediaSource, owr_local_media_source, OWR_TYPE_MEDIA_SOURCE)

static GstElement *owr_local_media_source_request_source(OwrMediaSource *media_source, GstCaps *caps);

struct _OwrLocalMediaSourcePrivate {
    guint device_index;
};

static void owr_local_media_source_class_init(OwrLocalMediaSourceClass *klass)
{
    OwrMediaSourceClass *media_source_class = OWR_MEDIA_SOURCE_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrLocalMediaSourcePrivate));

    media_source_class->request_source = (void *(*)(OwrMediaSource *, void *))owr_local_media_source_request_source;
}

static void owr_local_media_source_init(OwrLocalMediaSource *source)
{
    OwrLocalMediaSourcePrivate *priv;
    source->priv = priv = OWR_LOCAL_MEDIA_SOURCE_GET_PRIVATE(source);
    priv->device_index = 0;
}

#define LINK_ELEMENTS(a, b) \
    if (!gst_element_link(a, b)) \
        GST_ERROR("Failed to link " #a " -> " #b);

#define CREATE_ELEMENT(elem, factory, name) \
    elem = gst_element_factory_make(factory, name); \
    if (!elem) \
        GST_ERROR("Could not create " name " from factory " factory); \
    g_assert(elem);

/* FIXME: Copy from owr/orw.c without any error handling whatsoever */
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer user_data)
{
    gboolean ret, is_warning = FALSE;
    GstStateChangeReturn change_status;
    gchar *message_type, *debug;
    GError *error;
    GstPipeline *pipeline = user_data;

    g_return_val_if_fail(GST_IS_BUS(bus), TRUE);

    (void)user_data;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_LATENCY:
        ret = gst_bin_recalculate_latency(GST_BIN(pipeline));
        g_warn_if_fail(ret);
        break;

    case GST_MESSAGE_CLOCK_LOST:
        change_status = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
        g_warn_if_fail(change_status != GST_STATE_CHANGE_FAILURE);
        change_status = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
        g_warn_if_fail(change_status != GST_STATE_CHANGE_FAILURE);
        break;

    case GST_MESSAGE_EOS:
        g_print("End of stream\n");
        break;

    case GST_MESSAGE_WARNING:
        is_warning = TRUE;

    case GST_MESSAGE_ERROR:
        if (is_warning) {
            message_type = "Warning";
            gst_message_parse_warning(msg, &error, &debug);
        } else {
            message_type = "Error";
            gst_message_parse_error(msg, &error, &debug);
        }

        g_printerr("==== %s message start ====\n", message_type);
        g_printerr("%s in element %s.\n", message_type, GST_OBJECT_NAME(msg->src));
        g_printerr("%s: %s\n", message_type, error->message);
        g_printerr("Debugging info: %s\n", (debug) ? debug : "none");

        g_printerr("==== %s message stop ====\n", message_type);
        /*GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline.dot");*/

        g_error_free(error);
        g_free(debug);
        break;

    default:
        break;
    }

    return TRUE;
}

static gboolean shutdown_media_source(GHashTable *args)
{
    OwrMediaSource *media_source;
    GstElement *source_pipeline, *source_tee;

    media_source = g_hash_table_lookup(args, "media_source");
    g_assert(media_source);

    source_pipeline = _owr_media_source_get_source_bin(media_source);
    if (!source_pipeline) {
        g_object_unref(media_source);
        g_hash_table_unref(args);
        return FALSE;
    }

    source_tee = _owr_media_source_get_source_tee(media_source);
    if (!source_tee) {
        gst_object_unref(source_pipeline);
        g_object_unref(media_source);
        g_hash_table_unref(args);
        return FALSE;
    }

    if (source_tee->numsrcpads != 1) {
        gst_object_unref(source_pipeline);
        gst_object_unref(source_tee);
        g_object_unref(media_source);
        g_hash_table_unref(args);
        return FALSE;
    }

    _owr_media_source_set_source_bin(media_source, NULL);
    _owr_media_source_set_source_tee(media_source, NULL);

    gst_element_set_state(source_pipeline, GST_STATE_NULL);
    gst_object_unref(source_pipeline);
    gst_object_unref(source_tee);

    g_object_unref(media_source);
    g_hash_table_unref(args);

    return FALSE;
}

static void tee_pad_removed_cb(GstElement *tee, GstPad *old_pad, gpointer user_data)
{
    OwrMediaSource *media_source = user_data;

    OWR_UNUSED(old_pad);

    /* Only the fakesink is left, shutdown */
    if (tee->numsrcpads == 1) {
      GHashTable *args;

      args = g_hash_table_new(g_str_hash, g_str_equal);
      g_hash_table_insert(args, "media_source", media_source);
      g_object_ref (media_source);

      _owr_schedule_with_hash_table((GSourceFunc)shutdown_media_source, args);
    }
}

/*
 * owr_local_media_source_get_pad
 *
 * The beginning of a media source chain in the pipeline looks like this:
 *                                               +------------+
 *                                           /---+ fakesink   |
 * +--------+   +------------+   +-----+    /    +------------+
 * | source +---+ capsfilter +---+ tee +---/
 * +--------+   +------------+   +-----+   \
 *                                          \    +------------+
 *                                           \---+ inter*sink |
 *                                               +------------+
 *
 * For each newly requested pad a new inter*sink is added to the tee.
 * Note that this is a completely independent pipeline, and the complete
 * pipeline is only created once for a specific media source.
 *
 * Then for each newly requested pad another bin with a inter*src is
 * created, which is then going to be part of the transport agent
 * pipeline. The ghostpad of it is what we return here.
 *
 * +-----------+   +-------------------------------+   +----------+
 * | inter*src +---+ converters/queues/capsfilters +---+ ghostpad |
 * +-----------+   +-------------------------------+   +----------+
 *
 */
static GstElement *owr_local_media_source_request_source(OwrMediaSource *media_source, GstCaps *caps)
{
    OwrLocalMediaSource *local_source;
    OwrLocalMediaSourcePrivate *priv;
    GstElement *source_element = NULL;
    GstElement *source_pipeline;

    g_assert(media_source);
    local_source = OWR_LOCAL_MEDIA_SOURCE(media_source);
    priv = local_source->priv;

    /* only create the source bin for this media source once */
    if ((source_pipeline = _owr_media_source_get_source_bin(media_source))) {
        GST_DEBUG_OBJECT(media_source, "Re-using existing source element/bin");
    } else {
        OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
        OwrSourceType source_type = OWR_SOURCE_TYPE_UNKNOWN;
        GstElement *source, *capsfilter = NULL, *tee;
        GstElement *queue, *fakesink;
        GEnumClass *media_enum_class, *source_enum_class;
        GEnumValue *media_enum_value, *source_enum_value;
        gchar *bin_name;
        GstCaps *source_caps;
        GstStructure *source_structure;
        GstBus *bus;

        g_object_get(media_source, "media-type", &media_type, "type", &source_type, NULL);

        media_enum_class = G_ENUM_CLASS(g_type_class_ref(OWR_TYPE_MEDIA_TYPE));
        source_enum_class = G_ENUM_CLASS(g_type_class_ref(OWR_TYPE_SOURCE_TYPE));
        media_enum_value = g_enum_get_value(media_enum_class, media_type);
        source_enum_value = g_enum_get_value(source_enum_class, source_type);

        bin_name = g_strdup_printf("local-%s-%s-source-bin-%u",
            media_enum_value ? media_enum_value->value_nick : "unknown",
            source_enum_value ? source_enum_value->value_nick : "unknown",
            g_atomic_int_add(&unique_bin_id, 1));

        g_type_class_unref(media_enum_class);
        g_type_class_unref(source_enum_class);

        source_pipeline = gst_pipeline_new(bin_name);
        g_free(bin_name);
        bin_name = NULL;

#ifdef OWR_DEBUG
        g_signal_connect(source_pipeline, "deep-notify", G_CALLBACK(gst_object_default_deep_notify), NULL);
#endif

        bus = gst_pipeline_get_bus(GST_PIPELINE(source_pipeline));
        g_main_context_push_thread_default(_owr_get_main_context());
        gst_bus_add_watch(bus, (GstBusFunc)bus_call, source_pipeline);
        g_main_context_pop_thread_default(_owr_get_main_context());
        gst_object_unref(bus);

        GST_DEBUG_OBJECT(local_source, "media_type: %d, type: %d", media_type, source_type);

        if (media_type == OWR_MEDIA_TYPE_UNKNOWN || source_type == OWR_SOURCE_TYPE_UNKNOWN) {
            GST_ERROR_OBJECT(local_source,
                "Cannot connect source with unknown type or media type to other component");
            goto done;
        }

        switch (media_type) {
        case OWR_MEDIA_TYPE_AUDIO:
            {
            switch (source_type) {
            case OWR_SOURCE_TYPE_CAPTURE:
                CREATE_ELEMENT(source, AUDIO_SRC, "audio-source");
#if !defined(__APPLE__) || !TARGET_IPHONE_SIMULATOR
                g_object_set(source, "buffer-time", G_GINT64_CONSTANT(40000),
                    "latency-time", G_GINT64_CONSTANT(10000), NULL);
#ifdef __APPLE__
                g_object_set(source, "device", priv->device_index, NULL);
#endif
#endif
                break;
            case OWR_SOURCE_TYPE_TEST:
                CREATE_ELEMENT(source, "audiotestsrc", "audio-source");
                g_object_set(source, "is-live", TRUE, NULL);
                break;
            case OWR_SOURCE_TYPE_UNKNOWN:
            default:
                g_assert_not_reached();
                goto done;
            }

#if defined(__APPLE__) && !TARGET_IPHONE_SIMULATOR
            /* workaround for osxaudiosrc bug
             * https://bugzilla.gnome.org/show_bug.cgi?id=711764 */
            CREATE_ELEMENT(capsfilter, "capsfilter", "audio-source-capsfilter");
            source_caps = gst_caps_copy(caps);
            source_structure = gst_caps_get_structure(source_caps, 0);
            gst_structure_set(source_structure,
                "format", G_TYPE_STRING, "S32LE",
                "rate", G_TYPE_INT, 44100, NULL);
            gst_structure_remove_field(source_structure, "channels");
            g_object_set(capsfilter, "caps", source_caps, NULL);
            gst_caps_unref(source_caps);
            gst_bin_add(GST_BIN(source_pipeline), capsfilter);
#endif

            break;
            }
        case OWR_MEDIA_TYPE_VIDEO:
        {
            gint fps_n, fps_d;

            switch (source_type) {
            case OWR_SOURCE_TYPE_CAPTURE:
                CREATE_ELEMENT(source, VIDEO_SRC, "video-source");
#if defined(__APPLE__) && !TARGET_IPHONE_SIMULATOR
                g_object_set(source, "device-index", priv->device_index, NULL);
#elif defined(__ANDROID__)
                g_object_set(source, "cam-index", priv->device_index, NULL);
#elif defined(__linux__)
                {
                    gchar *device = g_strdup_printf("/dev/video%u", priv->device_index);
                    g_object_set(source, "device", device, NULL);
                    g_free(device);
                }
#endif
                break;
            case OWR_SOURCE_TYPE_TEST:
                CREATE_ELEMENT(source, "videotestsrc", "video-source");
                g_object_set(source, "is-live", TRUE, NULL);
                break;
            case OWR_SOURCE_TYPE_UNKNOWN:
            default:
                g_assert_not_reached();
                goto done;
            }

            CREATE_ELEMENT(capsfilter, "capsfilter", "video-source-capsfilter");
            source_caps = gst_caps_copy(caps);
            source_structure = gst_caps_get_structure(source_caps, 0);
            gst_structure_remove_field(source_structure, "format");

            /* If possible try to limit the framerate at the source already */
            if (gst_structure_get_fraction(source_structure, "framerate", &fps_n, &fps_d)) {
              GstStructure *tmp = gst_structure_copy(source_structure);
              gst_structure_remove_field(tmp, "framerate");
              gst_caps_append_structure(source_caps, tmp);
            }
            g_object_set(capsfilter, "caps", source_caps, NULL);
            gst_caps_unref(source_caps);
            gst_bin_add(GST_BIN(source_pipeline), capsfilter);

            break;
        }
        case OWR_MEDIA_TYPE_UNKNOWN:
        default:
            g_assert_not_reached();
            goto done;
        }
        g_assert(source);

        CREATE_ELEMENT(tee, "tee", "source-tee");

        CREATE_ELEMENT(queue, "queue", "source-tee-fakesink-queue");

        CREATE_ELEMENT(fakesink, "fakesink", "source-tee-fakesink");
        g_object_set(fakesink, "async", FALSE, NULL);

        gst_bin_add_many(GST_BIN(source_pipeline), source, tee, queue, fakesink, NULL);

        gst_element_sync_state_with_parent(queue);
        gst_element_sync_state_with_parent(fakesink);
        LINK_ELEMENTS(tee, queue);
        LINK_ELEMENTS(queue, fakesink);

        if (!source)
            GST_ERROR_OBJECT(media_source, "Failed to create source element!");

        if (capsfilter) {
            LINK_ELEMENTS(capsfilter, tee);
            gst_element_sync_state_with_parent(tee);
            gst_element_sync_state_with_parent(capsfilter);
            LINK_ELEMENTS(source, capsfilter);
        } else {
            gst_element_sync_state_with_parent(tee);
            LINK_ELEMENTS(source, tee);
        }
        gst_element_sync_state_with_parent(source);
        _owr_media_source_set_source_bin(media_source, source_pipeline);
        _owr_media_source_set_source_tee(media_source, tee);
        if (gst_element_set_state(source_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            GST_ERROR("Failed to set local source pipeline %s to playing", GST_OBJECT_NAME(source_pipeline));
            /* FIXME: We should handle this and don't expose the source */
        }

        g_signal_connect(tee, "pad-removed", G_CALLBACK(tee_pad_removed_cb), media_source);
    }
    gst_object_unref(source_pipeline);

    source_element = OWR_MEDIA_SOURCE_CLASS(owr_local_media_source_parent_class)->request_source (media_source, caps);

done:
    return source_element;
}

OwrLocalMediaSource *_owr_local_media_source_new(gchar *name, OwrMediaType media_type,
    OwrSourceType source_type)
{
    OwrLocalMediaSource *source;

    source = g_object_new(OWR_TYPE_LOCAL_MEDIA_SOURCE,
        "name", name,
        "media-type", media_type,
        NULL);

    _owr_media_source_set_type(OWR_MEDIA_SOURCE(source), source_type);

    return source;
}

void _owr_local_media_source_set_capture_device_index(OwrLocalMediaSource *source, guint index)
{
    OwrSourceType source_type = -1;
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(source));
    g_object_get(source, "type", &source_type, NULL);
    g_return_if_fail(source_type == OWR_SOURCE_TYPE_CAPTURE);
    source->priv->device_index = index;
}
