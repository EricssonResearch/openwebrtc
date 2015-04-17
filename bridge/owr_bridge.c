/*
 * Copyright (C) 2014-2015 Ericsson AB. All rights reserved.
 * Copyright (c) 2014, Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_bridge.h"

#include "client/domutils.js.h"
#include "client/sdp.js.h"
#include "client/webrtc.js.h"
#include "owr.h"
#include "seed/websocket.js.h"
#include "seed/workerinit.js.h"
#include "seed/workerutils.js.h"
#include "shared/wbjsonrpc.js.h"
#include "worker/bridgeserver.js.h"
#include "worker/peerhandler.js.h"

#ifdef OWR_STATIC
#include OWR_GIR_FILE
#include <gir/GIRepository-2.0.gir.h>
#include <gir/GLib-2.0.gir.h>
#include <gir/GObject-2.0.gir.h>
#include <gir/Gio-2.0.gir.h>

#include <girepository.h>
#include <girparser.h>
#endif

#include <glib/gstdio.h>
#include <seed.h>

#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#ifdef OWR_STATIC
GLogLevelFlags logged_levels;
static void no_log_handler() { }

static gchar * get_cache_directory_name(const gchar *namespace)
{
#ifdef __ANDROID__
    gchar *cmdline, *cachedir;
    if (g_file_get_contents("/proc/self/cmdline", &cmdline, NULL, NULL)) {
        cachedir = g_build_filename("/data/data", cmdline, "cache", namespace, NULL);
        g_free(cmdline);
        return cachedir;
    }
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
    return g_build_filename(g_get_home_dir(), "Library", "Caches", namespace, NULL);
#else
    return g_build_filename(g_get_tmp_dir(), "typelib-cache", namespace, NULL);
#endif
}

static GITypelib ** get_typelibs_from_cache(gchar **namespaces, gchar **gir_checksums)
{
    GITypelib **typelibs = NULL;
    guint i;
    gchar *dirname, *filename, *filepath;
    guint array_length;
    gchar **typelib_data;
    gsize *typelib_lengths;

    array_length = g_strv_length(namespaces);
    g_return_val_if_fail(g_strv_length(gir_checksums) == array_length, NULL);

    typelib_data = g_new0(gchar *, array_length + 1);
    typelib_lengths = g_new0(gsize, array_length + 1);

    for (i = 0; i < array_length; i++) {
        dirname = get_cache_directory_name(namespaces[i]);
        filename = g_strdup_printf("%s.typelib", gir_checksums[i]);
        filepath = g_build_filename(dirname, filename, NULL);
        g_free(filename);
        g_free(dirname);

        if (!g_file_get_contents(filepath, &typelib_data[i], &typelib_lengths[i], NULL)) {
            g_free(filepath);
            goto error;
        }
        g_free(filepath);
    }

    typelibs = g_new0(GITypelib *, array_length + 1);

    for (i = 0; i < array_length; i++) {
        typelibs[i] = g_typelib_new_from_memory((guint8 *)typelib_data[i],
            typelib_lengths[i], NULL);
        if (!typelibs[i])
            goto error;
    }

    goto out;

error:
    for (i = 0; typelibs && typelibs[i]; i++)
        g_typelib_free(typelibs[i]);
    for (; typelib_data[i]; i++)
        g_free(typelib_data[i]);

    g_free(typelibs);
    typelibs = NULL;

out:
    g_free(typelib_data);
    g_free(typelib_lengths);

    return typelibs;
}

static GITypelib ** compile_and_cache_typelibs(gchar **namespaces, gchar **gir_checksums,
    gchar **girs, guint *gir_lengths)
{
    GITypelib **typelibs;
    GIrParser *parser;
    GIrModule *module;
    GError *error = NULL;
    guint i, array_length;
    gchar *dirname, *filename, *filepath, *filepath_old;
    GDir *dir;

    array_length = g_strv_length(namespaces);
    g_return_val_if_fail(g_strv_length(gir_checksums) == array_length, NULL);
    g_return_val_if_fail(g_strv_length(girs) == array_length, NULL);
    for (i = 0; gir_lengths[i]; i++) { }
    g_return_val_if_fail(i == array_length, NULL);

    typelibs = g_new0(GITypelib *, array_length + 1);
    parser = _g_ir_parser_new();

    for (i = 0; i < array_length; i++) {
        module = _g_ir_parser_parse_string(parser, namespaces[i], NULL,
            girs[i], gir_lengths[i], &error);
        if (!module) {
            g_warning("Failed to parse %s: %s", namespaces[i], error->message);
            g_error_free(error);
            break;
        }

        g_free(module->shared_library);
        module->shared_library = NULL;
        g_log_set_default_handler(no_log_handler, NULL);
        typelibs[i] = _g_ir_module_build_typelib(module);
        g_log_set_default_handler(g_log_default_handler, NULL);

        dirname = get_cache_directory_name(namespaces[i]);
        filename = g_strdup_printf("%s.typelib", gir_checksums[i]);
        filepath = g_build_filename(dirname, filename, NULL);
        g_free(filename);

        dir = g_dir_open(dirname, 0, NULL);
        if (dir) {
            while ((filename = (gchar *)g_dir_read_name(dir))) {
                filepath_old = g_build_filename(dirname, filename, NULL);
                g_message("Removing old file: %s", filepath_old);
                g_unlink(filepath_old);
                g_free(filepath_old);
            }
            g_dir_close(dir);
        }

        g_mkdir_with_parents(dirname, 0700);

        if (!g_file_set_contents(filepath, (gchar *)typelibs[i]->data, typelibs[i]->len, NULL))
            g_warning("Failed to cache typelib in %s", filepath);

        g_free(filepath);
        g_free(dirname);
    }
    if (i != array_length) {
        for (i = 0; typelibs[i]; i++)
            g_typelib_free(typelibs[i]);

        typelibs = NULL;
    }

    _g_ir_parser_free(parser);

    return typelibs;
}

static void load_typelibs()
{
    GITypelib **typelibs;
    const char *namespace;
    guint i;

    gchar *namespaces[] = {
        "GLib",
        "GObject",
        "Gio",
        "GIRepository",
        "Owr",
        NULL
    };

    guchar *gir_checksums[] = {
        GLib_2_0_gir_sha1,
        GObject_2_0_gir_sha1,
        Gio_2_0_gir_sha1,
        GIRepository_2_0_gir_sha1,
        G_PASTE(OWR_GIR_VAR_PREFIX, _gir_sha1),
        NULL
    };
    guint gir_checksum_lengths[G_N_ELEMENTS(gir_checksums)];

    guchar *girs[] = {
        GLib_2_0_gir,
        GObject_2_0_gir,
        Gio_2_0_gir,
        GIRepository_2_0_gir,
        G_PASTE(OWR_GIR_VAR_PREFIX, _gir),
        NULL
    };
    guint gir_lengths[G_N_ELEMENTS(girs)];

    gir_checksum_lengths[0] = GLib_2_0_gir_sha1_len;
    gir_checksum_lengths[1] = GObject_2_0_gir_sha1_len,
    gir_checksum_lengths[2] = Gio_2_0_gir_sha1_len,
    gir_checksum_lengths[3] = GIRepository_2_0_gir_sha1_len,
    gir_checksum_lengths[4] = G_PASTE(OWR_GIR_VAR_PREFIX, _gir_sha1_len),
    gir_checksum_lengths[5] = 0;

    gir_lengths[0] = GLib_2_0_gir_len;
    gir_lengths[1] = GObject_2_0_gir_len,
    gir_lengths[2] = Gio_2_0_gir_len,
    gir_lengths[3] = GIRepository_2_0_gir_len,
    gir_lengths[4] = G_PASTE(OWR_GIR_VAR_PREFIX, _gir_len),
    gir_lengths[5] = 0;

    for (i = 0; gir_checksums[i]; i++)
        gir_checksums[i] = (guchar *)g_strndup((gchar *)gir_checksums[i], gir_checksum_lengths[i]);

    typelibs = get_typelibs_from_cache(namespaces, (gchar **)gir_checksums);
    if (!typelibs) {
        typelibs = compile_and_cache_typelibs(namespaces, (gchar **)gir_checksums,
            (gchar **)girs, gir_lengths);
    }

    for (i = 0; typelibs && typelibs[i]; i++) {
        namespace = g_irepository_load_typelib(NULL, typelibs[i], 0, NULL);
        g_message("Loaded namespace: %s", namespace);
    }

    g_free(typelibs);

    for (i = 0; gir_checksums[i]; i++)
        g_free(gir_checksums[i]);
}
#endif

static SeedException evaluate_script(SeedContext context, gchar *script, guint script_len,
    gchar *script_name)
{
    SeedScript *seed_script;
    SeedException exception;
    gchar *script0 = script_len ? g_strndup(script, script_len) : g_strdup(script);
    seed_script = seed_make_script(context, script0, script_name, 0);
    seed_evaluate(context, seed_script, seed_context_get_global_object(context));
    exception = seed_script_exception(seed_script);
    seed_script_destroy(seed_script);
    g_free(script0);
    return exception;
}

static void print_exception(SeedContext context, SeedException exception)
{
    gchar *message;
    g_return_if_fail(exception);
    message = seed_exception_to_string(context, exception);
    g_message("Exception: %s", message);
    g_free(message);
}

static gboolean bridge_ready_callback(GAsyncQueue *msg_queue)
{
    g_return_val_if_fail(msg_queue, FALSE);
    g_async_queue_push(msg_queue, "ready");
    return FALSE;
}

static gpointer run(GAsyncQueue *msg_queue)
{
    SeedEngine *engine;
    SeedGlobalContext worker_context;
    SeedException exception;
    guint i;
    gint argc = 2;
    gchar *args[] = {"seed", "" /* "--seed-debug=initialization,finalization"*/};
    gchar **argv = args;

    gchar *script_names[] = {
        "workerutils.js",
        "websocket.js",
        "workerinit.js",
        NULL
    };
    guchar *scripts[] = {
        workerutils_js_,
        websocket_js_,
        workerinit_js_,
        NULL
    };
    guint script_lengths[G_N_ELEMENTS(scripts)];

    gchar *worker_script_names[] = {
        "wbjsonrpc.js",
        "peerhandler.js",
        "bridgeserver.js",
        NULL
    };
    guchar *worker_scripts[] = {
        wbjsonrpc_js_,
        peerhandler_js_,
        bridgeserver_js_,
        NULL
    };
    guint worker_script_lengths[G_N_ELEMENTS(worker_scripts)];

    gchar *client_script_names[] = {
        "wbjsonrpc_js",
        "domutils_js",
        "sdp_js",
        "webrtc_js",
        NULL
    };
    guchar *client_scripts[] = {
        wbjsonrpc_js_,
        domutils_js_,
        sdp_js_,
        webrtc_js_,
        NULL
    };
    guint client_script_lengths[G_N_ELEMENTS(client_scripts)];

    script_lengths[0] = workerutils_js__len;
    script_lengths[1] = websocket_js__len;
    script_lengths[2] = workerinit_js__len;
    script_lengths[3] = 0;

    worker_script_lengths[0] = wbjsonrpc_js__len;
    worker_script_lengths[1] = peerhandler_js__len;
    worker_script_lengths[2] = bridgeserver_js__len;
    worker_script_lengths[3] = 0;

    client_script_lengths[0] = wbjsonrpc_js__len;
    client_script_lengths[1] = domutils_js__len;
    client_script_lengths[2] = sdp_js__len;
    client_script_lengths[3] = webrtc_js__len;
    client_script_lengths[4] = 0;

