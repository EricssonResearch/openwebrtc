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

#include "owr.h"
#include "owr_bus.h"
#include "owr_message_origin.h"
#include "owr_message_origin_private.h"
#include "owr_utils.h"

#include <stdlib.h>

/* OwrMessageOrigin mock implementation */

#define MOCK_TYPE_ORIGIN            (mock_origin_get_type())
#define MOCK_ORIGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MOCK_TYPE_ORIGIN, MockOrigin))
#define MOCK_IS_ORIGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MOCK_TYPE_ORIGIN))

typedef struct {
    GObject parent_instance;

    OwrMessageOriginBusSet *bus_set;
} MockOrigin;

typedef struct {
    GObjectClass parent_class;
} MockOriginClass;

static gpointer mock_origin_get_bus_set(OwrMessageOrigin *origin)
{
    MockOrigin *mock = (MockOrigin *)origin;
    return mock->bus_set;
}

static void mock_origin_interface_init(OwrMessageOriginInterface *interface)
{
    interface->get_bus_set = mock_origin_get_bus_set;
}

static void mock_origin_finalize(GObject *object);

G_DEFINE_TYPE_WITH_CODE(MockOrigin, mock_origin, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(OWR_TYPE_MESSAGE_ORIGIN, mock_origin_interface_init))

static void mock_origin_class_init(MockOriginClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mock_origin_finalize;
}

static void mock_origin_init(MockOrigin *mock)
{
    mock->bus_set = owr_message_origin_bus_set_new();
}

static void mock_origin_finalize(GObject *object)
{
    MockOrigin *mock = (MockOrigin *) object;

    owr_message_origin_bus_set_free(mock->bus_set);

    G_OBJECT_CLASS(mock_origin_parent_class)->finalize(object);
}

static OwrMessageOrigin *mock_origin_new()
{
    return g_object_new(MOCK_TYPE_ORIGIN, NULL);
}

static void mock_origin_assert_bus_table_size(OwrMessageOrigin *origin, guint expected_size)
{
    guint actual_size;

    actual_size = g_hash_table_size(MOCK_ORIGIN(origin)->bus_set->table);

    if (actual_size != expected_size) {
        g_print("** ERROR ** bus size assertion failed:\n");
        g_print("expected size: %d\n", expected_size);
        g_print("actual size: %d\n", actual_size);
        g_assert_not_reached();
    }
}




static gpointer timeout_thread_func(gpointer data)
{
    OWR_UNUSED(data);

    g_usleep(G_USEC_PER_SEC);
    g_print("** ERROR ** test timed out\n");
    exit(-1);
}

static gchar *expected_log_string;

static void expect_assert_happened()
{
    if (expected_log_string) {
        g_print("** ERROR ** log message assertion failed:\n");
        g_print("expected: [%s]\n", expected_log_string);
        g_print("but nothing was logged\n");
        exit(-1);
    }
}

static void expect_assert(const gchar *function, const gchar* assertion)
{
    const gchar *prg_name = g_get_prgname();

    if (!prg_name)
        prg_name = "process";

    g_free(expected_log_string);
    expected_log_string = g_strdup_printf("%s: assertion '%s' failed", function, assertion);
}

static void log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data)
{
    (void) log_domain;
    (void) log_level;
    (void) user_data;

    if (g_strcmp0(message, expected_log_string)) {
        g_print("** ERROR ** log message assertion failed:\n");
        g_print("expected: \"%s\"\n", expected_log_string);
        g_print("but got : \"%s\"\n", message);
        exit(-1);
    }
    expected_log_string = NULL;
}

static void expect_message_received(GAsyncQueue *queue, OwrMessageSubType expected_sub_type)
{
    OwrMessageSubType sub_type = GPOINTER_TO_INT(g_async_queue_pop(queue)) - 1;

    if (sub_type != expected_sub_type) {
        g_print("** ERROR ** message assertion failed:\n");
        g_print("expected sub type: %d\n", expected_sub_type);
        g_print("but got: %d\n", sub_type);
        exit(-1);
    } else {
        g_print("received expected message with sub type: %d\n", sub_type);
    }
}

