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
\*\ OwrWindowRegistry
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "owr_window_registry.h"

#define OWR_WINDOW_REGISTRY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
    OWR_TYPE_WINDOW_REGISTRY, OwrWindowRegistryPrivate))

G_DEFINE_TYPE(OwrWindowRegistry, owr_window_registry, G_TYPE_OBJECT);

static OwrWindowRegistry *owr_window_registry_instance = NULL;

G_LOCK_DEFINE_STATIC(owr_window_registry_mutex);

struct _OwrWindowRegistryPrivate
{
    GHashTable *registry_hash_table;
};

static void owr_window_registry_finalize(GObject *object)
{
    OwrWindowRegistry *window_registry = OWR_WINDOW_REGISTRY(object);

    G_LOCK(owr_window_registry_mutex);

    if (window_registry == owr_window_registry_instance)
        owr_window_registry_instance = NULL;

    G_UNLOCK(owr_window_registry_mutex);

    g_hash_table_unref(window_registry->priv->registry_hash_table);

    G_OBJECT_CLASS(owr_window_registry_parent_class)->finalize(object);
}

static void owr_window_registry_class_init(OwrWindowRegistryClass *klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrWindowRegistryPrivate));

    gobject_class->finalize = owr_window_registry_finalize;
}

static void owr_window_registry_init(OwrWindowRegistry *window_registry)
{
    OwrWindowRegistryPrivate *priv = window_registry->priv =
        OWR_WINDOW_REGISTRY_GET_PRIVATE(window_registry);

    priv->registry_hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

void owr_window_registry_register(OwrWindowRegistry *window_registry,
    const gchar *tag, gpointer handle)
{
    OwrWindowRegistryPrivate *priv;

    g_return_if_fail(OWR_IS_WINDOW_REGISTRY(window_registry));
    g_return_if_fail(handle);

    priv = window_registry->priv;

    g_return_if_fail(!g_hash_table_contains(priv->registry_hash_table, tag));
    g_hash_table_insert(priv->registry_hash_table, g_strdup(tag), handle);
}

void owr_window_registry_unregister(OwrWindowRegistry *window_registry,
    const gchar *tag)
{
    gboolean found;
    g_return_if_fail(OWR_IS_WINDOW_REGISTRY(window_registry));

    found = g_hash_table_remove(window_registry->priv->registry_hash_table, tag);
    g_warn_if_fail(found);
}

guintptr _owr_window_registry_lookup(OwrWindowRegistry *window_registry,
    const gchar *tag)
{
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

    if (!owr_window_registry_instance) {
        owr_window_registry_instance = g_object_new(OWR_TYPE_WINDOW_REGISTRY, NULL);
    }

    G_UNLOCK(owr_window_registry_mutex);

    return owr_window_registry_instance;
}
