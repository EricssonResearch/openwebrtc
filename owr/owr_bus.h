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

/**
 * OwrMessageSubType:
 *
 * @OWR_ERROR_TYPE_PROCESSING_ERROR: a processing error occured, the origin of the error
 * will likely not be able to produce or consume anymore data.
 *
 * @OWR_STATS_TYPE_SCHEDULE: information about a call that was scheduled on the main thread
 * - @function_name: #utf8 the name of the function that scheduled the call
 * - @start_time: #gint64 monotonic time when the call was scheduled
 * - @call_time: #gint64 monotonic time when the function call started
 * - @end_time: #gint64 monotonic time when the function call completed
 *
 * @OWR_STATS_TYPE_SEND_PIPELINE_ADDED: a send pipeline was added to a media session
 * - @start_time: #gint64 monotonic time when the pipeline setup began
 * - @end_time: #gint64 monotonic time when the pipeline setup was completed
 *
 * @OWR_STATS_TYPE_SEND_PIPELINE_REMOVED: a send pipeline was removed to a media session
 * - @start_time: #gint64 monotonic time when the pipeline teardown began
 * - @end_time: #gint64 monotonic time when the pipeline teardown was completed
 *
 * @OWR_EVENT_TYPE_RENDERER_STARTED: a renderer was started
 *
 * @OWR_EVENT_TYPE_RENDERER_STOPPED: a renderer was stopped
 *
 * @OWR_EVENT_TYPE_LOCAL_SOURCE_STARTED: a local media source was started
 * - @start_time: #gint64 monotonic time when the pipeline setup began
 * - @end_time: #gint64 monotonic time when the pipeline setup was completed
 *
 * @OWR_EVENT_TYPE_LOCAL_SOURCE_STOPPED: a local media source was stopped
 * - @start_time: #gint64 monotonic time when the pipeline teardown began
 * - @end_time: #gint64 monotonic time when the pipeline teardown was completed
 */
typedef enum {
    OWR_ERROR_TYPE_TEST = 0x1000,
    OWR_ERROR_TYPE_PROCESSING_ERROR,
    OWR_STATS_TYPE_TEST = 0x2000,
    OWR_STATS_TYPE_SCHEDULE,
    OWR_STATS_TYPE_SEND_PIPELINE_ADDED,
    OWR_STATS_TYPE_SEND_PIPELINE_REMOVED,
    OWR_EVENT_TYPE_TEST = 0x3000,
    OWR_EVENT_TYPE_RENDERER_STARTED,
    OWR_EVENT_TYPE_RENDERER_STOPPED,
    OWR_EVENT_TYPE_LOCAL_SOURCE_STARTED,
    OWR_EVENT_TYPE_LOCAL_SOURCE_STOPPED,
} OwrMessageSubType;

typedef void (*OwrBusMessageCallback) (OwrMessageOrigin *origin, OwrMessageType type, OwrMessageSubType sub_type, GHashTable *data, gpointer user_data);

#define OWR_TYPE_MESSAGE_TYPE (owr_message_type_get_type())
GType owr_message_type_get_type(void);

#define OWR_TYPE_MESSAGE_SUB_TYPE (owr_message_sub_type_get_type())
GType owr_message_sub_type_get_type(void);

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