static void on_message(OwrMessageOrigin *origin, OwrMessageType type, OwrMessageSubType sub_type, GHashTable *data, gpointer user_data)
{
    GAsyncQueue *queue = (GAsyncQueue *) user_data;
    OWR_UNUSED(origin);
    OWR_UNUSED(type);
    OWR_UNUSED(data);

    g_async_queue_push(queue, GINT_TO_POINTER(sub_type + 1));
}







static void test_illegal_argument_warnings()
{
    OwrBus *bus;
    OwrMessageOrigin *origin;

    bus = owr_bus_new();

    expect_assert("owr_bus_set_message_callback", "OWR_IS_BUS(bus)");
    owr_bus_set_message_callback(NULL, on_message, NULL, NULL);
    expect_assert_happened();

    expect_assert("owr_bus_set_message_callback", "callback");
    owr_bus_set_message_callback(bus, NULL, NULL, NULL);
    expect_assert_happened();

    origin = mock_origin_new();

    expect_assert("owr_bus_add_message_origin", "OWR_IS_BUS(bus)");
    owr_bus_add_message_origin(NULL, NULL);
    expect_assert_happened();

    expect_assert("owr_bus_add_message_origin", "OWR_IS_BUS(bus)");
    owr_bus_add_message_origin(NULL, origin);
    expect_assert_happened();

    expect_assert("owr_bus_add_message_origin", "OWR_IS_MESSAGE_ORIGIN(origin)");
    owr_bus_add_message_origin(bus, NULL);
    expect_assert_happened();

    expect_assert("owr_bus_remove_message_origin", "OWR_IS_BUS(bus)");
    owr_bus_remove_message_origin(NULL, NULL);
    expect_assert_happened();

    expect_assert("owr_bus_remove_message_origin", "OWR_IS_BUS(bus)");
    owr_bus_remove_message_origin(NULL, origin);
    expect_assert_happened();

    expect_assert("owr_bus_remove_message_origin", "OWR_IS_MESSAGE_ORIGIN(origin)");
    owr_bus_remove_message_origin(bus, NULL);
    expect_assert_happened();

    /* should be able to add and remove the same origin multiple times */

    mock_origin_assert_bus_table_size(origin, 0);
    owr_bus_add_message_origin(bus, origin);
    mock_origin_assert_bus_table_size(origin, 1);
    owr_bus_add_message_origin(bus, origin);
    mock_origin_assert_bus_table_size(origin, 1);
    owr_bus_remove_message_origin(bus, origin);
    mock_origin_assert_bus_table_size(origin, 0);
    owr_bus_remove_message_origin(bus, origin);
    mock_origin_assert_bus_table_size(origin, 0);
    owr_bus_add_message_origin(bus, origin);
    mock_origin_assert_bus_table_size(origin, 1);

    g_object_unref(origin);
    g_object_unref(bus);
}

