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

#define _GNU_SOURCE 1

#include <string.h>

#include "owr.h"
#include "owr_local.h"
#include "owr_media_source.h"
#include "owr_media_renderer.h"
#include "owr_audio_renderer.h"
#include "owr_video_renderer.h"

static OwrMediaSource *audio_source = NULL, *video_source = NULL;
static OwrMediaRenderer *audio_renderer = NULL, *video_renderer = NULL;

static gboolean dump_pipeline(gpointer user_data)
{
    g_print("Dumping pipelines\n");

    if (audio_source)
        owr_media_source_dump_dot_file(audio_source, "test_self_view-audio_source", FALSE);
    if (audio_renderer)
        owr_media_renderer_dump_dot_file(audio_renderer, "test_self_view-audio_renderer", FALSE);

    if (video_source)
        owr_media_source_dump_dot_file(video_source, "test_self_view-video_source", FALSE);
    if (video_renderer)
        owr_media_renderer_dump_dot_file(video_renderer, "test_self_view-video_renderer", FALSE);
    return FALSE;
}

static void got_sources(GList *sources, gpointer user_data)
{
    OwrMediaSource *source = NULL;
    static gboolean have_video = FALSE, have_audio = FALSE;
    g_assert(sources);

    g_print("Got sources!\n");

    while(sources && (source = sources->data)) {
        OwrMediaType media_type;
        OwrSourceType source_type;

        g_assert(OWR_IS_MEDIA_SOURCE(source));

        g_object_get(source, "type", &source_type, "media-type", &media_type, NULL);

        if (!have_video && media_type == OWR_MEDIA_TYPE_VIDEO && source_type == OWR_SOURCE_TYPE_CAPTURE) {
            OwrVideoRenderer *renderer;

            have_video = TRUE;

            renderer = owr_video_renderer_new(NULL);
            g_assert(renderer);

            g_object_set(renderer, "width", 1280, "height", 720, "max-framerate", 30.0, NULL);
            owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);
            video_renderer = OWR_MEDIA_RENDERER(renderer);
            video_source = source;
        } else if (!have_audio && media_type == OWR_MEDIA_TYPE_AUDIO && source_type == OWR_SOURCE_TYPE_CAPTURE) {
            OwrAudioRenderer *renderer;

            have_audio = TRUE;

            renderer = owr_audio_renderer_new();
            g_assert(renderer);

            owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);
            audio_renderer = OWR_MEDIA_RENDERER(renderer);
            audio_source = source;
        }

        if (have_video && have_audio)
            break;

        sources = sources->next;
    }

    g_timeout_add(5000, dump_pipeline, NULL);
}

int main() {
    GMainContext *ctx = g_main_context_default();
    GMainLoop *loop = g_main_loop_new(ctx, FALSE);

    owr_init_with_main_context(ctx);

    owr_get_capture_sources(OWR_MEDIA_TYPE_AUDIO|OWR_MEDIA_TYPE_VIDEO, got_sources, NULL);

    g_main_loop_run(loop);

    return 0;
}
