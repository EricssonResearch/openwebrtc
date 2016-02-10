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

#define _GNU_SOURCE 1
#include "owr.h"
#include "owr_audio_payload.h"
#include "owr_audio_renderer.h"
#include "owr_bus.h"
#include "owr_local.h"
#include "owr_media_renderer.h"
#include "owr_media_session.h"
#include "owr_media_source.h"
#include "owr_payload.h"
#include "owr_session.h"
#include "owr_transport_agent.h"
#include "owr_uri_source.h"
#include "owr_uri_source_agent.h"
#include "owr_video_payload.h"
#include "owr_video_renderer.h"
#include "owr_window_registry.h"
#include "test_utils.h"

#include <string.h>

static OwrTransportAgent *recv_transport_agent = NULL;
static OwrMediaSession *recv_session_audio = NULL;
static OwrMediaSession *recv_session_video = NULL;
static OwrTransportAgent *send_transport_agent = NULL;
static OwrMediaSession *send_session_audio = NULL;
static OwrMediaSession *send_session_video = NULL;
static OwrMediaRenderer *video_renderer = NULL;
static OwrMediaRenderer *remote_video_renderer = NULL;
static OwrMediaRenderer *remote_audio_renderer = NULL;
static OwrMediaSource *audio_source = NULL;
static OwrMediaSource *video_source = NULL;
static OwrMediaSource *remote_audio_source = NULL;
static OwrMediaSource *remote_video_source = NULL;
static OwrBus *bus = NULL;
static OwrURISourceAgent *uri_source_agent = NULL;

static gchar *uri = NULL;
static gboolean disable_video = FALSE, disable_audio = FALSE, print_messages = FALSE, adaptation = FALSE;
static gchar *local_addr = NULL, *remote_addr = NULL;
static const char *stun_pass = "5f1f2614f722cd60fbae275193608d4e";

static GOptionEntry entries[] = {
    { "disable-video", 0, 0, G_OPTION_ARG_NONE, &disable_video, "Disable video", NULL },
    { "disable-audio", 0, 0, G_OPTION_ARG_NONE, &disable_audio, "Disable audio", NULL },
    { "uri", 'u', 0, G_OPTION_ARG_STRING, &uri, "URI to use as source", NULL },
    { "print-messages", 'p', 0, G_OPTION_ARG_NONE, &print_messages, "Prints all messages, instead of just errors", NULL },
    { "local-address", 'l', 0, G_OPTION_ARG_STRING, &local_addr, "Local candidate address", NULL },
    { "remote-address", 'r', 0, G_OPTION_ARG_STRING, &remote_addr, "Remote candidate address", NULL },
    { "adaptation", 'a', 0, G_OPTION_ARG_NONE, &adaptation, "Enable bitrate adaptation", NULL },
    { NULL, }
};

static void got_remote_source(OwrMediaSession *session, OwrMediaSource *source, gpointer user_data)
{
    gchar *name = NULL;
    OwrMediaType media_type;

    g_assert(!user_data);

    g_object_get(source, "media-type", &media_type, "name", &name, NULL);

    g_print("Got remote source: %s\n", name);

    if (media_type == OWR_MEDIA_TYPE_VIDEO) {
        OwrVideoRenderer *renderer;

        g_print("Creating video renderer\n");
        renderer = owr_video_renderer_new(NULL);
        g_assert(renderer);
        owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(renderer));

        g_print("Connecting source to video renderer\n");
        owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);
        remote_video_renderer = OWR_MEDIA_RENDERER(renderer);
    } else if (media_type == OWR_MEDIA_TYPE_AUDIO) {
        OwrAudioRenderer *renderer;

        g_print("Creating audio renderer\n");
        renderer = owr_audio_renderer_new();
        g_assert(renderer);
        owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(renderer));

        g_print("Connecting source to audio renderer\n");
        owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);
        remote_audio_renderer = OWR_MEDIA_RENDERER(renderer);
    }

    g_free(name);

    if (media_type == OWR_MEDIA_TYPE_VIDEO)
        remote_video_source = g_object_ref(source);
    else
        remote_audio_source = g_object_ref(source);
}

