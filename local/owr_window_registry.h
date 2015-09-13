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

#ifndef _OWR_WINDOW_REGISTRY_H_
#define _OWR_WINDOW_REGISTRY_H_

#include "owr_local.h"

G_BEGIN_DECLS

/**
 * OwrWindowHandle:
 *
 * An opaque pointer to a native window
 */
typedef gpointer OwrWindowHandle;

#define OWR_TYPE_WINDOW_REGISTRY             (owr_window_registry_get_type())
#define OWR_WINDOW_REGISTRY(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_WINDOW_REGISTRY, OwrWindowRegistry))
#define OWR_WINDOW_REGISTRY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_WINDOW_REGISTRY, OwrWindowRegistryClass))
#define OWR_IS_WINDOW_REGISTRY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_WINDOW_REGISTRY))
#define OWR_IS_WINDOW_REGISTRY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_WINDOW_REGISTRY))
#define OWR_WINDOW_REGISTRY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_WINDOW_REGISTRY, OwrWindowRegistryClass))

typedef struct _OwrWindowRegistry        OwrWindowRegistry;
typedef struct _OwrWindowRegistryClass   OwrWindowRegistryClass;
typedef struct _OwrWindowRegistryPrivate OwrWindowRegistryPrivate;

struct _OwrWindowRegistry {
    GObject parent_instance;

    /*< private >*/
    OwrWindowRegistryPrivate *priv;
};

struct _OwrWindowRegistryClass {
    GObjectClass parent_class;
};

GType owr_window_registry_get_type(void) G_GNUC_CONST;

OwrWindowRegistry *owr_window_registry_get(void);

void owr_window_registry_register(OwrWindowRegistry *registry, const gchar *tag, gpointer handle);
void owr_window_registry_unregister(OwrWindowRegistry *registry, const gchar *tag);

G_END_DECLS

#endif /* _OWR_WINDOW_REGISTRY_H_ */
