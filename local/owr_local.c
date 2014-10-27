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
\*\ OwrLocal
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_local.h"

#include "owr_local_media_source.h"
#include "owr_local_media_source_private.h"
#include "owr_media_source.h"
#include "owr_media_source_private.h"
#include "owr_private.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define PRIMARY_VIDEO_DEVICE_INDEX 1
#define SECONDARY_VIDEO_DEVICE_INDEX 0
#else
#define PRIMARY_VIDEO_DEVICE_INDEX 0
#define SECONDARY_VIDEO_DEVICE_INDEX 1
#endif

/* PRIVATE */

static GList *get_capture_sources(OwrMediaType types)
{
    static GList *cached_sources = NULL;
    OwrLocalMediaSource *source;
    OwrMediaType media_type;
    GList *result_list = NULL;
    GList *elem;

    if (g_once_init_enter(&cached_sources)) {
        GList *sources = NULL;

        /* FIXME: This code makes no sense at all, we shouldn't hardcode
         * capture sources but check what is available. Not everybody has
         * /dev/video0 and /dev/video1, and not always are they camera
         * sources...
         * Use GstDeviceMonitor here! */
        source = _owr_local_media_source_new("Audio capture source", OWR_MEDIA_TYPE_AUDIO,
            OWR_SOURCE_TYPE_CAPTURE);
        sources = g_list_append(sources, OWR_MEDIA_SOURCE(source));
        source = _owr_local_media_source_new("Video capture source", OWR_MEDIA_TYPE_VIDEO,
            OWR_SOURCE_TYPE_CAPTURE);
        _owr_local_media_source_set_capture_device_index(source, PRIMARY_VIDEO_DEVICE_INDEX);
        sources = g_list_append(sources, OWR_MEDIA_SOURCE(source));
        source = _owr_local_media_source_new("Video capture source", OWR_MEDIA_TYPE_VIDEO,
            OWR_SOURCE_TYPE_CAPTURE);
        _owr_local_media_source_set_capture_device_index(source, SECONDARY_VIDEO_DEVICE_INDEX);
        sources = g_list_append(sources, OWR_MEDIA_SOURCE(source));

        if (g_getenv("OWR_USE_TEST_SOURCES")) {
            source = _owr_local_media_source_new("Video test source", OWR_MEDIA_TYPE_VIDEO,
                OWR_SOURCE_TYPE_TEST);
            sources = g_list_append(sources, OWR_MEDIA_SOURCE(source));
            source = _owr_local_media_source_new("Audio test source", OWR_MEDIA_TYPE_AUDIO,
                OWR_SOURCE_TYPE_TEST);
            sources = g_list_append(sources, OWR_MEDIA_SOURCE(source));
        }
        g_once_init_leave(&cached_sources, sources);
    }

    for (elem = cached_sources; elem; elem = elem->next) {
        source = OWR_LOCAL_MEDIA_SOURCE(elem->data);

        g_object_get(source, "media-type", &media_type, NULL);

        if (types & media_type) {
            g_object_ref(source);
            result_list = g_list_append(result_list, source);
        }
    }

    return result_list;
}


/**
 * OwrCaptureSourcesCallback:
 * @sources: (transfer full) (element-type OwrMediaSource): list of sources
 * @user_data: (allow-none): the data passed to owr_get_capture_sources
 *
 * Prototype for the callback passed to owr_get_capture_sources()
 */


/* wrapper function to always only call the callback once */
static gboolean capture_sources_callback(GHashTable *args)
{
    OwrCaptureSourcesCallback *callbacks;
    OwrMediaType media_types;
    gpointer user_data;

    g_return_val_if_fail(args, FALSE);

    callbacks = g_hash_table_lookup(args, "callback");
    media_types = GPOINTER_TO_UINT(g_hash_table_lookup(args, "media_types"));
    user_data = g_hash_table_lookup(args, "user_data");
    callbacks[0](get_capture_sources(media_types), user_data);

    g_free(callbacks);
    g_hash_table_unref(args);

    return FALSE;
}


/* PUBLIC */

/**
 * owr_get_capture_sources:
 * @types:
 * @callback: (scope async):
 * @user_data: (allow-none):
 */
void owr_get_capture_sources(OwrMediaType types, OwrCaptureSourcesCallback callback, gpointer user_data)
{
    GHashTable *args;
    OwrCaptureSourcesCallback *callbacks;

    g_return_if_fail(callback);

    callbacks = g_new(OwrCaptureSourcesCallback, 1);
    callbacks[0] = callback;
    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "callback", callbacks);
    g_hash_table_insert(args, "media_types", GUINT_TO_POINTER(types));
    g_hash_table_insert(args, "user_data", user_data);

    _owr_schedule_with_hash_table((GSourceFunc)capture_sources_callback, args);
}