static void got_candidate(OwrMediaSession *session_a, OwrCandidate *candidate, OwrMediaSession *session_b)
{
    GList *local_candidates;
    gchar *addr;
    guint port, component;
    guint rtp_port, rtcp_port;

    if (!local_addr || !remote_addr)
        owr_session_add_remote_candidate(OWR_SESSION(session_b), candidate);
    else {
        local_candidates = g_object_get_data(G_OBJECT(session_a), "local-candidates");
        local_candidates = g_list_prepend(local_candidates, candidate);
        g_object_set_data(G_OBJECT(session_a), "local-candidates", local_candidates);

        g_object_set(candidate, "ufrag", local_addr, "password", stun_pass, NULL);
    }
}

static void gathering_done(OwrSession *session)
{
    OwrMediaSession *msession = OWR_MEDIA_SESSION(session);
    OwrCandidate *rtp_candidate, *rtcp_candidate;
    GList *candidates;
    guint l_rtp_port, l_rtcp_port, r_rtp_port, r_rtcp_port;

    rtp_candidate = owr_candidate_new(OWR_CANDIDATE_TYPE_HOST, OWR_COMPONENT_TYPE_RTP);
    rtcp_candidate = owr_candidate_new(OWR_CANDIDATE_TYPE_HOST, OWR_COMPONENT_TYPE_RTCP);

    g_object_set(rtp_candidate, "address", remote_addr, "ufrag", remote_addr, "password", stun_pass, "foundation", "3", NULL);
    g_object_set(rtcp_candidate, "address", remote_addr, "ufrag", remote_addr, "password", stun_pass, "foundation", "3", NULL);

    if (msession == send_session_video) {
        l_rtp_port = 5120;
        l_rtcp_port = 5121;
        r_rtp_port = 5122;
        r_rtcp_port = 5123;
    } else if (msession == send_session_audio) {
        l_rtp_port = 5124;
        l_rtcp_port = 5125;
        r_rtp_port = 5126;
        r_rtcp_port = 5127;
    } else if (msession == recv_session_video) {
        l_rtp_port = 5122;
        l_rtcp_port = 5123;
        r_rtp_port = 5120;
        r_rtcp_port = 5121;
    } else if (msession == recv_session_audio) {
        l_rtp_port = 5126;
        l_rtcp_port = 5127;
        r_rtp_port = 5124;
        r_rtcp_port = 5125;
    }

    g_object_set(rtp_candidate, "port", r_rtp_port, NULL);
    g_object_set(rtcp_candidate, "port", r_rtcp_port, NULL);

    owr_session_add_remote_candidate(session, rtp_candidate);
    owr_session_add_remote_candidate(session, rtcp_candidate);

    for (candidates = g_object_get_data(G_OBJECT(session), "local-candidates"); candidates;
            candidates = g_list_next(candidates)) {
        OwrCandidate *local_candidate = candidates->data, *remote_candidate;
        OwrComponentType ctype;
        guint port = 0;
        gchar *addr;

        g_object_get(local_candidate, "address", &addr, "port", &port, "component-type", &ctype, NULL);

        if (!g_str_equal(addr, local_addr)) {
            g_free(addr);
            continue;
        }
        g_free(addr);

        if (port == l_rtp_port && ctype == OWR_COMPONENT_TYPE_RTP)
            remote_candidate = rtp_candidate;
        else if (port == l_rtcp_port && ctype == OWR_COMPONENT_TYPE_RTCP)
            remote_candidate = rtcp_candidate;
        else
            continue;

        owr_session_force_candidate_pair(session, ctype, local_candidate, remote_candidate);
    }
}

