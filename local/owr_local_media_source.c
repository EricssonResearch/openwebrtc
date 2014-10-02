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

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && !TARGET_IPHONE_SIMULATOR
#define AUDIO_SRC "osxaudiosrc"
#define VIDEO_SRC "avfvideosrc"

#if TARGET_OS_IPHONE
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


struct _OwrLocalMediaSourcePrivate {
    guint device_index;

    GstElement *source_tee;
};

static void owr_local_media_source_class_init(OwrLocalMediaSourceClass *klass)
{
    OwrMediaSourceClass *media_source_class = OWR_MEDIA_SOURCE_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrLocalMediaSourcePrivate));

    media_source_class->get_pad = (void *(*)(OwrMediaSource *, void *))owr_local_media_source_get_pad;
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

/*
 * create_post_tee_bin
 *
 * The following chain is created after the tee for each output from the
 * source:
 *
 *    +-------+   +---------------------+   +-------+
 * ---+ queue +---+ conversion elements +---+ queue +---
 *    +-------+   +---------------------+   +-------+
 */
static GstElement *create_post_tee_bin(OwrMediaSource *media_source, GstElement *source_bin,
    GstCaps *caps, GstPad *ghostpad, guint source_id)
{
    OwrMediaType media_type;
    GstElement *post_tee_bin, *queue_pre, *queue_post, *capsfilter;
    GstPad *bin_pad, *queue_pre_pad, *srcpad;
    GSList *list = NULL;
    gchar *bin_name;

    bin_name = g_strdup_printf("source-post-tee-bin-%u", source_id);
    post_tee_bin = gst_bin_new(bin_name);
    if (!gst_bin_add(GST_BIN(source_bin), post_tee_bin)) {
        GST_ERROR("Failed to add %s to source bin", bin_name);
        g_free(bin_name);
        g_object_unref(post_tee_bin);
        post_tee_bin = NULL;
        goto done;
    }
    g_free(bin_name);
    gst_element_sync_state_with_parent(post_tee_bin);

    CREATE_ELEMENT_WITH_ID(queue_pre, "queue", "source-post-tee-queue", source_id);
    CREATE_ELEMENT_WITH_ID(capsfilter, "capsfilter", "source-output-capsfilter", source_id);
    list = g_slist_append(list, capsfilter);
    CREATE_ELEMENT_WITH_ID(queue_post, "queue", "source-output-queue", source_id);
    list = g_slist_append(list, queue_post);

    g_object_set(capsfilter, "caps", caps, NULL);

    g_object_get(media_source, "media-type", &media_type, NULL);
    switch (media_type) {
    case OWR_MEDIA_TYPE_AUDIO:
        {
        GstElement *audioresample, *audioconvert;

        CREATE_ELEMENT_WITH_ID(audioresample, "audioresample", "source-audio-resample", source_id);
        list = g_slist_prepend(list, audioresample);
        CREATE_ELEMENT_WITH_ID(audioconvert, "audioconvert", "source-audio-convert", source_id);
        list = g_slist_prepend(list, audioconvert);
        list = g_slist_prepend(list, queue_pre);

        gst_bin_add_many(GST_BIN(post_tee_bin),
            queue_pre, audioconvert, audioresample, capsfilter, queue_post, NULL);
        LINK_ELEMENTS(capsfilter, queue_post);
        LINK_ELEMENTS(audioresample, capsfilter);
        LINK_ELEMENTS(audioconvert, audioresample);
        LINK_ELEMENTS(queue_pre, audioconvert);

        break;
        }
    case OWR_MEDIA_TYPE_VIDEO:
        {
        GstElement *videorate, *videoscale, *videoconvert;

        CREATE_ELEMENT_WITH_ID(videoconvert, VIDEO_CONVERT, "source-video-convert", source_id);
        list = g_slist_prepend(list, videoconvert);
        CREATE_ELEMENT_WITH_ID(videoscale, "videoscale", "source-video-scale", source_id);
        list = g_slist_prepend(list, videoscale);
        CREATE_ELEMENT_WITH_ID(videorate, "videorate", "source-video-rate", source_id);
        list = g_slist_prepend(list, videorate);
        list = g_slist_prepend(list, queue_pre);

        gst_bin_add_many(GST_BIN(post_tee_bin),
            queue_pre, videorate, videoscale, videoconvert, capsfilter, queue_post, NULL);
        LINK_ELEMENTS(capsfilter, queue_post);
        LINK_ELEMENTS(videoconvert, capsfilter);
        LINK_ELEMENTS(videoscale, videoconvert);
        LINK_ELEMENTS(videorate, videoscale);
        LINK_ELEMENTS(queue_pre, videorate);

        break;
        }
    case OWR_MEDIA_TYPE_UNKNOWN:
    default:
        g_assert_not_reached();
        goto done;
    }

    srcpad = gst_element_get_static_pad(queue_post, "src");
    g_assert(srcpad);

    bin_pad = gst_ghost_pad_new("src", srcpad);
    gst_pad_set_active(bin_pad, TRUE);
    gst_element_add_pad(post_tee_bin, bin_pad);
    gst_object_unref(srcpad);

    gst_ghost_pad_set_target(GST_GHOST_PAD(ghostpad), bin_pad);
    gst_pad_set_active(ghostpad, TRUE);
    gst_element_add_pad(source_bin, ghostpad);

    g_slist_foreach(list, sync_to_parent, NULL);

    queue_pre_pad = gst_element_get_static_pad(queue_pre, "sink");
    g_assert(queue_pre_pad);

    bin_pad = gst_ghost_pad_new("sink", queue_pre_pad);
    gst_pad_set_active(bin_pad, TRUE);
    gst_element_add_pad(post_tee_bin, bin_pad);
    gst_object_unref(queue_pre_pad);

done:
    g_slist_free(list);
    list = NULL;

    return post_tee_bin;
}

