/*
 * Copyright (c) 2014-2015, Ericsson AB. All rights reserved.
 * Copyright (c) 2014, Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
 *     Author: Arun Raghavan <arun@centricular.com>
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

#include "owr.h"
#include "owr_audio_renderer.h"
#include "owr_local.h"
#include "owr_media_renderer.h"
#include "owr_media_source.h"
#include "owr_video_renderer.h"
#include "test_utils.h"

static OwrMediaSource *audio_source = NULL, *video_source = NULL;
static OwrMediaRenderer *audio_renderer = NULL, *video_renderer = NULL;

#define MANUAL_SOURCE_SELECTION 0

/* For reading source selection from console in manual mode*/
#include <stdio.h>

#include <string.h>


gboolean dump_pipeline(gpointer user_data)
{
    g_print("Dumping pipelines\n");

    if (audio_source)
        write_dot_file("test_self_view-audio_source", owr_media_source_get_dot_data(audio_source), FALSE);
    if (audio_renderer)
        write_dot_file("test_self_view-audio_renderer", owr_media_renderer_get_dot_data(audio_renderer), FALSE);

    if (video_source)
        write_dot_file("test_self_view-video_source", owr_media_source_get_dot_data(video_source), FALSE);
    if (video_renderer)
        write_dot_file("test_self_view-video_renderer", owr_media_renderer_get_dot_data(video_renderer), FALSE);

    return FALSE;
}

static OwrMediaSource * pick_a_source(GList *sources, OwrMediaType type, gboolean manual)
{
    GList *item = sources;
    OwrMediaSource *source = NULL;
    OwrSourceType source_type;
    guint i = 1, sel = 0;
    gboolean have_one = FALSE;

    while (item) {
        gchar *name;
        OwrMediaType media_type;

        source = OWR_MEDIA_SOURCE(item->data);
        g_object_get(source, "name", &name, "media-type", &media_type, "type", &source_type, NULL);

        if (source_type == OWR_SOURCE_TYPE_CAPTURE && media_type == type) {
            if (manual) {
                g_print("%u: %s (%s)\n", i, name, media_type == OWR_MEDIA_TYPE_VIDEO ? "video" : "audio");
                have_one = TRUE;
            } else {
                g_free(name);
                return source;
            }
        }

        item = item->next;
        i++;

        g_free(name);
    }

    if (!have_one)
        return NULL;

    scanf("%u", &sel);

    if (sel < 1 || sel >= i)
        return NULL;

    return OWR_MEDIA_SOURCE((g_list_nth(sources, sel - 1))->data);
}

void got_sources(GList *sources, gpointer user_data)
{
    OwrMediaSource *source;
    static gboolean have_video = FALSE, have_audio = FALSE;

    g_assert(sources);

    g_print("Got sources!\n");

    if (have_video && have_audio)
        return;

    if (!have_video) {
        OwrVideoRenderer *renderer;

        source = pick_a_source(sources, OWR_MEDIA_TYPE_VIDEO, MANUAL_SOURCE_SELECTION);

        if (source) {
            have_video = TRUE;

            renderer = owr_video_renderer_new(NULL);
            g_assert(renderer);

            g_object_set(renderer, "width", 1280, "height", 720, "max-framerate", 30.0, NULL);
            owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);

            video_renderer = OWR_MEDIA_RENDERER(renderer);
            video_source = source;
        }
    }

    if (!have_audio) {
        OwrAudioRenderer *renderer;

        source = pick_a_source(sources, OWR_MEDIA_TYPE_AUDIO, MANUAL_SOURCE_SELECTION);

        if (source) {
            have_audio = TRUE;

            renderer = owr_audio_renderer_new();
            g_assert(renderer);

            owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);
            audio_renderer = OWR_MEDIA_RENDERER(renderer);
            audio_source = source;
        }
    }

    g_timeout_add(5000, dump_pipeline, NULL);
}

int main()
{
    owr_init(NULL);

    owr_get_capture_sources(OWR_MEDIA_TYPE_AUDIO | OWR_MEDIA_TYPE_VIDEO, got_sources, NULL);

    owr_run();

    return 0;
}