static void got_sources(GList *sources, gpointer user_data)
{
    OwrMediaSource *source = NULL;
    static gboolean have_video = FALSE, have_audio = FALSE;

    g_assert(sources);

    while (sources && (source = sources->data)) {
        OwrMediaType media_type;
        OwrSourceType source_type;

        g_assert(OWR_IS_MEDIA_SOURCE(source));

        g_object_get(source, "type", &source_type, "media-type", &media_type, NULL);

        if (remote_addr) {
            owr_transport_agent_add_helper_server(send_transport_agent, OWR_HELPER_SERVER_TYPE_STUN,
                "stun.services.mozilla.com", 3478, NULL, NULL);
            owr_transport_agent_add_helper_server(recv_transport_agent, OWR_HELPER_SERVER_TYPE_STUN,
                "stun.services.mozilla.com", 3478, NULL, NULL);
        }

        if (!disable_video && !have_video && media_type == OWR_MEDIA_TYPE_VIDEO) {
            OwrVideoRenderer *renderer;
            OwrPayload *payload;

            have_video = TRUE;

            owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(source));

            payload = owr_video_payload_new(OWR_CODEC_TYPE_VP8, 103, 90000, TRUE, FALSE);
            g_object_set(payload, "width", 640, "height", 480, "framerate", 30.0, NULL);
            g_object_set(payload, "rtx-payload-type", 123, NULL);
            if (adaptation)
                g_object_set(payload, "adaptation", TRUE, NULL);

            owr_media_session_set_send_payload(send_session_video, payload);

            owr_media_session_set_send_source(send_session_video, source);

            owr_transport_agent_add_session(recv_transport_agent, OWR_SESSION(recv_session_video));
            owr_transport_agent_add_session(send_transport_agent, OWR_SESSION(send_session_video));

            g_print("Displaying self-view\n");

            renderer = owr_video_renderer_new(NULL);
            g_assert(renderer);
            owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(renderer));

            g_object_set(renderer, "width", 640, "height", 480, "max-framerate", 30.0, NULL);
            owr_media_renderer_set_source(OWR_MEDIA_RENDERER(renderer), source);
            video_renderer = OWR_MEDIA_RENDERER(renderer);
            video_source = g_object_ref(source);
        } else if (!disable_audio && !have_audio && media_type == OWR_MEDIA_TYPE_AUDIO) {
            OwrPayload *payload;

            have_audio = TRUE;

            owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(source));

            payload = owr_audio_payload_new(OWR_CODEC_TYPE_OPUS, 100, 48000, 1);
            owr_media_session_set_send_payload(send_session_audio, payload);

            owr_media_session_set_send_source(send_session_audio, source);

            owr_transport_agent_add_session(recv_transport_agent, OWR_SESSION(recv_session_audio));
            owr_transport_agent_add_session(send_transport_agent, OWR_SESSION(send_session_audio));
            audio_source = g_object_ref(source);
        }

        if ((disable_video || have_video) && (disable_audio || have_audio))
            break;

        sources = sources->next;
    }
}

void on_new_source(gpointer *unused, OwrMediaSource *source)
{
    static gboolean have_video = FALSE, have_audio = FALSE;
    static GList *sources = NULL;
    gchar *name = NULL;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;

    (void)unused;
    g_assert(OWR_IS_URI_SOURCE(source));

    if (have_video && have_audio)
        return;

    g_object_get(source, "name", &name, "media-type", &media_type, NULL);

    g_print("Got \"%s\" source!\n", name ? name : "");

    sources = g_list_prepend(sources, g_object_ref(source));

    if (!have_video && media_type == OWR_MEDIA_TYPE_VIDEO)
        have_video = TRUE;

    if (!have_audio && media_type == OWR_MEDIA_TYPE_AUDIO)
        have_audio = TRUE;

    if ((disable_video || have_video) && (disable_audio || have_audio)) {
        got_sources(sources, NULL);
        g_list_free_full(sources, g_object_unref);
    }
}


