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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_bus.h"

#include "owr_bus_private.h"
#include "owr_message_origin_private.h"

#include "owr_utils.h"

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN(_owrbus_debug);
#define GST_CAT_DEFAULT _owrbus_debug

#define DEFAULT_MESSAGE_TYPE_MASK (OWR_MESSAGE_TYPE_ERROR | OWR_MESSAGE_TYPE_STATS | OWR_MESSAGE_TYPE_EVENT)

enum {
    PROP_0,
    PROP_MESSAGE_TYPE_MASK,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };

#define OWR_BUS_GET_PRIVATE(obj) \
        (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_BUS, OwrBusPrivate))

G_DEFINE_TYPE(OwrBus, owr_bus, G_TYPE_OBJECT)

struct _OwrBusPrivate {
    OwrMessageType message_type_mask;
    GThread *thread;
    GAsyncQueue *queue;

    OwrBusMessageCallback callback_func;
    gpointer callback_user_data;
    GDestroyNotify callback_destroy_data;
    GMutex callback_mutex;
};

static void owr_bus_finalize(GObject *);
static void owr_bus_set_property(GObject *, guint property_id, const GValue *, GParamSpec *);
static void owr_bus_get_property(GObject *, guint property_id, GValue *, GParamSpec *);
static gpointer bus_thread_func(OwrBus *bus);

static OwrMessage last_message;

static void owr_bus_class_init(OwrBusClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrBusPrivate));

    obj_properties[PROP_MESSAGE_TYPE_MASK] = g_param_spec_flags("message-type-mask", "message-type-mask",
        "The message types that the bus should forward, other message types will be discarded"
        " (default: forward all messages)",
        OWR_TYPE_MESSAGE_TYPE, DEFAULT_MESSAGE_TYPE_MASK,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    gobject_class->set_property = owr_bus_set_property;
    gobject_class->get_property = owr_bus_get_property;

    gobject_class->finalize = owr_bus_finalize;

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}

static void owr_bus_init(OwrBus *bus)
{
    OwrBusPrivate *priv;

    bus->priv = priv = OWR_BUS_GET_PRIVATE(bus);

    priv->message_type_mask = DEFAULT_MESSAGE_TYPE_MASK;

    priv->queue = g_async_queue_new_full((GDestroyNotify) _owr_message_unref);
    priv->thread = g_thread_new("owr-bus-thread", (GThreadFunc) bus_thread_func, bus);

    priv->callback_func = NULL;
    priv->callback_user_data = NULL;
    priv->callback_destroy_data = NULL;
    g_mutex_init(&priv->callback_mutex);
}

static void owr_bus_finalize(GObject *object)
{
    OwrBus *bus = OWR_BUS(object);
    OwrBusPrivate *priv = bus->priv;

    GST_LOG_OBJECT(bus, "pushing last message");
    g_async_queue_push(priv->queue, &last_message);
    g_thread_join(priv->thread);
    GST_LOG_OBJECT(bus, "joined bus thread");
    g_thread_unref(priv->thread);
    priv->thread = NULL;

    g_async_queue_unref(priv->queue);
    priv->queue = NULL;

    if (priv->callback_destroy_data) {
        priv->callback_destroy_data(priv->callback_user_data);
    }
    priv->callback_func = NULL;
    priv->callback_user_data = NULL;
    priv->callback_destroy_data = NULL;
    g_mutex_clear(&priv->callback_mutex);

    G_OBJECT_CLASS(owr_bus_parent_class)->finalize(object);
}

