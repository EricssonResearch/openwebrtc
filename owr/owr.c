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
\*\ Owr
/*/

/**
 * SECTION:owr
 * @title: owr
 *
 * Functions to initialize the OpenWebRTC library.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "owr.h"
#include "owr_private.h"
#include "owr_utils.h"

#include <gst/gst.h>

#ifdef OWR_STATIC
#include <stdlib.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

static gboolean owr_initialized = FALSE;
static GMainContext *owr_main_context = NULL;
static GMainLoop *owr_main_loop = NULL;

G_LOCK_DEFINE_STATIC(base_time);
static GstClockTime owr_base_time = GST_CLOCK_TIME_NONE;

GST_DEBUG_CATEGORY(_owraudiopayload_debug);
GST_DEBUG_CATEGORY(_owraudiorenderer_debug);
GST_DEBUG_CATEGORY(_owrbridge_debug);
GST_DEBUG_CATEGORY(_owrbus_debug);
GST_DEBUG_CATEGORY(_owrcandidate_debug);
GST_DEBUG_CATEGORY(_owrdatachannel_debug);
GST_DEBUG_CATEGORY(_owrdatasession_debug);
GST_DEBUG_CATEGORY(_owrcrypto_debug);
GST_DEBUG_CATEGORY(_owrdevicelist_debug);
GST_DEBUG_CATEGORY(_owrimagerenderer_debug);
GST_DEBUG_CATEGORY(_owrimageserver_debug);
GST_DEBUG_CATEGORY(_owrlocal_debug);
GST_DEBUG_CATEGORY(_owrlocalmediasource_debug);
GST_DEBUG_CATEGORY(_owrmediarenderer_debug);
GST_DEBUG_CATEGORY(_owrmediasession_debug);
GST_DEBUG_CATEGORY(_owrmediasource_debug);
GST_DEBUG_CATEGORY(_owrpayload_debug);
GST_DEBUG_CATEGORY(_owrremotemediasource_debug);
GST_DEBUG_CATEGORY(_owrsession_debug);
GST_DEBUG_CATEGORY(_owrtransportagent_debug);
GST_DEBUG_CATEGORY(_owrvideopayload_debug);
GST_DEBUG_CATEGORY(_owrvideorenderer_debug);
GST_DEBUG_CATEGORY(_owrwindowregistry_debug);

#ifdef OWR_STATIC
GST_PLUGIN_STATIC_DECLARE(alaw);
GST_PLUGIN_STATIC_DECLARE(app);
GST_PLUGIN_STATIC_DECLARE(audioconvert);
GST_PLUGIN_STATIC_DECLARE(audioresample);
GST_PLUGIN_STATIC_DECLARE(audiotestsrc);
GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(dtls);
GST_PLUGIN_STATIC_DECLARE(mulaw);
GST_PLUGIN_STATIC_DECLARE(nice);
GST_PLUGIN_STATIC_DECLARE(opengl);
GST_PLUGIN_STATIC_DECLARE(openh264);
GST_PLUGIN_STATIC_DECLARE(opus);
GST_PLUGIN_STATIC_DECLARE(rtp);
GST_PLUGIN_STATIC_DECLARE(rtpmanager);
GST_PLUGIN_STATIC_DECLARE(scream);
GST_PLUGIN_STATIC_DECLARE(sctp);
GST_PLUGIN_STATIC_DECLARE(srtp);
GST_PLUGIN_STATIC_DECLARE(videoconvert);
GST_PLUGIN_STATIC_DECLARE(videocrop);
GST_PLUGIN_STATIC_DECLARE(videofilter);
GST_PLUGIN_STATIC_DECLARE(videoparsersbad);
GST_PLUGIN_STATIC_DECLARE(videorate);
GST_PLUGIN_STATIC_DECLARE(videorepair);
GST_PLUGIN_STATIC_DECLARE(videoscale);
GST_PLUGIN_STATIC_DECLARE(videotestsrc);
GST_PLUGIN_STATIC_DECLARE(volume);
GST_PLUGIN_STATIC_DECLARE(vpx);

#ifdef __APPLE__
#if !TARGET_IPHONE_SIMULATOR
GST_PLUGIN_STATIC_DECLARE(applemedia);
GST_PLUGIN_STATIC_DECLARE(osxaudio);
#endif
#elif defined(__ANDROID__)
GST_PLUGIN_STATIC_DECLARE(androidvideosource);
GST_PLUGIN_STATIC_DECLARE(opensles);
#elif defined(__linux__)
GST_PLUGIN_STATIC_DECLARE(pulseaudio);
GST_PLUGIN_STATIC_DECLARE(video4linux2);
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR
GST_PLUGIN_STATIC_DECLARE(ercolorspace);
#endif
#endif

#ifdef __ANDROID__
static void g_print_android_handler(const gchar *message)
{
    __android_log_write(ANDROID_LOG_INFO, "g_print", message);
}

static void g_printerr_android_handler(const gchar *message)
{
    __android_log_write(ANDROID_LOG_ERROR, "g_printerr", message);
}

static void g_log_android_handler(const gchar *log_domain, GLogLevelFlags log_level,
    const gchar *message, gpointer unused_data)
{
    if ((log_level & G_LOG_LEVEL_MASK) <= G_LOG_LEVEL_WARNING) {
        __android_log_write(ANDROID_LOG_INFO, "g_log", message);
        g_log_default_handler(log_domain, log_level, message, unused_data);
    }
}

static void gst_log_android_handler(GstDebugCategory *category,
                 GstDebugLevel level,
                 const gchar *file,
                 const gchar *function,
                 gint line,
                 GObject *object,
                 GstDebugMessage *message,
                 gpointer data)
{
    gchar *obj = NULL;

    OWR_UNUSED(data);

    if (level > gst_debug_category_get_threshold(category))
      return;

    if (GST_IS_PAD(object) && GST_OBJECT_NAME(object)) {
      obj = g_strdup_printf("<%s:%s>", GST_DEBUG_PAD_NAME(object));
    } else if (GST_IS_OBJECT(object)) {
      obj = g_strdup_printf("<%s>", GST_OBJECT_NAME(object));
    }

    __android_log_print(ANDROID_LOG_INFO, "gst_log", "%p %s %s %s:%d:%s:%s %s\n",
            (void *)g_thread_self(),
            gst_debug_level_get_name(level), gst_debug_category_get_name(category),
            file, line, function, obj ? obj : "", gst_debug_message_get(message));

    g_free(obj);
}
#endif


/**
 * owr_init:
 * @ctx: #GMainContext to use inside OpenWebRTC, if NULL is passed the default main context is used.
 *
 * Initializes the OpenWebRTC library.
 */