#ifdef OWR_STATIC
    load_typelibs();
#endif

    owr_init(NULL);
    engine = seed_init(&argc, &argv);
    seed_engine_set_search_path(engine, "");
    worker_context = seed_context_create(engine->group, NULL);
    seed_object_set_property(engine->context, seed_context_get_global_object(engine->context),
        "worker_global",  seed_context_get_global_object(worker_context));

    for (i = 0; client_scripts[i]; i++) {
        seed_object_set_property(worker_context, seed_context_get_global_object(worker_context),
            client_script_names[i], seed_value_from_binary_string(worker_context,
            (gchar *)client_scripts[i], client_script_lengths[i], NULL));
    }

    evaluate_script(engine->context, "imports.searchPath = [\".\"];" \
        "imports.extensions = {\"GLib\": {}};", 0, "init script");

    for (i = 0; scripts[i]; i++) {
        exception = evaluate_script(engine->context, (gchar *)scripts[i], script_lengths[i],
            script_names[i]);
        if (exception)
            print_exception(engine->context, exception);
    }

    for (i = 0; worker_scripts[i]; i++) {
        exception = evaluate_script(worker_context, (gchar *)worker_scripts[i],
            worker_script_lengths[i], worker_script_names[i]);
        if (exception)
            print_exception(worker_context, exception);
    }

    if (msg_queue)
        g_idle_add((GSourceFunc)bridge_ready_callback, msg_queue);

    owr_run();

    seed_context_unref(worker_context);

    return NULL;
}

void owr_bridge_start()
{
    run(NULL);
}

void owr_bridge_start_in_thread()
{
    GAsyncQueue *msg_queue = g_async_queue_new();
    g_thread_new("owr-bridge-thread", (GThreadFunc)run, msg_queue);
    g_message("%s", (gchar *)g_async_queue_pop(msg_queue));
    g_async_queue_unref(msg_queue);
}