static void test_message_type_mask()
{
    OwrBus *bus;
    OwrMessageOrigin *origin;
    GAsyncQueue *queue;

    bus = owr_bus_new();

    origin = mock_origin_new();
    g_assert(MOCK_ORIGIN(origin)->bus_set);
    g_assert(MOCK_ORIGIN(origin)->bus_set == owr_message_origin_get_bus_set(origin));

    owr_bus_add_message_origin(bus, origin);

    queue = g_async_queue_new();

    owr_bus_set_message_callback(bus, on_message, queue, (GDestroyNotify) g_async_queue_unref);

    OWR_POST_ERROR(origin, TEST, NULL);
    OWR_POST_STATS(origin, TEST, NULL);
    OWR_POST_EVENT(origin, TEST, NULL);

    expect_message_received(queue, OWR_ERROR_TYPE_TEST);
    expect_message_received(queue, OWR_STATS_TYPE_TEST);
    expect_message_received(queue, OWR_EVENT_TYPE_TEST);

    g_object_set(bus, "message-type-mask", OWR_MESSAGE_TYPE_EVENT, NULL);

    OWR_POST_ERROR(origin, TEST, NULL);
    OWR_POST_STATS(origin, TEST, NULL);
    OWR_POST_EVENT(origin, TEST, NULL);

    expect_message_received(queue, OWR_EVENT_TYPE_TEST);

    g_object_set(bus, "message-type-mask", OWR_MESSAGE_TYPE_STATS, NULL);

    owr_message_origin_post_message(origin, OWR_MESSAGE_TYPE_ERROR, OWR_ERROR_TYPE_TEST, NULL);
    owr_message_origin_post_message(origin, OWR_MESSAGE_TYPE_STATS, OWR_STATS_TYPE_TEST, NULL);
    owr_message_origin_post_message(origin, OWR_MESSAGE_TYPE_EVENT, OWR_EVENT_TYPE_TEST, NULL);

    expect_message_received(queue, OWR_STATS_TYPE_TEST);

    g_object_unref(origin);
    g_object_unref(bus);
}

static GPtrArray *create_buses(guint count)
{
    GPtrArray *buses;
    guint i;

    buses = g_ptr_array_new_full(count, (GDestroyNotify) g_object_unref);
    for (i = 0; i < count; i++) {
        g_ptr_array_add(buses, owr_bus_new());
    }
    return buses;
}

static void test_destruction()
{
    OwrBus *bus;
    OwrMessageOrigin *origin;
    OwrMessageOrigin *origin2;
    GPtrArray *buses;
    GAsyncQueue *queue;

    bus = owr_bus_new();
    origin = mock_origin_new();
    queue = g_async_queue_new();

    mock_origin_assert_bus_table_size(origin, 0);
    owr_bus_add_message_origin(bus, origin);
    mock_origin_assert_bus_table_size(origin, 1);
    owr_bus_set_message_callback(bus, on_message, queue, (GDestroyNotify) g_async_queue_unref);

    OWR_POST_EVENT(origin, TEST, NULL);
    expect_message_received(queue, OWR_EVENT_TYPE_TEST);

    g_assert(1 == g_hash_table_size(MOCK_ORIGIN(origin)->bus_set->table));
    OWR_POST_EVENT(origin, TEST, NULL);
    mock_origin_assert_bus_table_size(origin, 1);
    g_object_unref(bus);
    mock_origin_assert_bus_table_size(origin, 1);
    OWR_POST_EVENT(origin, TEST, NULL);
    mock_origin_assert_bus_table_size(origin, 0);

    origin2 = mock_origin_new();
    mock_origin_assert_bus_table_size(origin2, 0);

    buses = create_buses(10);
    g_ptr_array_foreach(buses, (GFunc) owr_bus_add_message_origin, origin);
    g_ptr_array_foreach(buses, (GFunc) owr_bus_add_message_origin, origin2);
    mock_origin_assert_bus_table_size(origin, 10);
    mock_origin_assert_bus_table_size(origin2, 10);
    g_ptr_array_unref(buses);
    mock_origin_assert_bus_table_size(origin, 10);
    mock_origin_assert_bus_table_size(origin2, 10);
    OWR_POST_EVENT(origin, TEST, NULL);
    mock_origin_assert_bus_table_size(origin, 0);
    mock_origin_assert_bus_table_size(origin2, 10);
    OWR_POST_EVENT(origin2, TEST, NULL);
    mock_origin_assert_bus_table_size(origin2, 0);
}

static void add_buses(OwrMessageOrigin *origin, GPtrArray *buses)
{
    g_ptr_array_foreach(buses, (GFunc) owr_bus_add_message_origin, origin);
}

