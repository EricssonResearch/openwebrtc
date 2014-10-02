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
#include <gstnice.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

static gboolean owr_initialized = FALSE;
static GMainContext *owr_main_context = NULL;
static GMainLoop *owr_main_loop = NULL;
static GThread *owr_main_thread = NULL;
static GstElement *owr_pipeline = NULL;

static guint bus_watch_id = -1;

static gpointer owr_run(gpointer data);
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer user_data);

GST_PLUGIN_STATIC_DECLARE(alaw);
GST_PLUGIN_STATIC_DECLARE(app);
GST_PLUGIN_STATIC_DECLARE(audioconvert);
GST_PLUGIN_STATIC_DECLARE(audioresample);
GST_PLUGIN_STATIC_DECLARE(audiotestsrc);
GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(erdtls);
GST_PLUGIN_STATIC_DECLARE(mulaw);
GST_PLUGIN_STATIC_DECLARE(opengl);
GST_PLUGIN_STATIC_DECLARE(openh264);
GST_PLUGIN_STATIC_DECLARE(opus);
GST_PLUGIN_STATIC_DECLARE(rtp);
GST_PLUGIN_STATIC_DECLARE(rtpmanager);
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

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR)
GST_PLUGIN_STATIC_DECLARE(ercolorspace);
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
    __android_log_write(ANDROID_LOG_INFO, "g_log", message);
    g_log_default_handler(log_domain, log_level, message, unused_data);
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
 *
 * Initializes the OpenWebRTC library. Creates a new #GMainContext and starts
 * the main loop used in the OpenWebRTC library.  Either this function or
 * owr_init_with_main_context() must be called before doing anything else.
 */
void owr_init()
{
    gboolean owr_main_context_is_external;
    GstBus *bus;

    g_return_if_fail(!owr_initialized);

#ifdef __ANDROID__
    g_set_print_handler((GPrintFunc)g_print_android_handler);
    g_set_printerr_handler((GPrintFunc)g_printerr_android_handler);
    g_log_set_default_handler((GLogFunc)g_log_android_handler, NULL);
    gst_debug_add_log_function((GstLogFunction)gst_log_android_handler, NULL, NULL);
#endif

    /* Hack to make sure that all symbols that we need are included in the resulting library/binary */
    _owr_require_symbols();

    gst_init(NULL, NULL);
    owr_initialized = TRUE;

    GST_PLUGIN_STATIC_REGISTER(alaw);
    GST_PLUGIN_STATIC_REGISTER(app);
    GST_PLUGIN_STATIC_REGISTER(audioconvert);
    GST_PLUGIN_STATIC_REGISTER(audioresample);
    GST_PLUGIN_STATIC_REGISTER(audiotestsrc);
    GST_PLUGIN_STATIC_REGISTER(coreelements);
    GST_PLUGIN_STATIC_REGISTER(erdtls);
    GST_PLUGIN_STATIC_REGISTER(mulaw);
    GST_PLUGIN_STATIC_REGISTER(opengl);
    GST_PLUGIN_STATIC_REGISTER(openh264);
    GST_PLUGIN_STATIC_REGISTER(opus);
    GST_PLUGIN_STATIC_REGISTER(rtp);
    GST_PLUGIN_STATIC_REGISTER(rtpmanager);
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
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR)
    GST_PLUGIN_STATIC_REGISTER(ercolorspace);
#endif

    gst_element_register(NULL, "nicesrc", GST_RANK_NONE, GST_TYPE_NICE_SRC);
    gst_element_register(NULL, "nicesink", GST_RANK_NONE, GST_TYPE_NICE_SINK);

    owr_main_context_is_external = !!owr_main_context;

    if (!owr_main_context_is_external)
        owr_main_context = g_main_context_new();

    owr_pipeline = gst_pipeline_new("OpenWebRTC");

    bus = gst_pipeline_get_bus(GST_PIPELINE(owr_pipeline));
    g_main_context_push_thread_default(owr_main_context);
    bus_watch_id = gst_bus_add_watch(bus, (GstBusFunc)bus_call, NULL);
    g_main_context_pop_thread_default(owr_main_context);
    gst_object_unref(bus);

    gst_element_set_state(owr_pipeline, GST_STATE_PLAYING);