void owr_init(GMainContext *main_context)
{
    g_return_if_fail(!owr_initialized);

#ifdef __ANDROID__
    g_set_print_handler((GPrintFunc)g_print_android_handler);
    g_set_printerr_handler((GPrintFunc)g_printerr_android_handler);
    g_log_set_default_handler((GLogFunc)g_log_android_handler, NULL);
    gst_debug_add_log_function((GstLogFunction)gst_log_android_handler, NULL, NULL);
#endif

#ifdef OWR_STATIC
    /* Hack to make sure that all symbols that we need are included in the resulting library/binary */
    if (!_owr_require_symbols()) {
      g_warning("Not all symbols found\n");
      abort();
    }
#endif

    gst_init(NULL, NULL);
    owr_initialized = TRUE;

    GST_DEBUG_CATEGORY_INIT(_owraudiopayload_debug, "owraudiopayload", 0,
        "OpenWebRTC Audio Payload");
    GST_DEBUG_CATEGORY_INIT(_owraudiorenderer_debug, "owraudiorenderer", 0,
        "OpenWebRTC Audio Renderer");
    GST_DEBUG_CATEGORY_INIT(_owrbridge_debug, "owrbridge", 0,
        "OpenWebRTC Bridge");
    GST_DEBUG_CATEGORY_INIT(_owrbus_debug, "owrbus", 0,
        "OpenWebRTC Bus");
    GST_DEBUG_CATEGORY_INIT(_owrcandidate_debug, "owrcandidate", 0,
        "OpenWebRTC Candidate");
    GST_DEBUG_CATEGORY_INIT(_owrcrypto_debug, "owrcrypto", 0,
        "OpenWebRTC Cryptography");
    GST_DEBUG_CATEGORY_INIT(_owrdatachannel_debug, "owrdatachannel", 0,
        "OpenWebRTC Data Channel");
    GST_DEBUG_CATEGORY_INIT(_owrdatasession_debug, "owrdatasession", 0,
        "OpenWebRTC Data Session");
    GST_DEBUG_CATEGORY_INIT(_owrdevicelist_debug, "owrdevicelist", 0,
        "OpenWebRTC Device List");
    GST_DEBUG_CATEGORY_INIT(_owrimagerenderer_debug, "owrimagerenderer", 0,
        "OpenWebRTC Image Renderer");
    GST_DEBUG_CATEGORY_INIT(_owrimageserver_debug, "owrimageserver", 0,
        "OpenWebRTC Image Server");
    GST_DEBUG_CATEGORY_INIT(_owrlocal_debug, "owrlocal", 0,
        "OpenWebRTC Local");
    GST_DEBUG_CATEGORY_INIT(_owrlocalmediasource_debug, "owrlocalmediasource", 0,
        "OpenWebRTC Local Media Source");
    GST_DEBUG_CATEGORY_INIT(_owrmediarenderer_debug, "owrmediarenderer", 0,
        "OpenWebRTC Media Renderer");
    GST_DEBUG_CATEGORY_INIT(_owrmediasession_debug, "owrmediasession", 0,
        "OpenWebRTC Media Session");
    GST_DEBUG_CATEGORY_INIT(_owrmediasource_debug, "owrmediasource", 0,
        "OpenWebRTC Media Source");
    GST_DEBUG_CATEGORY_INIT(_owrpayload_debug, "owrpayload", 0,
        "OpenWebRTC Payload");
    GST_DEBUG_CATEGORY_INIT(_owrremotemediasource_debug, "owrremotemediasource", 0,
        "OpenWebRTC Remote Media Source");
    GST_DEBUG_CATEGORY_INIT(_owrsession_debug, "owrsession", 0,
        "OpenWebRTC Session");
    GST_DEBUG_CATEGORY_INIT(_owrtransportagent_debug, "owrtransportagent", 0,
        "OpenWebRTC Transport Agent");
    GST_DEBUG_CATEGORY_INIT(_owrvideopayload_debug, "owrvideopayload", 0,
        "OpenWebRTC Video Payload");
    GST_DEBUG_CATEGORY_INIT(_owrvideorenderer_debug, "owrvideorenderer", 0,
        "OpenWebRTC Video Renderer");
    GST_DEBUG_CATEGORY_INIT(_owrwindowregistry_debug, "owrwindowregistry", 0,
        "OpenWebRTC Window Registry");

#ifdef OWR_STATIC
    GST_PLUGIN_STATIC_REGISTER(alaw);
    GST_PLUGIN_STATIC_REGISTER(app);
    GST_PLUGIN_STATIC_REGISTER(audioconvert);
    GST_PLUGIN_STATIC_REGISTER(audioresample);
    GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
    GST_PLUGIN_STATIC_REGISTER(coreelements);
    GST_PLUGIN_STATIC_REGISTER(dtls);
    GST_PLUGIN_STATIC_REGISTER(mulaw);
    GST_PLUGIN_STATIC_REGISTER(nice);
    GST_PLUGIN_STATIC_REGISTER(opengl);
    GST_PLUGIN_STATIC_REGISTER(openh264);
    GST_PLUGIN_STATIC_REGISTER(opus);
    GST_PLUGIN_STATIC_REGISTER(rtp);
    GST_PLUGIN_STATIC_REGISTER(rtpmanager);
    GST_PLUGIN_STATIC_REGISTER(scream);
    GST_PLUGIN_STATIC_REGISTER(sctp);
    GST_PLUGIN_STATIC_REGISTER(srtp);
    GST_PLUGIN_STATIC_REGISTER(videoconvert);
    GST_PLUGIN_STATIC_REGISTER(videocrop);
    GST_PLUGIN_STATIC_REGISTER(videofilter);
    GST_PLUGIN_STATIC_REGISTER(videoparsersbad);
    GST_PLUGIN_STATIC_REGISTER(videorate);
    GST_PLUGIN_STATIC_REGISTER(videorepair);
    GST_PLUGIN_STATIC_REGISTER(videoscale);
    GST_PLUGIN_STATIC_REGISTER(videotestsrc);
    GST_PLUGIN_STATIC_REGISTER(volume);
    GST_PLUGIN_STATIC_REGISTER(vpx);

#ifdef __APPLE__
#if !TARGET_IPHONE_SIMULATOR
    GST_PLUGIN_STATIC_REGISTER(applemedia);
    GST_PLUGIN_STATIC_REGISTER(osxaudio);
#endif
#elif defined(__ANDROID__)
    GST_PLUGIN_STATIC_REGISTER(androidvideosource);
    GST_PLUGIN_STATIC_REGISTER(opensles);
#elif defined(__linux__)
    GST_PLUGIN_STATIC_REGISTER(pulseaudio);
    GST_PLUGIN_STATIC_REGISTER(video4linux2);
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR
    GST_PLUGIN_STATIC_REGISTER(ercolorspace);
#endif

#endif

    owr_main_context = main_context;

    if (!owr_main_context)
        owr_main_context = g_main_context_ref_thread_default();
    else
        g_main_context_ref(owr_main_context);
}

