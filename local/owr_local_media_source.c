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

#if TARGET_OS_IPHONE && !defined(__arm64__)
#define VIDEO_CONVERT "ercolorspace"
#else
#define VIDEO_CONVERT "videoconvert"
#endif

#elif defined(__ANDROID__)
#define AUDIO_SRC "openslessrc"
#define VIDEO_SRC "androidvideosource"
#define VIDEO_CONVERT "ercolorspace"

#elif defined(__linux__)
#define AUDIO_SRC  "pulsesrc"
#define VIDEO_SRC  "v4l2src"
#define VIDEO_CONVERT "videoconvert"

#else
#define AUDIO_SRC  "audiotestsrc"
#define VIDEO_SRC  "videotestsrc"
#define VIDEO_CONVERT "videoconvert"
#endif


static guint unique_bin_id = 0;
static guint unique_pad_id = 0;

#define OWR_LOCAL_MEDIA_SOURCE_GET_PRIVATE(obj) \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_LOCAL_MEDIA_SOURCE, OwrLocalMediaSourcePrivate))

G_DEFINE_TYPE(OwrLocalMediaSource, owr_local_media_source, OWR_TYPE_MEDIA_SOURCE)

static GstPad *owr_local_media_source_get_pad(OwrMediaSource *media_source, GstCaps *caps);
static void owr_local_media_source_unlink(OwrMediaSource *media_source, GstPad *downstream_pad);

struct _OwrLocalMediaSourcePrivate {
    guint device_index;

    GstElement *source_tee;
};

static void owr_local_media_source_class_init(OwrLocalMediaSourceClass *klass)
{
    OwrMediaSourceClass *media_source_class = OWR_MEDIA_SOURCE_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrLocalMediaSourcePrivate));

    media_source_class->get_pad = (void *(*)(OwrMediaSource *, void *))owr_local_media_source_get_pad;
    media_source_class->unlink = (void (*)(OwrMediaSource *, void *))owr_local_media_source_unlink;
}

static void owr_local_media_source_init(OwrLocalMediaSource *source)
{
    OwrLocalMediaSourcePrivate *priv;
    source->priv = priv = OWR_LOCAL_MEDIA_SOURCE_GET_PRIVATE(source);
    priv->device_index = 0;
}


static void sync_to_parent(gpointer data, gpointer user_data)
{
    GstElement *element = GST_ELEMENT(data);

    g_assert(data);
    g_assert(!user_data);

    gst_element_sync_state_with_parent(element);
}