#ifdef OWR_DEBUG
    g_signal_connect(owr_pipeline, "deep-notify", G_CALLBACK(gst_object_default_deep_notify), NULL);
#endif

    if (owr_main_context_is_external)
        return;

    owr_main_loop = g_main_loop_new(owr_main_context, FALSE);
    owr_main_thread = g_thread_new("owr_main_loop", owr_run, NULL);
}


/**
 * owr_init_with_main_context:
 * @main_context: a #GMainContext to be used in OpenWebRTC.
 *
 * Initializes the OpenWebRTC library with the given #GMainContext. When this
 * function is used the application is responsible for iterating the
 * #GMainContext. Either This function or owr_init() must be called before
 * doing anything else.
 */
void owr_init_with_main_context(GMainContext *main_context)
{
    g_return_if_fail(!owr_initialized);
    g_return_if_fail(main_context);

    owr_main_context = main_context;
    owr_init();
}


static gpointer owr_run(gpointer data)
{
    g_return_val_if_fail(!data, NULL);
    g_main_context_push_thread_default(owr_main_context);
    g_main_loop_run(owr_main_loop);
    return NULL;
}

gboolean _owr_is_initialized()
{
    return owr_initialized;
}

GMainContext * _owr_get_main_context()
{
    return owr_main_context;
}

GMainLoop * _owr_get_main_loop()
{
    return owr_main_loop;
}


void _owr_schedule_with_hash_table(GSourceFunc func, GHashTable *hash_table)
{
    GSource *source = g_idle_source_new();

    g_source_set_callback(source, func, hash_table, NULL);
    g_source_attach(source, owr_main_context);
}

GstElement * _owr_get_pipeline()
{
    return owr_pipeline;
}

void owr_dump_dot_file(const gchar *base_filename)
{
    g_return_if_fail(owr_pipeline);
    g_return_if_fail(base_filename);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(owr_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, base_filename);
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer user_data)
{
    gboolean ret, is_warning = FALSE;
    GstStateChangeReturn change_status;
    gchar *message_type, *debug;
    GError *error;

    g_return_val_if_fail(GST_IS_BUS(bus), TRUE);

    (void)user_data;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_LATENCY:
        ret = gst_bin_recalculate_latency(GST_BIN(owr_pipeline));
        g_warn_if_fail(ret);
        break;

    case GST_MESSAGE_CLOCK_LOST:
        change_status = gst_element_set_state(GST_ELEMENT(owr_pipeline), GST_STATE_PAUSED);
        g_warn_if_fail(change_status != GST_STATE_CHANGE_FAILURE);
        change_status = gst_element_set_state(GST_ELEMENT(owr_pipeline), GST_STATE_PLAYING);
        g_warn_if_fail(change_status != GST_STATE_CHANGE_FAILURE);
        break;

    case GST_MESSAGE_EOS:
        g_print("End of stream\n");
        break;

    case GST_MESSAGE_WARNING:
        is_warning = TRUE;

    case GST_MESSAGE_ERROR:
        if (is_warning) {
            message_type = "Warning";
            gst_message_parse_warning(msg, &error, &debug);
        } else {
            message_type = "Error";
            gst_message_parse_error(msg, &error, &debug);
        }

        g_printerr("==== %s message start ====\n", message_type);
        g_printerr("%s in element %s.\n", message_type, GST_OBJECT_NAME(msg->src));
        g_printerr("%s: %s\n", message_type, error->message);
        g_printerr("Debugging info: %s\n", (debug) ? debug : "none");

        _owr_utils_print_bin(owr_pipeline, TRUE);
        g_printerr("==== %s message stop ====\n", message_type);
        /*GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(owr_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline.dot");*/

        g_error_free(error);
        g_free(debug);
        break;

    default:
        break;
    }

    return TRUE;
}
