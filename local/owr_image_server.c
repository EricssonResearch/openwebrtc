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
\*\ OwrImageServer
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_image_server.h"

#include "owr_image_renderer_private.h"
#include "owr_utils.h"

#include <gio/gio.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN(_owrimageserver_debug);
#define GST_CAT_DEFAULT _owrimageserver_debug

#define DEFAULT_PORT 3325
#define DEFAULT_ALLOW_ORIGIN "null"

#define OWR_IMAGE_SERVER_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_IMAGE_SERVER, OwrImageServerPrivate))

G_DEFINE_TYPE(OwrImageServer, owr_image_server, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_PORT,
    PROP_ALLOW_ORIGIN,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

static void owr_image_server_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void owr_image_server_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static gboolean on_incoming_connection(GThreadedSocketService *service,
    GSocketConnection *connection, GObject *source_object, OwrImageServer *image_server);

struct _OwrImageServerPrivate {
    guint port;
    gchar *allow_origin;

    GHashTable *image_renderers;
    GMutex image_renderers_mutex;

    GSocketService *socket_service;
    gboolean socket_service_is_started;
};

static void owr_image_server_finalize(GObject *object)
{

    OwrImageServer *renderer = OWR_IMAGE_SERVER(object);
    OwrImageServerPrivate *priv = renderer->priv;

    g_socket_service_stop(priv->socket_service);
    g_object_unref(priv->socket_service);

    g_mutex_lock(&priv->image_renderers_mutex);
    g_hash_table_destroy(priv->image_renderers);
    g_mutex_unlock(&priv->image_renderers_mutex);
    g_mutex_clear(&priv->image_renderers_mutex);

    g_free(priv->allow_origin);

    G_OBJECT_CLASS(owr_image_server_parent_class)->finalize(object);
}

static void owr_image_server_class_init(OwrImageServerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrImageServerPrivate));

    obj_properties[PROP_PORT] = g_param_spec_uint("port", "Port",
        "The port to listen on for incoming connections",
        0, 65535, DEFAULT_PORT,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_ALLOW_ORIGIN] = g_param_spec_string("allow-origin", "Allow origin",
        "Space-separated list of origins allowed for cross-origin resource sharing"
        " (alternatively, \"null\" for none or \"*\" for all)",
        DEFAULT_ALLOW_ORIGIN,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = owr_image_server_set_property;
    gobject_class->get_property = owr_image_server_get_property;

    gobject_class->finalize = owr_image_server_finalize;

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_image_server_init(OwrImageServer *image_server)
{
    OwrImageServerPrivate *priv;
    image_server->priv = priv = OWR_IMAGE_SERVER_GET_PRIVATE(image_server);

    priv->port = DEFAULT_PORT;
    priv->allow_origin = g_strdup(DEFAULT_ALLOW_ORIGIN);

    priv->image_renderers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    g_mutex_init(&priv->image_renderers_mutex);

    priv->socket_service = g_threaded_socket_service_new(8);
    priv->socket_service_is_started = FALSE;
    g_signal_connect(priv->socket_service, "run", G_CALLBACK(on_incoming_connection), image_server);
}

static void owr_image_server_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrImageServerPrivate *priv;

    g_return_if_fail(OWR_IS_IMAGE_SERVER(object));
    priv = OWR_IMAGE_SERVER(object)->priv;

    switch (property_id) {
    case PROP_PORT:
        priv->port = g_value_get_uint(value);
        break;

    case PROP_ALLOW_ORIGIN:
        g_free(priv->allow_origin);
        priv->allow_origin = g_value_dup_string(value);
        g_strdelimit(priv->allow_origin, "\r\n", ' ');
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_image_server_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrImageServerPrivate *priv;

    g_return_if_fail(OWR_IS_IMAGE_SERVER(object));
    priv = OWR_IMAGE_SERVER(object)->priv;

    switch (property_id) {
    case PROP_PORT:
        g_value_set_uint(value, priv->port);
        break;

    case PROP_ALLOW_ORIGIN:
        g_value_set_string(value, priv->allow_origin);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}


/**
 * owr_image_server_new: (constructor)
 * @port: The port to listen on for incoming connections
 *
 * Returns: The new #OwrImageServer
 */
OwrImageServer *owr_image_server_new(guint port)
{
    return g_object_new(OWR_TYPE_IMAGE_SERVER, "port", port, NULL);
}

/**
 * owr_image_server_add_image_renderer:
 * @image_server:
 * @image_renderer: (transfer full):
 * @tag:
 *
 */
void owr_image_server_add_image_renderer(OwrImageServer *image_server,
    OwrImageRenderer *image_renderer, const gchar *tag)
{
    OwrImageServerPrivate *priv;

    g_return_if_fail(OWR_IS_IMAGE_SERVER(image_server));
    g_return_if_fail(OWR_IS_IMAGE_RENDERER(image_renderer));
    g_return_if_fail(tag && tag[0]);

    priv = image_server->priv;

    g_mutex_lock(&priv->image_renderers_mutex);

    if (!g_hash_table_contains(priv->image_renderers, tag))
        g_hash_table_insert(priv->image_renderers, g_strdup(tag), image_renderer);
    else {
        g_object_unref(image_renderer);
        g_warning("Image renderer not added, an image renderer is already added for this tag");
    }

    g_mutex_unlock(&priv->image_renderers_mutex);

    if (!priv->socket_service_is_started) {
        g_socket_listener_add_address(G_SOCKET_LISTENER(priv->socket_service),
            g_inet_socket_address_new(g_inet_address_new_from_string("127.0.0.1"),
            (guint16)priv->port), G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP,
            NULL, NULL, NULL);
        g_socket_service_start(priv->socket_service);
        priv->socket_service_is_started = TRUE;
    }
}

void owr_image_server_remove_image_renderer(OwrImageServer *image_server, const gchar *tag)
{
    OwrImageServerPrivate *priv;

    g_return_if_fail(OWR_IS_IMAGE_SERVER(image_server));

    priv = image_server->priv;

    g_mutex_lock(&priv->image_renderers_mutex);

    if (!g_hash_table_remove(priv->image_renderers, tag))
        g_warning("Image renderer not removed, no image renderer exists with this tag");

    g_mutex_unlock(&priv->image_renderers_mutex);
}

#define HTTP_RESPONSE_HEADER_TEMPLATE \
"HTTP/1.1 %d %s\r\n" \
"Content-Type: %s\r\n" \
"Content-Length: %u\r\n" \
"Cache-Control: no-cache,no-store\r\n" \
"Pragma: no-cache\r\n" \
"Access-Control-Allow-Origin: %s\r\n" \
"\r\n"

static gboolean on_incoming_connection(GThreadedSocketService *service,
    GSocketConnection *connection, GObject *source_object, OwrImageServer *image_server)
{
    GOutputStream *bos;
    GDataInputStream *dis;
    gchar *error_body, *error_header = NULL, *response_header = NULL;
    gchar *line, *tag;
    gsize line_length, i;
    guint content_length = 0;
    OwrImageRenderer *image_renderer;
    GBytes *image;
    gconstpointer image_data;
    gsize image_data_size = 0;

    OWR_UNUSED(service);
    OWR_UNUSED(source_object);

    g_return_val_if_fail(OWR_IS_IMAGE_SERVER(image_server), TRUE);

    bos = g_buffered_output_stream_new(g_io_stream_get_output_stream(G_IO_STREAM(connection)));
    dis = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(connection)));
    g_data_input_stream_set_newline_type(dis, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

    error_body = "404 Not Found";
    error_header = g_strdup_printf(HTTP_RESPONSE_HEADER_TEMPLATE, 404, "Not Found",
        "text/plain", (guint)strlen(error_body), "*");

    while (TRUE) {
        line = g_data_input_stream_read_line(dis, &line_length, NULL, NULL);
        if (!line)
            break;

        if (line_length > 6) {
            tag = g_strdup(line + 7);
            for (i = 0; i < strlen(tag); i++) {
                if (tag[i] == '-') {
                    tag[i] = '\0';
                    break;
                }
            }
        } else
            tag = NULL;

        g_free(line);

        while ((line = g_data_input_stream_read_line(dis, &line_length, NULL, NULL))) {
            g_free(line);

            if (!line_length) {
                /* got all request headers */
                break;
            }
        }

        if (!line)
            break;

        g_mutex_lock(&image_server->priv->image_renderers_mutex);
        image_renderer = tag ? g_hash_table_lookup(image_server->priv->image_renderers, tag) : NULL;
        if (image_renderer)
            g_object_ref(image_renderer);
        g_mutex_unlock(&image_server->priv->image_renderers_mutex);

        image = image_renderer ? _owr_image_renderer_pull_bmp_image(image_renderer) : NULL;

        if (image_renderer)
            g_object_unref(image_renderer);

        if (!image) {
            g_output_stream_write(bos, error_header, strlen(error_header), NULL, NULL);
            g_output_stream_write(bos, error_body, strlen(error_body), NULL, NULL);
            break;
        }

        image_data = g_bytes_get_data(image, &image_data_size);

        if (content_length != image_data_size) {
            content_length = image_data_size;
            g_free(response_header);
            response_header = g_strdup_printf(HTTP_RESPONSE_HEADER_TEMPLATE, 200, "OK",
                "image/bmp", content_length, image_server->priv->allow_origin);
            g_buffered_output_stream_set_buffer_size(G_BUFFERED_OUTPUT_STREAM(bos),
                strlen(response_header) + content_length);
        }
        g_output_stream_write(bos, response_header, strlen(response_header), NULL, NULL);
        g_output_stream_write(bos, image_data, image_data_size, NULL, NULL);
        g_output_stream_flush(bos, NULL, NULL);

        g_bytes_unref(image);
    }

    g_free(response_header);
    g_free(error_header);
    g_object_unref(dis);
    g_object_unref(bos);

    return FALSE;
}
