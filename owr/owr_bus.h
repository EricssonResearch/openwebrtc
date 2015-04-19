/*
 * Copyright (c) 2015, Ericsson AB. All rights reserved.
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
\*\ OwrBus
/*/

#ifndef __OWR_BUS_H__
#define __OWR_BUS_H__

#include "owr_message_origin.h"
#include "owr_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
    OWR_MESSAGE_TYPE_ERROR = (1 << 0),
    OWR_MESSAGE_TYPE_STATS = (1 << 1),
    OWR_MESSAGE_TYPE_EVENT = (1 << 2)
} OwrMessageType;

typedef void (*OwrBusMessageCallback) (OwrMessageType type, OwrMessageOrigin *origin, gchar *message, gpointer user_data);

#define OWR_TYPE_MESSAGE_TYPE (owr_message_type_get_type())
GType owr_message_type_get_type(void);

#define OWR_TYPE_BUS            (owr_bus_get_type())
#define OWR_BUS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_BUS, OwrBus))
#define OWR_BUS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_BUS, OwrBusClass))
#define OWR_IS_BUS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_BUS))
#define OWR_IS_BUS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_BUS))
#define OWR_BUS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_BUS, OwrBusClass))

typedef struct _OwrBus        OwrBus;
typedef struct _OwrBusClass   OwrBusClass;
typedef struct _OwrBusPrivate OwrBusPrivate;

struct _OwrBus {
    GObject parent_instance;

    /*< private >*/
    OwrBusPrivate *priv;
};

struct _OwrBusClass {
    GObjectClass parent_class;

    /*< private >*/
};

GType owr_bus_get_type(void) G_GNUC_CONST;

OwrBus *owr_bus_new();
void owr_bus_set_message_callback(OwrBus *bus, OwrBusMessageCallback callback,
    gpointer user_data, GDestroyNotify destroy_data);
void owr_bus_add_message_origin(OwrBus *bus, OwrMessageOrigin *origin);
void owr_bus_remove_message_origin(OwrBus *bus, OwrMessageOrigin *origin);

G_END_DECLS

#endif /* __OWR_BUS_H__ */