static gboolean owr_running_callback(GAsyncQueue *msg_queue)
{
    g_return_val_if_fail(msg_queue, FALSE);
    g_async_queue_push(msg_queue, "ready");
    return G_SOURCE_REMOVE;
}

static gpointer owr_run_thread_func(GAsyncQueue *msg_queue)
{
    GSource *idle_source;

    g_return_val_if_fail(msg_queue, NULL);
    g_return_val_if_fail(owr_main_context, NULL);

    idle_source = g_idle_source_new();
    g_source_set_callback(idle_source, (GSourceFunc) owr_running_callback, msg_queue, NULL);
    g_source_set_priority(idle_source, G_PRIORITY_DEFAULT);
    g_source_attach(idle_source, owr_main_context);
    msg_queue = NULL;

    owr_run();
    return NULL;
}

/**
 * owr_run:
 *
 * Runs the OpenWebRTC main-loop inside the current thread.
 */
void owr_run(void)
{
    GMainLoop *main_loop;

    g_return_if_fail(owr_main_context);
    g_return_if_fail(!owr_main_loop);

    main_loop = g_main_loop_new(owr_main_context, FALSE);
    owr_main_loop = main_loop;
    g_main_loop_run(main_loop);
    g_main_loop_unref(main_loop);
}

/**
 * owr_run_in_background:
 *
 * Creates a new thread and runs the OpenWebRTC main-loop inside that thread.
 * This function does not return until the thread has started and the mainloop is running.
 */
