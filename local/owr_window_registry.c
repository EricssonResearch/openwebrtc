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
\*\ OwrWindowRegistry
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_window_registry.h"

#include "owr_private.h"
#include "owr_message_origin.h"
#include "owr_video_renderer.h"
#include "owr_video_renderer_private.h"
#include "owr_window_registry_private.h"

GST_DEBUG_CATEGORY_EXTERN(_owrwindowregistry_debug);
#define GST_CAT_DEFAULT _owrwindowregistry_debug

#define OWR_WINDOW_REGISTRY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
    OWR_TYPE_WINDOW_REGISTRY, OwrWindowRegistryPrivate))

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface);

G_DEFINE_TYPE_WITH_CODE(OwrWindowRegistry, owr_window_registry, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(OWR_TYPE_MESSAGE_ORIGIN, owr_message_origin_interface_init));

static OwrWindowRegistry *owr_window_registry_instance = NULL;

G_LOCK_DEFINE_STATIC(owr_window_registry_mutex);

struct _OwrWindowRegistryPrivate {
    GHashTable *registry_hash_table;
    OwrMessageOriginBusSet *message_origin_bus_set;
};

typedef struct {
    guintptr window_handle;
    gboolean window_handle_set;

    OwrVideoRenderer *renderer; /* weak reference */
} WindowHandleData;

static void owr_window_registry_finalize(GObject *object)
{
    OwrWindowRegistry *window_registry = OWR_WINDOW_REGISTRY(object);

    g_warn_if_reached();

    G_LOCK(owr_window_registry_mutex);

    if (window_registry == owr_window_registry_instance)
        owr_window_registry_instance = NULL;

    G_UNLOCK(owr_window_registry_mutex);

    g_hash_table_unref(window_registry->priv->registry_hash_table);

    owr_message_origin_bus_set_free(window_registry->priv->message_origin_bus_set);
    window_registry->priv->message_origin_bus_set = NULL;

    G_OBJECT_CLASS(owr_window_registry_parent_class)->finalize(object);
}

static void owr_window_registry_class_init(OwrWindowRegistryClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrWindowRegistryPrivate));

    gobject_class->finalize = owr_window_registry_finalize;
}

static gpointer owr_window_registry_get_bus_set(OwrMessageOrigin *origin)
{
    return OWR_WINDOW_REGISTRY(origin)->priv->message_origin_bus_set;
}

static void owr_message_origin_interface_init(OwrMessageOriginInterface *interface)
{
    interface->get_bus_set = owr_window_registry_get_bus_set;
}


static void owr_window_registry_init(OwrWindowRegistry *window_registry)
{
    OwrWindowRegistryPrivate *priv = window_registry->priv =
        OWR_WINDOW_REGISTRY_GET_PRIVATE(window_registry);

    priv->registry_hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    priv->message_origin_bus_set = owr_message_origin_bus_set_new();
}

static gboolean do_register(GHashTable *args)
{
    OwrWindowRegistry *window_registry;
    gchar *tag;
    gpointer handle;
    OwrWindowRegistryPrivate *priv;
    WindowHandleData *data;

    g_return_val_if_fail(args, G_SOURCE_REMOVE);

    window_registry = g_hash_table_lookup(args, "window_registry");
    tag = g_hash_table_lookup(args, "tag");
    handle = g_hash_table_lookup(args, "handle");

    g_return_val_if_fail(OWR_IS_WINDOW_REGISTRY(window_registry), G_SOURCE_REMOVE);
    g_return_val_if_fail(tag, G_SOURCE_REMOVE);
    g_return_val_if_fail(handle, G_SOURCE_REMOVE);

    priv = window_registry->priv;

    data = g_hash_table_lookup(priv->registry_hash_table, tag);
    if (data) {
        if (data->window_handle_set) {
            g_warning("Tag '%s' has already been registered for another window handle", tag);
            goto end;
        }

        data->window_handle = (guintptr) handle;
        data->window_handle_set = TRUE;

        /* Notify the renderer */
        if (data->renderer)
            _owr_video_renderer_notify_tag_changed(data->renderer, tag, TRUE, (guintptr) handle);
    } else {
        data = g_new0(WindowHandleData, 1);
        data->window_handle = (guintptr) handle;
        data->window_handle_set = TRUE;
        data->renderer = NULL;

        g_hash_table_insert(priv->registry_hash_table, g_strdup(tag), data);
    }

end:
    g_object_unref(window_registry);
    g_free(tag);
    g_hash_table_unref(args);
    return G_SOURCE_REMOVE;
}

/**
 * owr_window_registry_register:
 * @registry:
 * @tag:
 * @handle: (transfer none)(type OwrWindowHandle):
 */
