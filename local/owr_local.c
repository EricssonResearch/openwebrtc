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

/*/
\*\ OwrLocal
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_local.h"

#include "owr_device_list_private.h"
#include "owr_local_media_source.h"
#include "owr_local_media_source_private.h"
#include "owr_media_source.h"
#include "owr_media_source_private.h"
#include "owr_private.h"
#include "owr_utils.h"

GST_DEBUG_CATEGORY_EXTERN(_owrlocal_debug);
#define GST_CAT_DEFAULT _owrlocal_debug

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

static GList *get_test_sources(OwrMediaType types)
{
    static GList *cached_sources = NULL;
    OwrLocalMediaSource *source;
    OwrMediaType media_type;
    GList *result_list = NULL;
    GList *elem;

    if (g_once_init_enter(&cached_sources)) {
        GList *sources = NULL;

        source = _owr_local_media_source_new_cached(-1, "Audio test source", OWR_MEDIA_TYPE_AUDIO, OWR_SOURCE_TYPE_TEST);
        sources = g_list_append(sources, OWR_MEDIA_SOURCE(source));

        source = _owr_local_media_source_new_cached(-1, "Video test source", OWR_MEDIA_TYPE_VIDEO, OWR_SOURCE_TYPE_TEST);
        sources = g_list_append(sources, OWR_MEDIA_SOURCE(source));

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
 * @sources: (transfer none) (element-type OwrMediaSource): list of sources
 * @user_data: (allow-none): the data passed to owr_get_capture_sources
 *
 * Prototype for the callback passed to owr_get_capture_sources()
 */


/* PUBLIC */

/**
 * owr_get_capture_sources:
 * @types:
 * @callback: (scope async):
 * @user_data: (allow-none):
 */
void owr_get_capture_sources(OwrMediaType types, OwrCaptureSourcesCallback callback, gpointer user_data)
{
    GClosure *closure;
    GClosure *merger;

    g_return_if_fail(callback);

    closure = g_cclosure_new(G_CALLBACK(callback), user_data, NULL);
    g_closure_set_marshal(closure, g_cclosure_marshal_generic);

    if (g_getenv("OWR_USE_TEST_SOURCES")) {
        GList *sources;

        merger = _owr_utils_list_closure_merger_new(closure,
            (GCopyFunc) g_object_ref,
            (GDestroyNotify) g_object_unref);

        g_closure_ref(merger);
        _owr_get_capture_devices(types, merger);

        g_closure_ref(merger);
        sources = get_test_sources(types);
        _owr_utils_call_closure_with_list(merger, sources);
        g_list_free_full(sources, g_object_unref);

        g_closure_unref(merger);
    } else
        _owr_get_capture_devices(types, closure);
}
