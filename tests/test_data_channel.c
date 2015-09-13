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
#include "owr_session.h"
#include "owr_data_session.h"
#include "owr_data_channel.h"
#include "owr_transport_agent.h"
#include "test_utils.h"

#include <string.h>

static gboolean wait_for_dtls;
static guint channel_id = 1;

static GOptionEntry entries[] = {
    { "wait-dtls", 0, 0, G_OPTION_ARG_NONE, &wait_for_dtls, "Wait for DTLS handshake to complete", NULL },
    { NULL, }
};

static OwrTransportAgent *left_transport_agent = NULL;
static OwrTransportAgent *right_transport_agent = NULL;
static OwrDataSession *left_session = NULL;
static OwrDataSession *right_session = NULL;

static void on_data(OwrDataChannel *data_channel, const gchar *string, GAsyncQueue *msg_queue)
{
    (void) data_channel;
    g_async_queue_push(msg_queue, g_strdup(string));
}

static void on_binary_data(OwrDataChannel *data_channel, const gchar *data, guint length, GAsyncQueue *msg_queue)
{
    (void) data_channel;
    g_async_queue_push(msg_queue, g_strndup(data, length));
}

static gboolean run_datachannel_test(const gchar *label, OwrDataChannel *left, OwrDataChannel *right)
{
    GAsyncQueue *msg_queue = g_async_queue_new();
    gchar *received_message;
    gint expected_message_count = 4;
    const gchar *binary_message;
    int i;

    g_print("[%s] starting\n", label);

    g_signal_connect(left, "on-data", G_CALLBACK(on_data), msg_queue);
    g_signal_connect(right, "on-data", G_CALLBACK(on_data), msg_queue);
    g_signal_connect(left, "on-binary-data", G_CALLBACK(on_binary_data), msg_queue);
    g_signal_connect(right, "on-binary-data", G_CALLBACK(on_binary_data), msg_queue);

    g_print("[%s] sending messages\n", label);

    owr_data_channel_send(left, "text: left->right");
    owr_data_channel_send(right, "text: right->left");
    binary_message = "binary: left->right";
    owr_data_channel_send_binary(left, (const guint8 *) binary_message, strlen(binary_message));
    binary_message = "binary: right->left";
    owr_data_channel_send_binary(right, (const guint8 *) binary_message, strlen(binary_message));

    g_print("[%s] expecting messages\n", label);

    for (i = 0; i < expected_message_count; i++) {
        received_message = g_async_queue_timeout_pop(msg_queue, 5000000);
        if (received_message) {
            g_print("[%s] received message: %s\n", label, received_message);
            g_free(received_message);
        } else {
            g_print("[%s] *** timeout while waiting for message\n", label);
            break;
        }
    }
    g_signal_handlers_disconnect_by_data(left, msg_queue);
    g_signal_handlers_disconnect_by_data(right, msg_queue);
    g_async_queue_unref(msg_queue);

    if (i >= expected_message_count) {
        g_print("[%s] Success, ", label);
    } else {
        g_print("[%s] Failure, ", label);
    }
    g_print("received %d / %d messages\n", i, expected_message_count);

    return i >= expected_message_count;
}

static void on_data_channel_requested(OwrDataSession *session, gboolean ordered,
    gint max_packet_life_time, gint max_retransmits, const gchar *protocol,
    gboolean negotiated, guint16 id, const gchar *label, GAsyncQueue *msg_queue)
{
    OwrDataChannel *data_channel;

    g_print("received data channel request: %s\n", label);
    data_channel = owr_data_channel_new(ordered, max_packet_life_time, max_retransmits, protocol, negotiated, id, label);
    g_async_queue_push(msg_queue, data_channel);
}

static gboolean run_requested_channel_test(gboolean left_to_right)
{
    GAsyncQueue *msg_queue = g_async_queue_new();
    OwrDataChannel *left;
    OwrDataChannel *right;
    OwrDataSession *session1;
    OwrDataSession *session2;
    guint id = channel_id++ * 2 + (left_to_right ? 0 : 1);

    if (left_to_right) {
        session1 = left_session;
        session2 = right_session;
    } else {
        session1 = right_session;
        session2 = left_session;
    }

    g_print("\n >>> Running requested channel test\n\n");

    g_signal_connect(session2, "on-data-channel-requested", G_CALLBACK(on_data_channel_requested), msg_queue);

    /* ordered, max_packet_life_time, max_retransmits, protocol, negotiated, id, label */

    left = owr_data_channel_new(FALSE, 5000, -1, "OTP", FALSE, id, "requested");
    owr_data_session_add_data_channel(session1, left);

    right = g_async_queue_timeout_pop(msg_queue, 5000000);
    g_async_queue_unref(msg_queue);
    if (!right) {
        g_print("requested: timeout while waiting for data channel\n");
        return FALSE;
    }
    owr_data_session_add_data_channel(session2, right);

    return run_datachannel_test("requested", left, right);
}

static void on_ready_state(OwrDataChannel *channel, GParamSpec *pspec, GAsyncQueue *msg_queue)
{
    gint ready_state;

    g_object_get(channel, "ready-state", &ready_state, NULL);

    if (ready_state == OWR_DATA_CHANNEL_READY_STATE_OPEN) {
        g_async_queue_push(msg_queue, "ready");
    }
}

