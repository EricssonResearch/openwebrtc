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
\*\ OwrAudioRenderer
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_audio_renderer.h"
#include "owr_utils.h"

#include "owr_private.h"


#ifdef __APPLE__
#include <TargetConditionals.h>

#if TARGET_IPHONE_SIMULATOR
#define AUDIO_SINK  "osxaudiosink"
#else
#define AUDIO_SINK "osxaudiosink"
#endif /* TARGET_IPHONE_SIMULATOR */

#elif __ANDROID__ /* __APPLE__ */

#define AUDIO_SINK "openslessink"

#elif __linux__

#define AUDIO_SINK  "pulsesink"

#else

#define AUDIO_SINK  "fakesink"

#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
#define SINK_BUFFER_TIME G_GINT64_CONSTANT(80000)
#else
#define SINK_BUFFER_TIME G_GINT64_CONSTANT(20000)
#endif


#define OWR_AUDIO_RENDERER_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_AUDIO_RENDERER, OwrAudioRendererPrivate))

G_DEFINE_TYPE(OwrAudioRenderer, owr_audio_renderer, OWR_TYPE_MEDIA_RENDERER)

static guint unique_bin_id = 0;

static GstElement *owr_audio_renderer_get_element(OwrMediaRenderer *renderer);
static GstCaps *owr_audio_renderer_get_caps(OwrMediaRenderer *renderer);

struct _OwrAudioRendererPrivate {
    GMutex audio_renderer_lock;
    GstElement *renderer_bin;
};

static void owr_audio_renderer_finalize(GObject *object)
{
    OwrAudioRenderer *renderer = OWR_AUDIO_RENDERER(object);
    OwrAudioRendererPrivate *priv = renderer->priv;

    g_mutex_clear(&priv->audio_renderer_lock);

    G_OBJECT_CLASS(owr_audio_renderer_parent_class)->finalize(object);
}

static void owr_audio_renderer_class_init(OwrAudioRendererClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    OwrMediaRendererClass *media_renderer_class = OWR_MEDIA_RENDERER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrAudioRendererPrivate));

    gobject_class->finalize = owr_audio_renderer_finalize;

    media_renderer_class->get_element = (void *(*)(OwrMediaRenderer *))owr_audio_renderer_get_element;
    media_renderer_class->get_caps = (void *(*)(OwrMediaRenderer *))owr_audio_renderer_get_caps;

}

static void owr_audio_renderer_init(OwrAudioRenderer *renderer)
{
    OwrAudioRendererPrivate *priv;
    renderer->priv = priv = OWR_AUDIO_RENDERER_GET_PRIVATE(renderer);

    priv->renderer_bin = NULL;

    g_mutex_init(&priv->audio_renderer_lock);
}

/**
 * owr_audio_renderer_new: (constructor)
 *
 * Returns: The new #OwrAudioRenderer
 */
OwrAudioRenderer *owr_audio_renderer_new(void)
{
    return g_object_new(OWR_TYPE_AUDIO_RENDERER,
        "media-type", OWR_MEDIA_TYPE_AUDIO,
        NULL);
}

#define LINK_ELEMENTS(a, b) \
    if (!gst_element_link(a, b)) \
        GST_ERROR("Failed to link " #a " -> " #b);

static GstElement *owr_audio_renderer_get_element(OwrMediaRenderer *renderer)
{
    OwrAudioRenderer *audio_renderer;
    OwrAudioRendererPrivate *priv;
    GstElement *audioresample, *audioconvert, *capsfilter, *volume, *sink;
    GstCaps *filter_caps;
    GstPad *ghostpad, *sinkpad;
    gchar *bin_name;

    g_assert(renderer);
    audio_renderer = OWR_AUDIO_RENDERER(renderer);
    priv = audio_renderer->priv;

    g_mutex_lock(&priv->audio_renderer_lock);

    if (priv->renderer_bin)
        goto done;

    bin_name = g_strdup_printf("audio-renderer-bin-%u", g_atomic_int_add(&unique_bin_id, 1));
    priv->renderer_bin = gst_bin_new(bin_name);
    g_free(bin_name);

    audioresample = gst_element_factory_make("audioresample", "audio-renderer-resample");
    audioconvert = gst_element_factory_make("audioconvert", "audio-renderer-convert");

    capsfilter = gst_element_factory_make("capsfilter", "audio-renderer-capsfilter");
    filter_caps = gst_caps_new_simple("audio/x-raw",
        "format", G_TYPE_STRING, "S16LE", NULL);
    g_object_set(capsfilter, "caps", filter_caps, NULL);

    volume = gst_element_factory_make("volume", "audio-renderer-volume");
    g_object_bind_property(renderer, "disabled", volume, "mute", G_BINDING_SYNC_CREATE);

    sink = gst_element_factory_make(AUDIO_SINK, "audio-renderer-sink");
    g_assert(sink);

    g_object_set(sink, "buffer-time", SINK_BUFFER_TIME,
        "latency-time", G_GINT64_CONSTANT(10000), NULL);

    /* async false is needed when using live sources to not require prerolling
     * as prerolling is not possible from live sources in GStreamer */
    g_object_set(sink, "async", FALSE, NULL);

    gst_bin_add_many(GST_BIN(priv->renderer_bin), audioresample, audioconvert, capsfilter,
        volume, sink, NULL);

    LINK_ELEMENTS(volume, sink);
    LINK_ELEMENTS(capsfilter, volume);
    LINK_ELEMENTS(audioconvert, capsfilter);
    LINK_ELEMENTS(audioresample, audioconvert);

    sinkpad = gst_element_get_static_pad(audioresample, "sink");
    g_assert(sinkpad);
    ghostpad = gst_ghost_pad_new("sink", sinkpad);
    gst_pad_set_active(ghostpad, TRUE);
    gst_element_add_pad(priv->renderer_bin, ghostpad);
    gst_object_unref(sinkpad);

done:
    g_mutex_unlock(&priv->audio_renderer_lock);
    return priv->renderer_bin;
}

static GstCaps *owr_audio_renderer_get_caps(OwrMediaRenderer *renderer)
{
    GstCaps *caps = NULL;

    OWR_UNUSED(renderer);

    caps = gst_caps_new_simple("audio/x-raw",
        "format", G_TYPE_STRING, "S16LE",
        "layout", G_TYPE_STRING, "interleaved",
        NULL);
    return caps;
}