void owr_run_in_background(void)
{
    GThread *owr_main_thread;
    GAsyncQueue *msg_queue = g_async_queue_new();

    g_return_if_fail(owr_main_context);
    g_return_if_fail(!owr_main_loop);

    owr_main_thread = g_thread_new("owr_main_loop", (GThreadFunc) owr_run_thread_func, msg_queue);
    g_thread_unref(owr_main_thread);
    g_async_queue_pop(msg_queue);
    g_async_queue_unref(msg_queue);
}

/**
 * owr_quit:
 *
 * Quits the OpenWebRTC main-loop, and stops the background thread if owr_run_in_background was used.
 */
void owr_quit(void)
{
    g_return_if_fail(owr_main_loop);
    g_main_loop_quit(owr_main_loop);
    owr_main_loop = NULL;
}

gboolean _owr_is_initialized()
{
    return owr_initialized;
}

GMainContext * _owr_get_main_context()
{
    return owr_main_context;
}

GstClockTime _owr_get_base_time()
{
    G_LOCK(base_time);
    if (!GST_CLOCK_TIME_IS_VALID(owr_base_time)) {
        GstClock *clock = gst_system_clock_obtain();

        owr_base_time = gst_clock_get_time(clock);
        gst_object_unref(clock);
    }
    G_UNLOCK(base_time);

    return owr_base_time;
}

void _owr_schedule_with_user_data(GSourceFunc func, gpointer user_data)
{
    GSource *source = g_idle_source_new();

    g_source_set_callback(source, func, user_data, NULL);
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_attach(source, owr_main_context);
}

static gboolean time_schedule_func(gpointer user_data)
{
    GHashTable *table, *data;
    GValue *value;
    OwrMessageOrigin *origin;
    GSourceFunc func;
    gboolean result;

    table = user_data;

    func = g_hash_table_lookup(table, "__func");
    origin = g_hash_table_lookup(table, "__origin");
    data = g_hash_table_lookup(table, "__data");

    value = _owr_value_table_add(data, "call_time", G_TYPE_INT64);
    g_value_set_int64(value, g_get_monotonic_time());

    result = func(table);

    g_return_val_if_fail(OWR_IS_MESSAGE_ORIGIN(origin), result);

    value = _owr_value_table_add(data, "end_time", G_TYPE_INT64);
    g_value_set_int64(value, g_get_monotonic_time());

    OWR_POST_STATS(origin, SCHEDULE, data);

    return result;
}

/**
 * _owr_schedule_with_hash_table:
 * @func:
 * @hash_table: (transfer full):
 *
 */
void _owr_schedule_with_hash_table(GSourceFunc func, GHashTable *hash_table)
{
    if (g_hash_table_lookup(hash_table, "__data")) {
        g_hash_table_insert(hash_table, "__func", func);
        _owr_schedule_with_user_data(time_schedule_func, hash_table);
    } else
        _owr_schedule_with_user_data(func, hash_table);
}

GHashTable *_owr_create_schedule_table_func(OwrMessageOrigin *origin, const gchar *function_name)
{
    GHashTable *args, *stats_table;
    GValue *value;

    stats_table = _owr_value_table_new();

    value = _owr_value_table_add(stats_table, "start_time", G_TYPE_INT64);
    g_value_set_int64(value, g_get_monotonic_time());

    value = _owr_value_table_add(stats_table, "function_name", G_TYPE_STRING);
    g_value_set_static_string(value, function_name);

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "__data", stats_table);
    g_hash_table_insert(args, "__origin", g_object_ref(origin));

    return args;
}