/* reconfiguration is not supported by all sources and can be disruptive
 * we will handle reconfiguration manually
 * FIXME: implement source reconfiguration support :) */
static GstPadProbeReturn drop_reconfigure_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    OWR_UNUSED(pad);
    OWR_UNUSED(user_data);

    if (GST_IS_EVENT(GST_PAD_PROBE_INFO_DATA(info))
        && GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_RECONFIGURE) {
        GST_DEBUG("Dropping reconfigure event");
        return GST_PAD_PROBE_DROP;
    }

    return GST_PAD_PROBE_OK;
}

/*
 * owr_local_media_source_get_pad
 *
 * The beginning of a media source chain in the pipeline looks like this:
 *
 * +--------+   +------------+   +-----+
 * | source +---+ capsfilter +---+ tee +---
 * +--------+   +------------+   +-----+
 *
 * Only one such chain is created per media source for the initial get_pad
 * call. Subsequent calls will just obtain another tee pad. After these initial
 * elements are created, they are linked together and synced up to the PLAYING
 * state.
 *
 * Once the initial chain is created, a block is placed on the new src pad of
 * the tee. The rest of the new chain (conversion elements, capsfilter, queues,
 * etc.) is created, linked and synced in the pad block callback.
 */
static GstPad *owr_local_media_source_get_pad(OwrMediaSource *media_source, GstCaps *caps)
{
    OwrLocalMediaSource *local_source;
    OwrLocalMediaSourcePrivate *priv;
    GstElement *source_bin, *post_tee_bin;
    GstElement *source = NULL, *capsfilter = NULL, *tee;
    GstPad *ghostpad = NULL;
    gchar *pad_name;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
    OwrSourceType source_type = OWR_SOURCE_TYPE_UNKNOWN;
    OwrCodecType codec_type = OWR_CODEC_TYPE_NONE;
    guint source_id;

    g_assert(media_source);
    local_source = OWR_LOCAL_MEDIA_SOURCE(media_source);
    priv = local_source->priv;

    g_object_get(media_source, "media-type", &media_type, "type", &source_type, NULL);

    /* only create the source bin for this media source once */
    if (_owr_media_source_get_element(media_source)) {
        GST_DEBUG_OBJECT(media_source, "Re-using existing source element/bin");
        source_bin = _owr_media_source_get_element(media_source);
        tee = priv->source_tee;
    } else {
        GEnumClass *media_enum_class, *source_enum_class;
        GEnumValue *media_enum_value, *source_enum_value;
        gchar *bin_name;
        GstCaps *source_caps;
        GstStructure *source_structure;
        GstElement *fakesink;

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

        source_bin = gst_bin_new(bin_name);

        g_free(bin_name);
        bin_name = NULL;

        gst_bin_add(GST_BIN(_owr_get_pipeline()), source_bin);
        gst_element_sync_state_with_parent(GST_ELEMENT(source_bin));

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
            gst_bin_add(GST_BIN(source_bin), capsfilter);
#endif

            break;
            }
        case OWR_MEDIA_TYPE_VIDEO:
        {
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
            gst_structure_remove_field(source_structure, "framerate");
            g_object_set(capsfilter, "caps", source_caps, NULL);
            gst_caps_unref(source_caps);
            gst_bin_add(GST_BIN(source_bin), capsfilter);

            break;
        }
        case OWR_MEDIA_TYPE_UNKNOWN:
        default:
            g_assert_not_reached();
            goto done;
        }
        g_assert(source);

        CREATE_ELEMENT(tee, "tee", "source-tee");

        CREATE_ELEMENT(fakesink, "fakesink", "source-tee-fakesink");
        g_object_set(fakesink, "async", FALSE, NULL);

        gst_bin_add_many(GST_BIN(source_bin), source, tee, fakesink, NULL);

        gst_element_sync_state_with_parent(fakesink);
        LINK_ELEMENTS(tee, fakesink);

        if (!source)
            GST_ERROR_OBJECT(media_source, "Failed to create source element!");
    }

    codec_type = _owr_caps_to_codec_type(caps);
    source_id = g_atomic_int_add(&unique_pad_id, 1);

    pad_name = g_strdup_printf("src_%u_%u", codec_type, source_id);
    ghostpad = gst_ghost_pad_new_no_target(pad_name, GST_PAD_SRC);
    g_free(pad_name);

    post_tee_bin = create_post_tee_bin(media_source, source_bin, caps, ghostpad, source_id);
    if (!post_tee_bin) {
        gst_object_unref(ghostpad);
        ghostpad = NULL;
        goto done;
    }

    if (!gst_element_link(tee, post_tee_bin)) {
        GST_ERROR("Failed to link source tee to source-post-tee-bin-%u", source_id);
        g_object_unref(post_tee_bin);
        ghostpad = NULL;
        goto done;
    }

    if (!_owr_media_source_get_element(media_source)) {
        /* the next code block inside the if is a workaround for avfvideosrc
         * not handling on-the-fly reconfiguration
         * on upstream reconfigure events, we drop the event in the probe */
        if (media_type == OWR_MEDIA_TYPE_VIDEO) {
            GstPad *tee_sinkpad;

            tee_sinkpad = gst_element_get_static_pad(tee, "sink");
            gst_pad_add_probe(tee_sinkpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
                drop_reconfigure_cb, NULL, NULL);
        }

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
        _owr_media_source_set_element(media_source, source_bin);
        priv->source_tee = tee;
    }

done:
    return ghostpad;
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