static void owr_bus_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    OwrBusPrivate *priv;

    g_return_if_fail(object);
    priv = OWR_BUS_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_MESSAGE_TYPE_MASK:
        priv->message_type_mask = g_value_get_flags(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void owr_bus_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    OwrBusPrivate *priv;

    g_return_if_fail(object);
    priv = OWR_BUS_GET_PRIVATE(object);

    switch (property_id) {
    case PROP_MESSAGE_TYPE_MASK:
        g_value_set_flags(value, priv->message_type_mask);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

/**
 * owr_bus_new:
 * Returns: (transfer full): a new #OwrBus
 */
OwrBus *owr_bus_new()
{
    return g_object_new(OWR_TYPE_BUS, NULL);
}

/**
 * OwrBusMessageCallback:
 * @origin: (transfer none): the origin of the message, an #OwrMessageOrigin
 * @type: the #OwrMessageType of the message
 * @sub_type: the #OwrMessageSubType of the message
 * @data: (element-type utf8 GValue) (nullable) (transfer none): the data passed to owr_bus_set_message_callback
 * @user_data: (nullable): the data passed to owr_bus_set_message_callback
 */

/**
 * owr_bus_set_message_callback:
 * @bus: an #OwrBus
 * @callback: (scope notified)
 * @user_data: (nullable): user data for @callback
 * @destroy_data: (nullable): a #GDestroyNotify for @user_data
 */
void owr_bus_set_message_callback(OwrBus *bus, OwrBusMessageCallback callback,
    gpointer user_data, GDestroyNotify destroy_data)
{
    OwrBusPrivate *priv;

    g_return_if_fail(OWR_IS_BUS(bus));
    g_return_if_fail(callback);
    priv = OWR_BUS_GET_PRIVATE(bus);

    g_mutex_lock(&priv->callback_mutex);

    if (priv->callback_destroy_data) {
        priv->callback_destroy_data(priv->callback_user_data);
    }
    priv->callback_func = callback;
    priv->callback_user_data = user_data;
    priv->callback_destroy_data = destroy_data;

    g_mutex_unlock(&priv->callback_mutex);
}


/**
 * owr_bus_add_message_origin:
 * @bus: the bus to which the message origin is added
 * @origin: (transfer none): the message origin that is added to the bus
 *
 * Adds a message origin to the bus, adding an already existing message origin has no effect.
 */
void owr_bus_add_message_origin(OwrBus *bus, OwrMessageOrigin *origin)
{
    OwrMessageOriginBusSet *bus_set;
    GWeakRef *ref;

    g_return_if_fail(OWR_IS_BUS(bus));
    g_return_if_fail(OWR_IS_MESSAGE_ORIGIN(origin));

    bus_set = owr_message_origin_get_bus_set(origin);
    g_return_if_fail(bus_set);

    ref = g_slice_new0(GWeakRef);
    g_weak_ref_init(ref, bus);

    GST_DEBUG_OBJECT(bus, "adding message origin %p with weak bus ref: %p", origin, ref);

    g_mutex_lock(&bus_set->mutex);
    g_hash_table_insert(bus_set->table, bus, ref);
    g_mutex_unlock(&bus_set->mutex);
}

/**
 * owr_bus_remove_message_origin:
 * @bus: the bus from which the message origin should be removed
 * @origin: (transfer none): the message origin that should be removed from the bus
 *
 * Removes a message origin from the bus if it exists, otherwise nothing is done.
 */
void owr_bus_remove_message_origin(OwrBus *bus, OwrMessageOrigin *origin)
{
    OwrMessageOriginBusSet *bus_set;

    g_return_if_fail(OWR_IS_BUS(bus));
    g_return_if_fail(OWR_IS_MESSAGE_ORIGIN(origin));

    bus_set = owr_message_origin_get_bus_set(origin);
    g_return_if_fail(bus_set);

    g_mutex_lock(&bus_set->mutex);
    if (g_hash_table_remove(bus_set->table, bus)) {
        GST_DEBUG_OBJECT(bus, "removed message origin %p", origin);
    }
    g_mutex_unlock(&bus_set->mutex);
}

/**
 * _owr_bus_post_message:
 * @bus: (transfer none): the bus that the message is posted to
 * @message: (transfer full): the message to post
 */
void _owr_bus_post_message(OwrBus *bus, OwrMessage *message)
{
    OwrBusPrivate *priv;

    g_return_if_fail(OWR_IS_BUS(bus));
    g_return_if_fail(message);
    priv = OWR_BUS_GET_PRIVATE(bus);

    if (message->type & priv->message_type_mask) {
        _owr_message_ref(message);
        g_async_queue_push(priv->queue, message);
    }
}

static gpointer bus_thread_func(OwrBus *bus)
{
    OwrBusPrivate *priv;
    OwrMessage *msg;

    g_return_val_if_fail(OWR_IS_BUS(bus), NULL);
    priv = OWR_BUS_GET_PRIVATE(bus);

    GST_DEBUG("bus thread started");

    while ((msg = g_async_queue_pop(priv->queue)) != &last_message) {
        g_mutex_lock(&priv->callback_mutex);
        if (priv->callback_func) {
            priv->callback_func(msg->origin, msg->type, msg->sub_type, msg->data, priv->callback_user_data);
        }
        g_mutex_unlock(&priv->callback_mutex);

        _owr_message_unref(msg);
    }

    GST_DEBUG("exiting bus thread");

    return NULL;
}

OwrMessage *_owr_message_new(OwrMessageOrigin *origin, OwrMessageType type, OwrMessageSubType sub_type, GHashTable *data)
{
    OwrMessage *message;

    message = g_slice_new0(OwrMessage);
    message->origin = g_object_ref(origin);
    message->type = type;
    message->sub_type = sub_type;
    message->data = data;
    message->ref_count = 1;

    return message;
}

void _owr_message_unref(OwrMessage *message)
{
    g_return_if_fail(message);
    g_object_unref(message->origin);

    if (g_atomic_int_dec_and_test(&message->ref_count)) {
        GST_TRACE("freeing message: %p", message);
        if (message->data)
            g_hash_table_unref(message->data);
        g_slice_free(OwrMessage, message);
    }
}

void _owr_message_ref(OwrMessage *message)
{
    g_return_if_fail(message);
    g_object_ref(message->origin);
    g_atomic_int_inc(&message->ref_count);
}

GType owr_message_type_get_type(void)
{
    static const GFlagsValue types[] = {
        {OWR_MESSAGE_TYPE_ERROR, "Error", "error"},
        {OWR_MESSAGE_TYPE_STATS, "State", "state"},
        {OWR_MESSAGE_TYPE_EVENT, "Event", "event"},
        {0, NULL, NULL}
    };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *)&id)) {
        GType _id = g_flags_register_static("OwrMessageTypes", types);
        g_once_init_leave((gsize *)&id, _id);
    }

    return id;
}