void owr_window_registry_register(OwrWindowRegistry *window_registry,
    const gchar *tag, gpointer handle)
{
    GHashTable *args;

    g_return_if_fail(OWR_IS_WINDOW_REGISTRY(window_registry));
    g_return_if_fail(tag);
    g_return_if_fail(handle);

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(window_registry));
    g_hash_table_insert(args, "window_registry", window_registry);
    g_hash_table_insert(args, "tag", g_strdup(tag));
    g_hash_table_insert(args, "handle", handle);

    g_object_ref(window_registry);

    _owr_schedule_with_hash_table((GSourceFunc)do_register, args);
}

static gboolean do_unregister(GHashTable *args)
{
    OwrWindowRegistry *window_registry;
    gchar *tag;
    OwrWindowRegistryPrivate *priv;
    WindowHandleData *data;

    g_return_val_if_fail(args, G_SOURCE_REMOVE);

    window_registry = g_hash_table_lookup(args, "window_registry");
    tag = g_hash_table_lookup(args, "tag");

    g_return_val_if_fail(OWR_IS_WINDOW_REGISTRY(window_registry), G_SOURCE_REMOVE);
    g_return_val_if_fail(tag, G_SOURCE_REMOVE);

    priv = window_registry->priv;

    data = g_hash_table_lookup(priv->registry_hash_table, tag);
    g_return_val_if_fail(data, G_SOURCE_REMOVE);

    if (data->renderer) {
        data->window_handle = (guintptr) NULL;
        data->window_handle_set = FALSE;

        _owr_video_renderer_notify_tag_changed(data->renderer, tag, FALSE, 0);
    } else
        g_hash_table_remove(priv->registry_hash_table, tag);

    g_object_unref(window_registry);
    g_free(tag);
    g_hash_table_unref(args);
    return G_SOURCE_REMOVE;
}

void owr_window_registry_unregister(OwrWindowRegistry *window_registry,
    const gchar *tag)
{
    GHashTable *args;

    g_return_if_fail(OWR_IS_WINDOW_REGISTRY(window_registry));
    g_return_if_fail(tag);

    args = _owr_create_schedule_table(OWR_MESSAGE_ORIGIN(window_registry));
    g_hash_table_insert(args, "window_registry", window_registry);
    g_hash_table_insert(args, "tag", g_strdup(tag));

    g_object_ref(window_registry);

    _owr_schedule_with_hash_table((GSourceFunc)do_unregister, args);
}

void _owr_window_registry_register_renderer(OwrWindowRegistry *window_registry,
    const gchar *tag, OwrVideoRenderer *video_renderer)
{
    WindowHandleData *data;
    OwrWindowRegistryPrivate *priv;

    g_return_if_fail(OWR_IS_WINDOW_REGISTRY(window_registry));
    g_return_if_fail(OWR_IS_VIDEO_RENDERER(video_renderer));

    priv = window_registry->priv;

    data = g_hash_table_lookup(priv->registry_hash_table, tag);
    if (data) {
        g_return_if_fail(!data->renderer);

        data->renderer = video_renderer;

        /* Notify the renderer */
        if (data->window_handle_set)
            _owr_video_renderer_notify_tag_changed(video_renderer, tag, TRUE, data->window_handle);
    } else {
        data = g_new0(WindowHandleData, 1);
        data->window_handle = 0;
        data->window_handle_set = FALSE;
        data->renderer = video_renderer;

        g_hash_table_insert(priv->registry_hash_table, g_strdup(tag), data);
    }
}

void _owr_window_registry_unregister_renderer(OwrWindowRegistry *window_registry,
    const gchar *tag, OwrVideoRenderer *video_renderer)
{
    OwrWindowRegistryPrivate *priv;
    WindowHandleData *data;

    g_return_if_fail(OWR_IS_WINDOW_REGISTRY(window_registry));
    g_return_if_fail(OWR_IS_VIDEO_RENDERER(video_renderer));

    priv = window_registry->priv;

    data = g_hash_table_lookup(priv->registry_hash_table, tag);
    g_return_if_fail(data);
    g_return_if_fail(data->renderer == video_renderer);

    data->renderer = NULL;
    if (!data->window_handle_set)
        g_hash_table_remove(priv->registry_hash_table, tag);
}

guintptr _owr_window_registry_lookup(OwrWindowRegistry *window_registry,
    const gchar *tag) {
    g_return_val_if_fail(OWR_IS_WINDOW_REGISTRY(window_registry), 0);

    return (guintptr)g_hash_table_lookup(window_registry->priv->registry_hash_table, tag);
}

/**
 * owr_window_registry_get:
 *
 * Returns: (transfer none):
 */
OwrWindowRegistry *owr_window_registry_get(void)
{
    G_LOCK(owr_window_registry_mutex);

    if (!owr_window_registry_instance)
        owr_window_registry_instance = g_object_new(OWR_TYPE_WINDOW_REGISTRY, NULL);

    G_UNLOCK(owr_window_registry_mutex);

    return owr_window_registry_instance;
}
