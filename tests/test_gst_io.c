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

#define _GNU_SOURCE 1

#include <string.h>

#include "owr.h"
#include "owr_media_renderer.h"
#include "owr_audio_renderer.h"
#include "owr_video_renderer.h"
#include "owr_gst_audio_renderer.h"
#include "owr_gst_video_renderer.h"
#include "owr_gst_media_source.h"

#define CUSTOM_AUDIO_SINK "pulsesink"
#define CUSTOM_VIDEO_SINK "xvimagesink"
#define CUSTOM_AUDIO_SOURCE "audiotestsrc wave=10"
#define CUSTOM_VIDEO_SOURCE "videotestsrc ! capsfilter caps=\"video/x-raw, width=(int)1280, height=(int)720\" ! videoscale"

static OwrMediaRenderer* add_renderer(OwrMediaSource *source, const gchar *bin_description)
{
    OwrMediaRenderer *renderer;
    GError *error = NULL;
    OwrMediaType type;
    GstElement *sink;

    g_object_get(source, "media-type", &type, NULL);
    sink = gst_parse_bin_from_description(bin_description, TRUE, &error);
    if (error != NULL) {
        g_printerr("Error parsing '%s': %s", bin_description, error->message);
        g_error_free(error);
        error = NULL;
        sink = NULL;
    }
    if (type == OWR_MEDIA_TYPE_VIDEO) {
        if (sink)
            renderer = OWR_MEDIA_RENDERER(owr_gst_video_renderer_new(sink));
        else
            renderer = OWR_MEDIA_RENDERER(owr_video_renderer_new(NULL));
    } else {
        if (sink)
            renderer = OWR_MEDIA_RENDERER(owr_gst_audio_renderer_new(sink));
        else
            renderer = OWR_MEDIA_RENDERER(owr_audio_renderer_new());
    }

    g_assert(renderer);
    owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);
    return renderer;
}

static OwrMediaSource* create_source(OwrMediaType type, const gchar *bin_description)
{
    OwrMediaSource *source;
    GstElement *gst_source;
    GError *error = NULL;

    gst_source = gst_parse_bin_from_description(bin_description, TRUE, &error);
    if (error != NULL) {
        g_printerr("Error parsing '%s': %s", bin_description, error->message);
        g_error_free(error);
        error = NULL;
        source = NULL;
    } else
        source = OWR_MEDIA_SOURCE(owr_gst_media_source_new(type, OWR_SOURCE_TYPE_CAPTURE, gst_source));
    return source;
}

int main() {
    OwrMediaSource *audio_source;
    OwrMediaSource *video_source;
    OwrMediaRenderer *audio_renderer;
    OwrMediaRenderer *video_renderer;

    owr_init(NULL);

    audio_source = create_source(OWR_MEDIA_TYPE_AUDIO, CUSTOM_AUDIO_SOURCE);
    if (audio_source)
        audio_renderer = add_renderer(audio_source, CUSTOM_AUDIO_SINK);

    video_source = create_source(OWR_MEDIA_TYPE_VIDEO, CUSTOM_VIDEO_SOURCE);
    if (video_source)
        video_renderer = add_renderer(video_source, CUSTOM_VIDEO_SINK);

    owr_run();

    return 0;
}
