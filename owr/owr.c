/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
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

#ifdef OWR_STATIC
GST_PLUGIN_STATIC_DECLARE(alaw);
GST_PLUGIN_STATIC_DECLARE(app);
GST_PLUGIN_STATIC_DECLARE(audioconvert);
GST_PLUGIN_STATIC_DECLARE(audioresample);
GST_PLUGIN_STATIC_DECLARE(audiotestsrc);
GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(dtls);
GST_PLUGIN_STATIC_DECLARE(inter);
GST_PLUGIN_STATIC_DECLARE(mulaw);
GST_PLUGIN_STATIC_DECLARE(nice);
GST_PLUGIN_STATIC_DECLARE(opengl);
GST_PLUGIN_STATIC_DECLARE(openh264);
GST_PLUGIN_STATIC_DECLARE(opus);
GST_PLUGIN_STATIC_DECLARE(rtp);
GST_PLUGIN_STATIC_DECLARE(rtpmanager);
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

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR && !defined(__arm64__))
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

#ifdef OWR_STATIC
    GST_PLUGIN_STATIC_REGISTER(alaw);
    GST_PLUGIN_STATIC_REGISTER(app);
    GST_PLUGIN_STATIC_REGISTER(audioconvert);
    GST_PLUGIN_STATIC_REGISTER(audioresample);
    GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
    GST_PLUGIN_STATIC_REGISTER(coreelements);
    GST_PLUGIN_STATIC_REGISTER(dtls);
    GST_PLUGIN_STATIC_REGISTER(inter);
    GST_PLUGIN_STATIC_REGISTER(mulaw);
    GST_PLUGIN_STATIC_REGISTER(nice);
    GST_PLUGIN_STATIC_REGISTER(opengl);
    GST_PLUGIN_STATIC_REGISTER(openh264);
    GST_PLUGIN_STATIC_REGISTER(opus);
    GST_PLUGIN_STATIC_REGISTER(rtp);
    GST_PLUGIN_STATIC_REGISTER(rtpmanager);
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
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR && !defined(__arm64__))
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

void _owr_schedule_with_user_data(GSourceFunc func, gpointer user_data)
{
    GSource *source = g_idle_source_new();

    g_source_set_callback(source, func, user_data, NULL);
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_attach(source, owr_main_context);
}

/**
 * _owr_schedule_with_hash_table:
 * @func:
 * @hash_table: (transfer full):
 *
 */
void _owr_schedule_with_hash_table(GSourceFunc func, GHashTable *hash_table)
{
    _owr_schedule_with_user_data(func, hash_table);
}