GType owr_message_sub_type_get_type(void)
{
    static const GEnumValue types[] = {
        {OWR_ERROR_TYPE_TEST, "Error Test", "error-test"},
        {OWR_ERROR_TYPE_PROCESSING_ERROR, "Processing error", "processing-error"},
        {OWR_STATS_TYPE_TEST, "Stats Test", "stats-test"},
        {OWR_STATS_TYPE_SCHEDULE, "Schedule", "schedule"},
        {OWR_STATS_TYPE_SEND_PIPELINE_ADDED, "Send pipeline added", "send-pipeline-added"},
        {OWR_STATS_TYPE_SEND_PIPELINE_REMOVED, "Send pipeline removed", "send-pipeline-removed"},
        {OWR_EVENT_TYPE_TEST, "Event Test", "event-test"},
        {OWR_EVENT_TYPE_RENDERER_STARTED, "Renderer started", "renderer-started"},
        {OWR_EVENT_TYPE_RENDERER_STOPPED, "Renderer stopped", "renderer-stopped"},
        {OWR_EVENT_TYPE_LOCAL_SOURCE_STARTED, "Local source started", "local-source-started"},
        {OWR_EVENT_TYPE_LOCAL_SOURCE_STOPPED, "Local source stopped", "local-source-stopped"},
        {0, NULL, NULL}
    };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *)&id)) {
        GType _id = g_enum_register_static("OwrMessageSubTypes", types);
        g_once_init_leave((gsize *)&id, _id);
    }

    return id;
}