#define LINK_ELEMENTS(a, b) \
    if (!gst_element_link(a, b)) \
        GST_ERROR("Failed to link " #a " -> " #b);

#define CREATE_ELEMENT(elem, factory, name) \
    elem = gst_element_factory_make(factory, name); \
    if (!elem) \
        GST_ERROR("Could not create " name " from factory " factory); \
    g_assert(elem);

#define CREATE_ELEMENT_WITH_ID(elem, factory, name, id) \
{ \
    gchar *temp_str = g_strdup_printf(name"-%u", id); \
    elem = gst_element_factory_make(factory, temp_str); \
    if (!elem) \
        GST_ERROR("Could not create %s from factory " factory, temp_str); \
    g_assert(elem); \
    g_free(temp_str); \
}

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

static void source_pad_linked_cb(GstPad *pad, GstPad *peer, GstElement *valve)
{
  OWR_UNUSED(pad);
  OWR_UNUSED(peer);
  g_object_set(valve, "drop", FALSE, NULL);
}

static void source_pad_unlinked_cb(GstPad *pad, GstPad *peer, GstElement *valve)
{
  OWR_UNUSED(pad);
  OWR_UNUSED(peer);
  g_object_set(valve, "drop", TRUE, NULL);
}

/*
 * create_post_tee_bin
 *
 * The following chain is created after the tee for each output from the
 * source:
 *
 * +-----------+   +-------------------------------+   +------ +   +----------+
 * | inter*src +---+ converters/queues/capsfilters +---+ valve +---+ ghostpad |
 * +-----------+   +-------------------------------+   +-------+   +----------+
 *
 * The valve is opened and closed based on linking/unlinking the ghostpad to
 * prevent not-linked errors.
 *
 * TODO: Share this with remote media sources
 *
 */
static GstPad *create_source_bin(OwrMediaSource *media_source, GstElement *source_pipeline,
    GstElement *tee, GstCaps *caps)
{
    OwrMediaType media_type;
    GstElement *source_bin, *source = NULL, *queue_pre, *queue_post;
    GstElement *valve, *capsfilter;
    GstElement *sink, *sink_queue, *sink_bin;
    GstPad *bin_pad = NULL, *srcpad, *sinkpad;
    GSList *list = NULL;
    gchar *bin_name;
    guint source_id;
    gchar *channel_name;

    source_id = g_atomic_int_add(&unique_pad_id, 1);

    bin_name = g_strdup_printf("local-source-bin-%u", source_id);
    source_bin = gst_bin_new(bin_name);
    /* TODO: This should be the transport agent or renderer pipeline later */
    if (!gst_bin_add(GST_BIN(_owr_get_pipeline()), source_bin)) {
        GST_ERROR("Failed to add %s to source bin", bin_name);
        g_free(bin_name);
        g_object_unref(source_bin);
        source_bin = NULL;
        goto done;
    }
    g_free(bin_name);
    gst_element_sync_state_with_parent(source_bin);

    CREATE_ELEMENT_WITH_ID(queue_pre, "queue", "source-queue", source_id);
    CREATE_ELEMENT_WITH_ID(capsfilter, "capsfilter", "source-output-capsfilter", source_id);
    list = g_slist_append(list, capsfilter);
    CREATE_ELEMENT_WITH_ID(queue_post, "queue", "source-output-queue", source_id);
    list = g_slist_append(list, queue_post);
    CREATE_ELEMENT_WITH_ID(valve, "valve", "source-valve", source_id);
    g_object_set(valve, "drop", TRUE, NULL);
    list = g_slist_append(list, valve);

    CREATE_ELEMENT_WITH_ID(sink_queue, "queue", "sink-queue", source_id);

    g_object_get(media_source, "media-type", &media_type, NULL);
    switch (media_type) {
    case OWR_MEDIA_TYPE_AUDIO:
        {
        GstElement *audioresample, *audioconvert;

        CREATE_ELEMENT_WITH_ID(source, "interaudiosrc", "source", source_id);
        CREATE_ELEMENT_WITH_ID(sink, "interaudiosink", "sink", source_id);

        g_object_set(capsfilter, "caps", caps, NULL);

        CREATE_ELEMENT_WITH_ID(audioresample, "audioresample", "source-audio-resample", source_id);
        list = g_slist_prepend(list, audioresample);
        CREATE_ELEMENT_WITH_ID(audioconvert, "audioconvert", "source-audio-convert", source_id);
        list = g_slist_prepend(list, audioconvert);
        list = g_slist_prepend(list, queue_pre);

        gst_bin_add_many(GST_BIN(source_bin),
            queue_pre, audioconvert, audioresample, capsfilter, queue_post, valve, NULL);
        LINK_ELEMENTS(capsfilter, queue_post);
        LINK_ELEMENTS(audioresample, capsfilter);
        LINK_ELEMENTS(audioconvert, audioresample);
        LINK_ELEMENTS(queue_pre, audioconvert);

        break;
        }
    case OWR_MEDIA_TYPE_VIDEO:
        {
        GstElement *videorate = NULL, *videoscale, *videoconvert;
        GstCaps *source_caps;
        GstStructure *source_structure;
        gint fps_n = 0, fps_d = 1;

        CREATE_ELEMENT_WITH_ID(source, "intervideosrc", "source", source_id);
        CREATE_ELEMENT_WITH_ID(sink, "intervideosink", "sink", source_id);

        source_caps = gst_caps_copy(caps);
        source_structure = gst_caps_get_structure(source_caps, 0);
        if (gst_structure_get_fraction(source_structure, "framerate", &fps_n, &fps_d))
            gst_structure_remove_field(source_structure, "framerate");
        g_object_set(capsfilter, "caps", source_caps, NULL);
        gst_caps_unref(source_caps);

        CREATE_ELEMENT_WITH_ID(videoconvert, VIDEO_CONVERT, "source-video-convert", source_id);
        list = g_slist_prepend(list, videoconvert);
        CREATE_ELEMENT_WITH_ID(videoscale, "videoscale", "source-video-scale", source_id);
        list = g_slist_prepend(list, videoscale);
        if (fps_n > 0) {
            CREATE_ELEMENT_WITH_ID(videorate, "videorate", "source-video-rate", source_id);
            g_object_set(videorate, "drop-only", TRUE, "max-rate", fps_n / fps_d, NULL);
            list = g_slist_prepend(list, videorate);
            gst_bin_add(GST_BIN(source_bin), videorate);
        }
        list = g_slist_prepend(list, queue_pre);

        gst_bin_add_many(GST_BIN(source_bin),
            queue_pre, videoscale, videoconvert, capsfilter, queue_post, valve, NULL);
        LINK_ELEMENTS(capsfilter, queue_post);
        LINK_ELEMENTS(videoconvert, capsfilter);
        LINK_ELEMENTS(videoscale, videoconvert);
        if (videorate) {
            LINK_ELEMENTS(videorate, videoscale);
            LINK_ELEMENTS(queue_pre, videorate);
        } else {
            LINK_ELEMENTS(queue_pre, videoscale);
        }

        break;
        }
    case OWR_MEDIA_TYPE_UNKNOWN:
    default:
        g_assert_not_reached();
        goto done;
    }

    channel_name = g_strdup_printf("local-source-%u", source_id);
    g_object_set(source, "channel", channel_name, NULL);
    g_object_set(sink, "channel", channel_name, NULL);
    g_free(channel_name);

    /* Add and link the inter*sink to the actual source pipeline */
    bin_name = g_strdup_printf("local-source-sink-bin-%u", source_id);
    sink_bin = gst_bin_new(bin_name);
    gst_bin_add_many(GST_BIN(sink_bin), sink, sink_queue, NULL);
    gst_element_sync_state_with_parent(sink);
    gst_element_sync_state_with_parent(sink_queue);
    LINK_ELEMENTS(sink_queue, sink);
    sinkpad = gst_element_get_static_pad(sink_queue, "sink");
    bin_pad = gst_ghost_pad_new("sink", sinkpad);
    gst_object_unref(sinkpad);
    gst_pad_set_active(bin_pad, TRUE);
    gst_element_add_pad(sink_bin, bin_pad);
    bin_pad = NULL;
    gst_bin_add(GST_BIN(source_pipeline), sink_bin);
    gst_element_sync_state_with_parent(sink_bin);
    LINK_ELEMENTS(tee, sink_bin);

    /* Start up our new bin and link it all */
    LINK_ELEMENTS(queue_post, valve);
    srcpad = gst_element_get_static_pad(valve, "src");
    g_assert(srcpad);

    bin_pad = gst_ghost_pad_new("src", srcpad);
    gst_object_unref(srcpad);
    g_signal_connect(bin_pad, "linked", G_CALLBACK(source_pad_linked_cb), valve);
    g_signal_connect(bin_pad, "unlinked", G_CALLBACK(source_pad_unlinked_cb), valve);
    gst_pad_set_active(bin_pad, TRUE);
    gst_element_add_pad(source_bin, bin_pad);

    gst_bin_add(GST_BIN(source_bin), source);
    LINK_ELEMENTS(source, queue_pre);
    list = g_slist_append(list, source);

    g_slist_foreach(list, sync_to_parent, NULL);

done:
    g_slist_free(list);
    list = NULL;

    return bin_pad;
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
static GstPad *owr_local_media_source_get_pad(OwrMediaSource *media_source, GstCaps *caps)
{
    OwrLocalMediaSource *local_source;
    OwrLocalMediaSourcePrivate *priv;
    GstElement *source_pipeline;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
    OwrSourceType source_type = OWR_SOURCE_TYPE_UNKNOWN;
    GstPad *pad = NULL;

    g_assert(media_source);
    local_source = OWR_LOCAL_MEDIA_SOURCE(media_source);
    priv = local_source->priv;

    g_object_get(media_source, "media-type", &media_type, "type", &source_type, NULL);

    /* only create the source bin for this media source once */
    if (_owr_media_source_get_element(media_source)) {
        GST_DEBUG_OBJECT(media_source, "Re-using existing source element/bin");
        source_pipeline = _owr_media_source_get_element(media_source);
    } else {
        GstElement *source, *capsfilter = NULL, *tee;
        GstElement *queue, *fakesink;
        GEnumClass *media_enum_class, *source_enum_class;
        GEnumValue *media_enum_value, *source_enum_value;
        gchar *bin_name;
        GstCaps *source_caps;
        GstStructure *source_structure;
        GstBus *bus;

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
        _owr_media_source_set_element(media_source, source_pipeline);
        priv->source_tee = tee;
        if (gst_element_set_state(source_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            GST_ERROR("Failed to set local source pipeline %s to playing", GST_OBJECT_NAME(source_pipeline));
            /* FIXME: We should handle this and don't expose the source */
        }
    }

    pad = create_source_bin(media_source, source_pipeline, priv->source_tee, caps);

done:
    return pad;
}

static gboolean shutdown_media_source(GHashTable *args)
{
    OwrMediaSource *media_source;
    OwrLocalMediaSource *local_media_source;
    GstElement *source_pipeline;

    media_source = g_hash_table_lookup(args, "media_source");
    g_assert(media_source);
    local_media_source = OWR_LOCAL_MEDIA_SOURCE(media_source);

    source_pipeline = _owr_media_source_get_element(media_source);
    if (!source_pipeline) {
        g_object_unref(media_source);
        return FALSE;
    }

    if (local_media_source->priv->source_tee->numsrcpads != 1) {
        g_object_unref(media_source);
        return FALSE;
    }

    _owr_media_source_set_element(media_source, NULL);
    local_media_source->priv->source_tee = NULL;

    gst_element_set_state(source_pipeline, GST_STATE_NULL);
    gst_object_unref(source_pipeline);

    g_object_unref(media_source);

    return FALSE;
}

static GstPadProbeReturn tee_idle_probe_cb(GstPad *teepad, GstPadProbeInfo *info, gpointer user_data)
{
    OwrMediaSource * media_source = user_data;
    GstElement *sink_bin;
    GstPad *sinkpad;
    GstObject *parent;
    GstElement *tee;

    gst_pad_remove_probe(teepad, GST_PAD_PROBE_INFO_ID(info));

    sinkpad = gst_pad_get_peer(teepad);
    g_assert(sinkpad);
    sink_bin = GST_ELEMENT(gst_object_get_parent(GST_OBJECT(sinkpad)));

    g_warn_if_fail(gst_pad_unlink(teepad, sinkpad));
    parent = gst_pad_get_parent(teepad);
    tee = GST_ELEMENT(parent);
    gst_element_release_request_pad(tee, teepad);
    gst_object_unref(parent);
    gst_object_unref(sinkpad);

    parent = gst_object_get_parent(GST_OBJECT(sink_bin));
    g_assert(parent);

    gst_bin_remove(GST_BIN(parent), sink_bin);
    gst_element_set_state(sink_bin, GST_STATE_NULL);
    gst_object_unref(sink_bin);
    gst_object_unref(parent);

    /* Only the fakesink is left, shutdown */
    if (tee->numsrcpads == 1) {
      GHashTable *args;

      args = g_hash_table_new(g_str_hash, g_str_equal);
      g_hash_table_insert(args, "media_source", media_source);
      g_object_ref (media_source);

      _owr_schedule_with_hash_table((GSourceFunc)shutdown_media_source, args);
    }

    return GST_PAD_PROBE_OK;
}

static void owr_local_media_source_unlink(OwrMediaSource *media_source, GstPad *downstream_pad)
{
    GstPad *srcpad, *sinkpad;
    gchar *pad_name, *bin_name, *parent_name;
    guint source_id = -1;
    GstObject *parent;
    GstElement *sink_bin, *source_pipeline;

    srcpad = gst_pad_get_peer(downstream_pad);
    g_assert(srcpad);

    pad_name = gst_pad_get_name(srcpad);
    if (!pad_name || strcmp(pad_name, "src")) {
        GST_WARNING_OBJECT(media_source, "Failed to get pad name when unlinking: %s",
                pad_name ? pad_name : "");
        g_free(pad_name);
        return;
    }

    g_free(pad_name);

    parent = gst_pad_get_parent(srcpad);
    parent_name = gst_object_get_name(GST_OBJECT(parent));
    gst_object_unref(srcpad);

    if (!parent_name || sscanf(parent_name, "local-source-bin-%u", &source_id) != 1) {
        GST_WARNING_OBJECT(media_source,
                "Failed to get %s for clean up", parent_name);
        return;
    }
    g_free(parent_name);

    GST_DEBUG_OBJECT(media_source, "Unlinking source %u", source_id);
    /* Automatically unlinks everything */
    gst_element_set_state(GST_ELEMENT(parent), GST_STATE_NULL);
    g_warn_if_fail(gst_bin_remove(GST_BIN(_owr_get_pipeline()), GST_ELEMENT(parent)));
    GST_DEBUG_OBJECT(media_source, "Source %u successfully unlinked", source_id);

    /* Unlink parts from the source pipeline */
    source_pipeline = _owr_media_source_get_element(media_source);
    g_assert(source_pipeline);
    bin_name = g_strdup_printf("local-source-sink-bin-%u", source_id);
    sink_bin = gst_bin_get_by_name(GST_BIN(source_pipeline), bin_name);
    if (!sink_bin) {
        GST_WARNING_OBJECT(media_source,
                "Failed to get %s from source pipeline", bin_name);
        g_free(bin_name);
        return;
    }
    g_free(bin_name);

    sinkpad = gst_element_get_static_pad(sink_bin, "sink");
    /* The pad on the tee */
    srcpad = gst_pad_get_peer(sinkpad);
    gst_object_unref(sinkpad);

    gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_IDLE, tee_idle_probe_cb, media_source, NULL);
    gst_object_unref(srcpad);
    gst_object_unref(sink_bin);
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