static gboolean dump_cb(gpointer *user_data)
{
    g_print("Dumping send transport agent pipeline!\n");

    if (video_source)
        write_dot_file("test_send-got_source-video-source", owr_media_source_get_dot_data(video_source), TRUE);
    if (video_renderer)
        write_dot_file("test_send-got_source-video-renderer", owr_media_renderer_get_dot_data(video_renderer), TRUE);
    if (audio_source)
        write_dot_file("test_send-got_source-audio-source", owr_media_source_get_dot_data(audio_source), TRUE);

    if (remote_video_source)
        write_dot_file("test_receive-got_remote_source-video-source", owr_media_source_get_dot_data(remote_video_source), TRUE);
    if (remote_video_renderer)
        write_dot_file("test_receive-got_remote_source-video-renderer", owr_media_renderer_get_dot_data(remote_video_renderer), TRUE);
    if (remote_audio_source)
        write_dot_file("test_receive-got_remote_source-audio-source", owr_media_source_get_dot_data(remote_audio_source), TRUE);
    if (remote_audio_renderer)
        write_dot_file("test_receive-got_remote_source-audio-renderer", owr_media_renderer_get_dot_data(remote_audio_renderer), TRUE);

    write_dot_file("test_send-got_source-transport_agent", owr_transport_agent_get_dot_data(send_transport_agent), TRUE);
    write_dot_file("test_receive-got_remote_source-transport_agent", owr_transport_agent_get_dot_data(recv_transport_agent), TRUE);

    return G_SOURCE_REMOVE;
}

static const gchar *message_origin_name_func(gpointer origin)
{
    if (!origin) {
        return "(null)";
    } else if (origin == recv_transport_agent) {
        return "recv TransportAgent";
    } else if (origin == recv_session_audio) {
        return "recv SessionAudio";
    } else if (origin == recv_session_video) {
        return "recv SessionVideo";
    } else if (origin == send_transport_agent) {
        return "send TransportAgent";
    } else if (origin == send_session_audio) {
        return "send SessionAudio";
    } else if (origin == send_session_video) {
        return "send SessionVideo";
    } else if (origin == video_renderer) {
        return "video Renderer";
    } else if (origin == remote_video_renderer) {
        return "remote VideoRenderer";
    } else if (origin == remote_audio_renderer) {
        return "remote AudioRenderer";
    } else if (origin == audio_source) {
        return "audio Source";
    } else if (origin == video_source) {
        return "video Source";
    } else if (origin == remote_audio_source) {
        return "remote AudioSource";
    } else if (origin == remote_video_source) {
        return "remote VideoSource";
    } else if (origin == owr_window_registry_get()) {
        return "WindowRegistry";
    } else {
        return "(unknown)";
    }
}