static void add_callback(OwrBus *bus, GAsyncQueue *queue)
{
    owr_bus_set_message_callback(bus, on_message, queue, (GDestroyNotify) g_async_queue_unref);
}

static void post_messages(OwrMessageOrigin *origin, gpointer user_data)
{
    OWR_UNUSED(user_data);

    OWR_POST_EVENT(origin, TEST, NULL);
}

static gpointer post_message_thread_func(OwrMessageOrigin *origin)
{
    OWR_POST_EVENT(origin, TEST, NULL);
    return NULL;
}

static void post_messages_from_new_thread(OwrMessageOrigin *origin, gpointer user_data)
{
    OWR_UNUSED(user_data);
    g_thread_new("bus-test-thread", (GThreadFunc) post_message_thread_func, origin);
}

static gpointer unref_bus_thread_func(OwrBus *bus)
{
    g_object_unref(bus);
    return NULL;
}

static void unref_bus_from_new_thread(OwrBus *bus, gpointer user_data)
{
    OWR_UNUSED(user_data);
    g_thread_new("bus-test-thread", (GThreadFunc) unref_bus_thread_func, bus);
}

static void queue_weak_notify_func(GAsyncQueue *queue, GObject *where_the_object_was)
{
    g_async_queue_push(queue, where_the_object_was);
}

static void add_object_to_unref_queue(OwrBus *bus, GAsyncQueue *queue)
{
    g_object_weak_ref(G_OBJECT(bus), (GWeakNotify) queue_weak_notify_func, queue);
}

static void pop_queue_n_times(GAsyncQueue *queue, guint size)
{
    guint i;

    for (i = 0; i < size; i++) {
        g_async_queue_pop(queue);
    }
}

static void test_mass_messaging()
{
    GPtrArray *buses;
    GPtrArray *origins;
    GAsyncQueue *queue;
    GAsyncQueue *unref_queue;
    const guint size = 20;
    guint i;

    queue = g_async_queue_new();
    unref_queue = g_async_queue_new();

    buses = g_ptr_array_new_full(size, (GDestroyNotify) g_object_unref);
    origins = g_ptr_array_new_full(size, (GDestroyNotify) g_object_unref);

    for (i = 0; i < size; i++) {
        g_ptr_array_add(buses, owr_bus_new());
        g_ptr_array_add(origins, mock_origin_new());
    }

    g_ptr_array_foreach(origins, (GFunc) add_buses, buses);
    g_ptr_array_foreach(buses, (GFunc) add_callback, queue);
    g_ptr_array_foreach(origins, (GFunc) post_messages, NULL);
    pop_queue_n_times(queue, size * size);

    g_ptr_array_foreach(origins, (GFunc) post_messages_from_new_thread, NULL);
    pop_queue_n_times(queue, size * size);

    g_ptr_array_foreach(buses, (GFunc) add_object_to_unref_queue, unref_queue);

    g_ptr_array_foreach(origins, (GFunc) mock_origin_assert_bus_table_size, GUINT_TO_POINTER(size));
    g_ptr_array_foreach(origins, (GFunc) post_messages_from_new_thread, NULL);
    g_ptr_array_foreach(origins, (GFunc) post_messages, NULL);
    g_ptr_array_foreach(buses, (GFunc) unref_bus_from_new_thread, NULL);

    pop_queue_n_times(unref_queue, size);

    g_ptr_array_foreach(origins, (GFunc) post_messages, NULL);
    g_ptr_array_foreach(origins, (GFunc) mock_origin_assert_bus_table_size, GUINT_TO_POINTER(0));
}

static void assert_weak_ref(GWeakRef *weak_ref, gboolean expect_object, guint line)
{
    gpointer object;

    object = g_weak_ref_get(weak_ref);

    if (object) {
        g_object_unref(object);
    }

    if (object && !expect_object) {
        g_print("** ERROR ** ref assertion failed:\n");
        g_print("%u: expected object to be finalized, but got ref\n", line);
        exit(-1);
    } else if (!object && expect_object) {
        g_print("** ERROR ** ref assertion failed:\n");
        g_print("%u: expected ref, but object was finalized\n", line);
        exit(-1);
    }
}