static gboolean run_prenegotiated_channel_test(gboolean wait_until_ready)
{
    OwrDataChannel *left;
    OwrDataChannel *right;
    guint id = channel_id++ * 2;

    g_print("\n >>> Running prenegotiated channels test\n\n");

    /* ordered, max_packet_life_time, max_retransmits, protocol, negotiated, id, label */
    left = owr_data_channel_new(FALSE, 5000, -1, "OTP", TRUE, id, "prenegotiated");
    right = owr_data_channel_new(FALSE, 5000, -1, "OTP", TRUE, id, "prenegotiated");

    owr_data_session_add_data_channel(left_session, left);
    owr_data_session_add_data_channel(right_session, right);

    if (wait_until_ready) {
        GAsyncQueue *msg_queue = g_async_queue_new();
        gboolean data_channels_ready;

        g_signal_connect(left, "notify::ready-state", G_CALLBACK(on_ready_state), msg_queue);
        g_signal_connect(right, "notify::ready-state", G_CALLBACK(on_ready_state), msg_queue);

        g_print("waiting for data channels to become ready\n");
        data_channels_ready = !!g_async_queue_timeout_pop(msg_queue, 5000000);
        data_channels_ready &= !!g_async_queue_timeout_pop(msg_queue, 5000000);
        g_async_queue_unref(msg_queue);

        if (!data_channels_ready) {
            g_print("data channel setup timed out\n");
            return FALSE;
        }
        g_print("data channels are now ready, running test\n");
    } else {
        g_print("data channels are expected to be ready immediately, running test\n");
    }

    return run_datachannel_test("prenegotiated", left, right);
}

static void got_candidate(OwrSession *ignored, OwrCandidate *candidate, OwrSession *session)
{
    (void) ignored;
    owr_session_add_remote_candidate(OWR_SESSION(session), candidate);
}

static void on_dtls_peer_certificate(OwrDataSession *session, GParamSpec *pspec, GAsyncQueue *msg_queue)
{
    (void) session;
    (void) pspec;
    g_print("received peer certificate\n");
    g_async_queue_push(msg_queue, "ready");
}

static gboolean setup_transport_agents()
{
    g_print("Setting up transport agents\n");
    // LEFT
    left_transport_agent = owr_transport_agent_new(FALSE);
    g_assert(OWR_IS_TRANSPORT_AGENT(left_transport_agent));

    owr_transport_agent_set_local_port_range(left_transport_agent, 5000, 5999);
    owr_transport_agent_add_local_address(left_transport_agent, "127.0.0.1");

    // RIGHT
    right_transport_agent = owr_transport_agent_new(TRUE);
    g_assert(OWR_IS_TRANSPORT_AGENT(right_transport_agent));

    owr_transport_agent_set_local_port_range(right_transport_agent, 5000, 5999);
    owr_transport_agent_add_local_address(right_transport_agent, "127.0.0.1");

    left_session = owr_data_session_new(TRUE);
    right_session = owr_data_session_new(FALSE);

    g_object_set(left_session, "sctp-local-port", 5000, "sctp-remote-port", 5000, NULL);
    g_object_set(right_session, "sctp-local-port", 5000, "sctp-remote-port", 5000, NULL);

    g_signal_connect(left_session, "on-new-candidate", G_CALLBACK(got_candidate), right_session);
    g_signal_connect(right_session, "on-new-candidate", G_CALLBACK(got_candidate), left_session);

    owr_transport_agent_add_session(left_transport_agent, OWR_SESSION(left_session));
    owr_transport_agent_add_session(right_transport_agent, OWR_SESSION(right_session));

    if (wait_for_dtls) {
        gboolean peer_certificate_received;
        GAsyncQueue *msg_queue = g_async_queue_new();

        g_signal_connect(left_session, "notify::dtls-peer-certificate", G_CALLBACK(on_dtls_peer_certificate), msg_queue);
        g_signal_connect(right_session, "notify::dtls-peer-certificate", G_CALLBACK(on_dtls_peer_certificate), msg_queue);

        g_print("waiting for dtls handshake to complete\n");

        peer_certificate_received = !!g_async_queue_timeout_pop(msg_queue, 5000000);
        peer_certificate_received &= !!g_async_queue_timeout_pop(msg_queue, 5000000);
        g_async_queue_unref(msg_queue);

        if (!peer_certificate_received) {
            g_print("dtls handshake timed out\n");
            return FALSE;
        }

        g_print("dtls handshake to completed\n");
    }

    return TRUE;
}

int main(int argc, char **argv)
{
    GOptionContext *options;
    GError *error = NULL;
    gint success_count = 0;
    gint test_count = 0;

    options = g_option_context_new(NULL);
    g_option_context_add_main_entries(options, entries, NULL);
    if (!g_option_context_parse(options, &argc, &argv, &error)) {
        g_print("Failed to parse options: %s\n", error->message);
        return -2;
    }

    g_print("\n *** Starting data channel test ***\n\n");

    owr_init(NULL);
    owr_run_in_background();

    if (setup_transport_agents()) {
        success_count += run_prenegotiated_channel_test(TRUE); test_count++;
        success_count += run_requested_channel_test(TRUE); test_count++;
        success_count += run_requested_channel_test(FALSE); test_count++;
        success_count += run_prenegotiated_channel_test(FALSE); test_count++;

        g_print("\n%d / %d test were successful\n", success_count, test_count);

        g_print("Dumping transport agent pipelines!\n");

        write_dot_file("test_data_channel-left-transport_agent", owr_transport_agent_get_dot_data(left_transport_agent), TRUE);
        write_dot_file("test_data_channel-right-transport_agent", owr_transport_agent_get_dot_data(right_transport_agent), TRUE);

        return test_count - success_count;
    }

    g_print("\n *** Failed initial setup ***\n\n");
    return -1;
}