int main(int argc, char **argv)
{
    GOptionContext *options;
    GError *error = NULL;

    options = g_option_context_new(NULL);
    g_option_context_add_main_entries(options, entries, NULL);
    if (!g_option_context_parse(options, &argc, &argv, &error)) {
        g_print("Failed to parse options: %s\n", error->message);
        return 1;
    }

    if (disable_audio && disable_video) {
        g_print("Audio and video disabled. Nothing to do.\n");
        return 0;
    }

    /* PREPARE FOR RECEIVING */

    OwrPayload *receive_payload;

    owr_init(NULL);

    bus = owr_bus_new();
    owr_bus_set_message_callback(bus, (OwrBusMessageCallback) bus_message_print_callback,
        message_origin_name_func, NULL);

    if (!print_messages) {
        g_object_set(bus, "message-type-mask", OWR_MESSAGE_TYPE_ERROR, NULL);
    }

    owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(owr_window_registry_get()));

    recv_transport_agent = owr_transport_agent_new(FALSE);
    g_assert(OWR_IS_TRANSPORT_AGENT(recv_transport_agent));
    owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(recv_transport_agent));

    owr_transport_agent_set_local_port_range(recv_transport_agent, 5120, 5127);
    if (!remote_addr)
        owr_transport_agent_add_local_address(recv_transport_agent, "127.0.0.1");
    else if (local_addr)
        owr_transport_agent_add_local_address(recv_transport_agent, local_addr);

    // SEND
    send_transport_agent = owr_transport_agent_new(TRUE);
    g_assert(OWR_IS_TRANSPORT_AGENT(send_transport_agent));
    owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(send_transport_agent));

    owr_transport_agent_set_local_port_range(send_transport_agent, 5120, 5129);
    if (!remote_addr)
        owr_transport_agent_add_local_address(send_transport_agent, "127.0.0.1");

    if (!disable_video) {
        recv_session_video = owr_media_session_new(FALSE);
        owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(recv_session_video));
        send_session_video = owr_media_session_new(TRUE);
        owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(send_session_video));
    }
    if (!disable_audio) {
        recv_session_audio = owr_media_session_new(FALSE);
        owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(recv_session_audio));
        send_session_audio = owr_media_session_new(TRUE);
        owr_bus_add_message_origin(bus, OWR_MESSAGE_ORIGIN(send_session_audio));
    }

    if (!disable_video) {
        g_signal_connect(recv_session_video, "on-new-candidate", G_CALLBACK(got_candidate), send_session_video);
        g_signal_connect(send_session_video, "on-new-candidate", G_CALLBACK(got_candidate), recv_session_video);
        if (remote_addr) {
            g_signal_connect(recv_session_video, "on-candidate-gathering-done", G_CALLBACK(gathering_done), send_session_video);
            g_signal_connect(send_session_video, "on-candidate-gathering-done", G_CALLBACK(gathering_done), recv_session_video);
	    owr_session_set_local_port(OWR_SESSION(send_session_video), OWR_COMPONENT_TYPE_RTP, 5120);
	    owr_session_set_local_port(OWR_SESSION(send_session_video), OWR_COMPONENT_TYPE_RTCP, 5121);
	    owr_session_set_local_port(OWR_SESSION(recv_session_video), OWR_COMPONENT_TYPE_RTP, 5122);
	    owr_session_set_local_port(OWR_SESSION(recv_session_video), OWR_COMPONENT_TYPE_RTCP, 5123);
        }
    }
    if (!disable_audio) {
        g_signal_connect(recv_session_audio, "on-new-candidate", G_CALLBACK(got_candidate), send_session_audio);
        g_signal_connect(send_session_audio, "on-new-candidate", G_CALLBACK(got_candidate), recv_session_audio);
        if (remote_addr) {
            g_signal_connect(recv_session_audio, "on-candidate-gathering-done", G_CALLBACK(gathering_done), send_session_audio);
            g_signal_connect(send_session_audio, "on-candidate-gathering-done", G_CALLBACK(gathering_done), recv_session_audio);
	    owr_session_set_local_port(OWR_SESSION(send_session_audio), OWR_COMPONENT_TYPE_RTP, 5124);
	    owr_session_set_local_port(OWR_SESSION(send_session_audio), OWR_COMPONENT_TYPE_RTCP, 5125);
	    owr_session_set_local_port(OWR_SESSION(recv_session_audio), OWR_COMPONENT_TYPE_RTP, 5126);
	    owr_session_set_local_port(OWR_SESSION(recv_session_audio), OWR_COMPONENT_TYPE_RTCP, 5127);
        }
    }

    // VIDEO
    if (!disable_video) {
        g_signal_connect(recv_session_video, "on-incoming-source", G_CALLBACK(got_remote_source), NULL);

        receive_payload = owr_video_payload_new(OWR_CODEC_TYPE_VP8, 103, 90000, TRUE, FALSE);
        g_object_set(receive_payload, "rtx-payload-type", 123, NULL);
        if (adaptation)
            g_object_set(receive_payload, "adaptation", TRUE, NULL);

        owr_media_session_add_receive_payload(recv_session_video, receive_payload);
    }

    // AUDIO
    if (!disable_audio) {
        g_signal_connect(recv_session_audio, "on-incoming-source", G_CALLBACK(got_remote_source), NULL);

        receive_payload = owr_audio_payload_new(OWR_CODEC_TYPE_OPUS, 100, 48000, 1);
        owr_media_session_add_receive_payload(recv_session_audio, receive_payload);
    }

    /* PREPARE FOR SENDING */

    if (!uri) {
        owr_get_capture_sources(
                (!disable_video ? OWR_MEDIA_TYPE_VIDEO : 0) | (!disable_audio ? OWR_MEDIA_TYPE_AUDIO : 0),
                got_sources, NULL);
    } else {
        uri_source_agent = owr_uri_source_agent_new(uri);
        g_signal_connect(uri_source_agent, "on-new-source", G_CALLBACK(on_new_source), NULL);
        owr_uri_source_agent_play(uri_source_agent);
    }

    g_timeout_add_seconds(10, (GSourceFunc)dump_cb, NULL);

    owr_run();

    g_free(remote_addr);
    g_free(uri);

    return 0;
}