static void test_refcounting()
{
    OwrBus *bus;
    OwrMessageOrigin *origin;
    GWeakRef weak_ref;
    GAsyncQueue *queue;

    queue = g_async_queue_new();

    bus = owr_bus_new();
    owr_bus_set_message_callback(bus, on_message, queue, NULL);
    origin = mock_origin_new();
    owr_bus_add_message_origin(bus, origin);
    g_weak_ref_init(&weak_ref, bus);
    OWR_POST_STATS(origin, TEST, NULL);
    g_object_unref(bus); /* this should finalize the bus, pending messages should not keep it alive */
    assert_weak_ref(&weak_ref, FALSE, __LINE__);
    g_weak_ref_clear(&weak_ref);

    bus = owr_bus_new();
    owr_bus_set_message_callback(bus, on_message, queue, NULL);
    owr_bus_add_message_origin(bus, origin);
    g_weak_ref_init(&weak_ref, origin);
    OWR_POST_STATS(origin, TEST, NULL);
    g_object_unref(origin); /* the origin should be kept alive though */
    g_object_ref(origin);
    assert_weak_ref(&weak_ref, TRUE, __LINE__);
    g_object_unref(origin);
    g_assert(g_async_queue_timeout_pop(queue, G_USEC_PER_SEC));
    g_usleep(1000); /* messages are cleaned up after all callbacks have happened, so wait a bit more */
    assert_weak_ref(&weak_ref, FALSE, __LINE__); /* but be cleaned up after the message was handled */
    g_weak_ref_clear(&weak_ref);


    /* same as previous tests, but with message filter */
    origin = mock_origin_new();
    owr_bus_add_message_origin(bus, origin);
    g_weak_ref_init(&weak_ref, origin);
    g_object_set(bus, "message-type-mask", OWR_MESSAGE_TYPE_STATS, NULL);
    OWR_POST_STATS(origin, TEST, NULL);
    OWR_POST_EVENT(origin, TEST, NULL);
    OWR_POST_ERROR(origin, TEST, NULL);
    g_object_unref(origin);
    g_object_ref(origin);
    assert_weak_ref(&weak_ref, TRUE, __LINE__);
    g_object_unref(origin);
    g_assert(g_async_queue_timeout_pop(queue, G_USEC_PER_SEC));
    g_usleep(1000);
    assert_weak_ref(&weak_ref, FALSE, __LINE__);
    g_weak_ref_clear(&weak_ref);

    origin = mock_origin_new();
    owr_bus_add_message_origin(bus, origin);
    g_weak_ref_init(&weak_ref, bus);
    OWR_POST_STATS(origin, TEST, NULL);
    OWR_POST_EVENT(origin, TEST, NULL);
    OWR_POST_ERROR(origin, TEST, NULL);
    g_object_unref(bus);
    assert_weak_ref(&weak_ref, FALSE, __LINE__);
    g_weak_ref_clear(&weak_ref);
}

int main()
{
    guint64 start_time;
    guint64 end_time;

    start_time = g_get_monotonic_time();
    g_log_set_handler(NULL, G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_FATAL, log_handler, NULL);

    /* This whole thing should be over within 1s */
    g_thread_new("test-timeout-thread", timeout_thread_func, NULL);

    /* we're not running the mainloop, as the message bus should work without it */
    owr_init(NULL);

    test_illegal_argument_warnings();
    test_message_type_mask();
    test_destruction();
    test_mass_messaging();
    test_refcounting();

    end_time = g_get_monotonic_time();

    g_print("\n *** Test successful! ***\n");
    g_print("Duration: %.3fs\n\n", (end_time - start_time) / 1000000.0);

    return 0;
}
