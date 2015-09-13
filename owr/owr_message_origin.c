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
\*\ OwrMessageOrigin
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_message_origin.h"

#include "owr_message_origin_private.h"

#include "owr_bus_private.h"
#include "owr_utils.h"

G_DEFINE_INTERFACE(OwrMessageOrigin, owr_message_origin, 0)

void owr_message_origin_default_init(OwrMessageOriginInterface *interface)
{
    interface->get_bus_set = NULL;
}

static void bus_set_value_destroy_func(GWeakRef *ref)
{
    g_weak_ref_clear(ref);
    g_slice_free(GWeakRef, ref);
}

OwrMessageOriginBusSet *owr_message_origin_bus_set_new()
{
    OwrMessageOriginBusSet *bus_set;

    bus_set = g_slice_new0(OwrMessageOriginBusSet);
    bus_set->table =  g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
        (GDestroyNotify) bus_set_value_destroy_func);
    g_mutex_init(&bus_set->mutex);

    return bus_set;
}

void owr_message_origin_bus_set_free(OwrMessageOriginBusSet *bus_set)
{
    g_hash_table_unref(bus_set->table);
    bus_set->table = NULL;
    g_mutex_clear(&bus_set->mutex);
    g_slice_free(OwrMessageOriginBusSet, bus_set);
}

OwrMessageOriginBusSet *owr_message_origin_get_bus_set(OwrMessageOrigin *origin)
{
    OwrMessageOriginInterface *interface;
    OwrMessageOriginBusSet *result = NULL;

    g_return_val_if_fail(OWR_IS_MESSAGE_ORIGIN(origin), NULL);

    interface = OWR_MESSAGE_ORIGIN_GET_INTERFACE(origin);

    if (interface->get_bus_set) {
        result = interface->get_bus_set(origin);
    }

    return result;
}

/**
 * owr_message_origin_post_message:
 * @origin: (transfer none): the origin that is posting the message
 * @type: the #OwrMessageType of the message
 * @sub_type: the #OwrMessageSubType of the message
 * @data: (element-type utf8 GValue) (nullable) (transfer full): extra data
 *
 * Post a new message to all buses that are subscribed to @origin
 */
void owr_message_origin_post_message(OwrMessageOrigin *origin, OwrMessageType type, OwrMessageSubType sub_type, GHashTable *data)
{
    OwrMessage *message;
    OwrMessageOriginBusSet *bus_set;
    GHashTableIter iter;
    GWeakRef *ref;
    OwrBus *bus;

    g_return_if_fail(OWR_IS_MESSAGE_ORIGIN(origin));

    message = _owr_message_new(origin, type, sub_type, data);
    GST_TRACE_OBJECT(origin, "posting message %p", message);

    bus_set = owr_message_origin_get_bus_set(origin);
    g_return_if_fail(bus_set);

    g_mutex_lock(&bus_set->mutex);

    g_hash_table_iter_init(&iter, bus_set->table);
    while (g_hash_table_iter_next(&iter, NULL, (gpointer *) &ref)) {
        bus = g_weak_ref_get(ref);
        if (bus) {
            _owr_bus_post_message(bus, message);
            g_object_unref(bus);
        } else {
            GST_DEBUG_OBJECT(origin, "message bus finalized, removing weak ref: %p", ref);
            g_hash_table_iter_remove(&iter);
        }
    }

    _owr_message_unref(message);

    g_mutex_unlock(&bus_set->mutex);
}

