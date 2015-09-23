/*
 * Copyright (c) 2014-2015, Ericsson AB. All rights reserved.
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

#include "owr.h"

#include <stdlib.h>

static GCond timeout_thread_cond;
static GMutex timeout_thread_mutex;
static gboolean done = FALSE;
static gchar *expected_log_string;

void expect_assert(const gchar *function, const gchar* assertion)
{
    const gchar *prg_name = g_get_prgname();

    if (!prg_name)
        prg_name = "process";

    g_free(expected_log_string);
    expected_log_string = g_strdup_printf("%s: assertion '%s' failed", function, assertion);
}

gpointer quit_mainloop_thread_func(gpointer ms)
{
    g_usleep(1000 * GPOINTER_TO_UINT(ms));
    g_print("calling owr_quit\n");
    owr_quit();
    return NULL;
}

void quit_mainloop_after(guint ms)
{
    /* if owr_quit is called from a timeout on the main thread, we will sometimes get a segfault.
        It only seems to happen when the main loop has already been quit once, and possibly only on OS X */
    GThread *thread = g_thread_new("mainloop-quitter", quit_mainloop_thread_func, GUINT_TO_POINTER(ms));
    g_thread_unref(thread);
}

gboolean assert_not_runnable(gpointer user_data)
{
    (void) user_data;
    expect_assert("owr_run", "!owr_main_loop");
    owr_run();
    return G_SOURCE_REMOVE;
}

gboolean assert_not_runnable_in_background(gpointer user_data)
{
    (void) user_data;
    expect_assert("owr_run_in_background", "!owr_main_loop");
    owr_run_in_background();
    return G_SOURCE_REMOVE;
}

gpointer timeout_thread_func(GAsyncQueue *msg_queue)
{
    gint64 end_time;

    g_mutex_lock(&timeout_thread_mutex);

    g_async_queue_push(msg_queue, "ready");

    end_time = g_get_monotonic_time() + G_TIME_SPAN_SECOND * 2;

    while (!done) {
        if (!g_cond_wait_until(&timeout_thread_cond, &timeout_thread_mutex, end_time)) {
            g_mutex_unlock(&timeout_thread_mutex);
            g_print("test failed, timeout reached");
            exit(-1);
            return NULL;
        }
    }

    done = FALSE;
    g_mutex_unlock(&timeout_thread_mutex);

    return NULL;
}

void start_timeout_thread()
{
    GThread *timeout_thread;
    GAsyncQueue *msg_queue = g_async_queue_new();
    timeout_thread = g_thread_new("owr-test-timeout-thread", (GThreadFunc) timeout_thread_func, msg_queue);
    g_thread_unref(timeout_thread);
    g_async_queue_pop(msg_queue);
    g_async_queue_unref(msg_queue);
}

void stop_timeout_thread(void)
{
    g_mutex_lock(&timeout_thread_mutex);
    done = TRUE;
    g_cond_signal(&timeout_thread_cond);
    g_mutex_unlock(&timeout_thread_mutex);
}

void log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data)
{
    (void) log_domain;
    (void) log_level;
    (void) user_data;

    if (g_strcmp0(message, expected_log_string)) {
        g_print("** ERROR ** log message assertion failed:\n");
        g_print("expected: [%s]\n", expected_log_string ? expected_log_string : "null");
        g_print("but got : [%s]\n", message ? message : "null");
        exit(-1);
    }
}

int main()
{
    g_log_set_handler(NULL, G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_FATAL, log_handler, NULL);
    g_print("first we make sure that run and quit doesn't work before owr_init");

    expect_assert("owr_run_in_background", "owr_main_context");
    owr_run_in_background();

    expect_assert("owr_run", "owr_main_context");
    owr_run();

    expect_assert("owr_quit", "owr_main_loop");
    owr_quit();

    g_print("calling owr_init\n");
    owr_init(NULL);

    start_timeout_thread();
    g_print("running mainloop for 100ms\n");
    quit_mainloop_after(100);
    owr_run();
    g_print("mainloop quit successfully\n");
    stop_timeout_thread();

    start_timeout_thread();
    g_print("running mainloop in background for 100ms\n");
    quit_mainloop_after(100);
    owr_run_in_background();

    expect_assert("owr_run", "!owr_main_loop");
    owr_run();

    g_usleep(G_USEC_PER_SEC / 5);
    g_print("waited 200ms, background thread should now have quit\n");
    stop_timeout_thread();

    start_timeout_thread();
    g_print("it should now be possible to run owr_run again\n");
    g_print("running mainloop for 100ms\n");
    quit_mainloop_after(100);
    g_timeout_add(30, (GSourceFunc) assert_not_runnable, NULL);
    g_timeout_add(60, (GSourceFunc) assert_not_runnable_in_background, NULL);
    owr_run();
    g_print("mainloop quit successfully\n");
    stop_timeout_thread();

    start_timeout_thread();
    g_print("and owr_run again\n");
    quit_mainloop_after(10);
    owr_run();
    g_print("and owr_run again...\n");
    quit_mainloop_after(10);
    owr_run();
    g_print("and owr_run again...\n");
    quit_mainloop_after(10);
    owr_run();
    g_print("and owr_run again...\n");
    quit_mainloop_after(10);
    owr_run();
    g_print("and owr_run again...\n");
    quit_mainloop_after(10);
    owr_run();
    stop_timeout_thread();

    g_print("\n *** Test successful! *** \n\n");

    return 0;
}
