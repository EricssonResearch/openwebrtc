/*
 * Copyright (C) 2014-2015 Ericsson AB. All rights reserved.
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
#include "test_utils.h"

void write_dot_file(const gchar *base_file_name, gchar *dot_data, gboolean with_timestamp)
{
    GTimeVal time;
    const gchar *path;
    gchar *filename, *timestamp = NULL;
    gboolean success;

    g_return_if_fail(base_file_name);
    g_return_if_fail(dot_data);

    path = g_getenv("OWR_DEBUG_DUMP_DOT_DIR");
    if (!path)
        return;

    if (with_timestamp) {
        g_get_current_time(&time);
        timestamp = g_time_val_to_iso8601(&time);
    }

    filename = g_strdup_printf("%s/%s%s%s.dot", path[0] ? path : ".",
        timestamp ? timestamp : "", timestamp ? "-" : "", base_file_name);
    success = g_file_set_contents(filename, dot_data, -1, NULL);
    g_warn_if_fail(success);

    g_free(dot_data);
    g_free(filename);
    g_free(timestamp);
}


static const gchar *sub_type_name(OwrMessageSubType sub_type)
{
    static GEnumClass *enum_class = NULL;

    if (g_once_init_enter(&enum_class)) {
        GType type = owr_message_sub_type_get_type();
        g_once_init_leave(&enum_class, g_type_class_ref(type));
    }
    return g_enum_get_value(enum_class, sub_type)->value_nick;
}

static void data_to_string_foreach(gchar *key, GValue *value, GString *string)
{
    gchar *value_string;

    value_string = g_strdup_value_contents(value);
    g_string_append_printf(string, ", %s=%s", key, value_string);
    g_free(value_string);
}

static gchar *data_to_string(GHashTable *data)
{
    GString *string;

    if (!data)
        return g_strdup("");

    string = g_string_new(NULL);
    g_hash_table_foreach(data, (GHFunc) data_to_string_foreach, string);

    return g_string_free(string, FALSE);
}

static void handle_error(gchar *origin, OwrMessageSubType sub_type, gchar *data)
{
    g_print(" *** ERROR *** \n");
    g_print("origin=%s, type=%s%s\n", origin, sub_type_name(sub_type), data);
}

static void handle_stats(gchar *origin, OwrMessageSubType sub_type, gchar *data)
{
    g_print("STATS: origin=%s, type=%s%s\n", origin, sub_type_name(sub_type), data);
}

static void handle_event(gchar *origin, OwrMessageSubType sub_type, gchar *data)
{
    g_print("EVENT: origin=%s, type=%s%s\n", origin, sub_type_name(sub_type), data);
}

void bus_message_print_callback(OwrMessageOrigin *origin,
    OwrMessageType type, OwrMessageSubType sub_type, GHashTable *data,
    const gchar *(*origin_name_func)(gpointer))
{
    gchar *data_string;
    gchar *origin_name = NULL;

    if (origin_name_func) {
        origin_name = g_strdup(origin_name_func(origin));
    }
    if (!origin_name) {
        origin_name = g_strdup_printf("%s<%p>", G_OBJECT_CLASS_NAME(origin), origin);
    }

    data_string = data_to_string(data);
    switch (type) {
        case OWR_MESSAGE_TYPE_ERROR:
            handle_error(origin_name, sub_type, data_string);
            break;
        case OWR_MESSAGE_TYPE_STATS:
            handle_stats(origin_name, sub_type, data_string);
            break;
        case OWR_MESSAGE_TYPE_EVENT:
            handle_event(origin_name, sub_type, data_string);
            break;
    }
    g_free(origin_name);
    g_free(data_string);
}
