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
#include "owr_uri_source.h"
#include "owr_uri_source_agent.h"
#include "owr_video_renderer.h"
#include "test_utils.h"

static OwrURISourceAgent *uri_source_agent = NULL;
static OwrMediaSource *audio_source = NULL, *video_source = NULL;
static OwrMediaRenderer *audio_renderer = NULL, *video_renderer = NULL;

/* For reading source selection from console in manual mode*/
#include <stdio.h>

#include <string.h>


gboolean dump_pipeline(gpointer user_data)
{
    g_print("Dumping pipelines\n");

    (void)user_data;

    if (uri_source_agent)
        write_dot_file("test_uri-source_agent", owr_uri_source_agent_get_dot_data(uri_source_agent), FALSE);

    if (audio_source)
        write_dot_file("test_uri-audio_source", owr_media_source_get_dot_data(audio_source), FALSE);
    if (audio_renderer)
        write_dot_file("test_uri-audio_renderer", owr_media_renderer_get_dot_data(audio_renderer), FALSE);

    if (video_source)
        write_dot_file("test_uri-video_source", owr_media_source_get_dot_data(video_source), FALSE);
    if (video_renderer)
        write_dot_file("test_uri-video_renderer", owr_media_renderer_get_dot_data(video_renderer), FALSE);

    return FALSE;
}

void on_new_source(gpointer *unused, OwrMediaSource *source)
{
    static gboolean have_video = FALSE, have_audio = FALSE;
    gchar *name = NULL;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;

    (void)unused;
    g_assert(OWR_IS_URI_SOURCE(source));

    if (have_video && have_audio)
        return;

    g_object_get(source, "name", &name, "media-type", &media_type, NULL);

    g_print("Got \"%s\" source!\n", name ? name : "");

    if (!have_video && media_type == OWR_MEDIA_TYPE_VIDEO) {
        OwrVideoRenderer *renderer;

        have_video = TRUE;

        renderer = owr_video_renderer_new(NULL);
        g_assert(renderer);

        owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);

        video_renderer = OWR_MEDIA_RENDERER(renderer);
        video_source = source;
    }

    if (!have_audio && media_type == OWR_MEDIA_TYPE_AUDIO) {
        OwrAudioRenderer *renderer;

        have_audio = TRUE;

        renderer = owr_audio_renderer_new();
        g_assert(renderer);

        owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);
        audio_renderer = OWR_MEDIA_RENDERER(renderer);
        audio_source = source;
    }

    if (have_video && have_audio) {
        g_print("Have audio and video streams!\n");
        owr_uri_source_agent_play(uri_source_agent);
        g_timeout_add(10000, dump_pipeline, NULL);
        return;
    }
}

int main(int argc, char *argv[])
{
    owr_init(NULL);

    if (argc != 2) {
        g_print("Invalid number of arguments. Usage:\n%s <uri>\n", argv[0]);
        return -1;
    }

    uri_source_agent = owr_uri_source_agent_new(argv[1]);
    g_signal_connect(uri_source_agent, "on-new-source", G_CALLBACK(on_new_source), NULL);
    owr_uri_source_agent_pause(uri_source_agent);

    owr_run();

    return 0;
}
