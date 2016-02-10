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

#define _GNU_SOURCE 1

#include <string.h>

#include "owr.h"
#include "owr_local.h"
#include "owr_media_source.h"

static void got_sources(GList *sources, gpointer user_data)
{
    OwrMediaSource *source = NULL;
    g_assert(sources);

    while(sources && (source = sources->data)) {
        gchar *name = NULL;
        OwrMediaType media_type;
        OwrSourceType source_type;

        g_assert(OWR_IS_MEDIA_SOURCE(source));

        g_object_get(source, "name", &name, "type", &source_type, "media-type", &media_type, NULL);

        g_print("[%s/%s] %s\n", media_type == OWR_MEDIA_TYPE_AUDIO ? "audio" : "video",
            source_type == OWR_SOURCE_TYPE_CAPTURE ? "capture" : source_type == OWR_SOURCE_TYPE_TEST ? "test" : "unknown",
            name);

        sources = sources->next;
    }

    owr_quit();
}

int main() {
    owr_init(NULL);

    owr_get_capture_sources(OWR_MEDIA_TYPE_AUDIO|OWR_MEDIA_TYPE_VIDEO, got_sources, NULL);
    owr_run();

    return 0;
}
