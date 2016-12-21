/*
 * Copyright (C) 2015 Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr.h"
#include "owr_audio_payload.h"
#include "owr_audio_renderer.h"
#include "owr_local.h"
#include "owr_media_session.h"
#include "owr_transport_agent.h"
#include "owr_video_payload.h"
#include "owr_video_renderer.h"

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <string.h>

#define SERVER_URL "http://demo.openwebrtc.org"

#define ENABLE_PCMA TRUE
#define ENABLE_PCMU TRUE
#define ENABLE_OPUS TRUE
#define ENABLE_H264 TRUE
#define ENABLE_VP8  TRUE

static GList *local_sources, *renderers;
static OwrTransportAgent *transport_agent;
static gchar *session_id, *peer_id;
static guint client_id;
static gchar *candidate_types[] = { "host", "srflx", "relay", NULL };
static gchar *tcp_types[] = { "", "active", "passive", "so", NULL };

static void read_eventstream_line(GDataInputStream *input_stream, gpointer user_data);
static void got_local_sources(GList *sources, gchar *url);

static void got_remote_source(OwrMediaSession *media_session, OwrMediaSource *source,
    gpointer user_data)
{
    OwrMediaType media_type;
    gchar *name = NULL;
    OwrMediaRenderer *renderer;

    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));
    g_return_if_fail(!user_data);

    g_object_get(source, "media-type", &media_type, "name", &name, NULL);
    g_message("Got remote source: %s", name);

    if (media_type == OWR_MEDIA_TYPE_AUDIO)
        renderer = OWR_MEDIA_RENDERER(owr_audio_renderer_new());
    else if (media_type == OWR_MEDIA_TYPE_VIDEO)
        renderer = OWR_MEDIA_RENDERER(owr_video_renderer_new(NULL));
    else
        g_return_if_reached();

    owr_media_renderer_set_source(renderer, source);
    renderers = g_list_append(renderers, renderer);
}

static gboolean can_send_answer()
{
    GObject *media_session;
    GList *media_sessions, *item;

    media_sessions = g_object_get_data(G_OBJECT(transport_agent), "media-sessions");
    for (item = media_sessions; item; item = item->next) {
        media_session = G_OBJECT(item->data);
        if (!GPOINTER_TO_UINT(g_object_get_data(media_session, "gathering-done"))
            || !g_object_get_data(media_session, "fingerprint"))
            return FALSE;
    }

    return TRUE;
}

static void answer_sent(SoupSession *soup_session, GAsyncResult *result, gpointer user_data)
{
    GInputStream *input_stream;
    g_return_if_fail(!user_data);

    input_stream = soup_session_send_finish(soup_session, result, NULL);
    if (!input_stream)
        g_warning("Failed to send answer to server");
    else
        g_object_unref(input_stream);
}

static void send_answer()
{
    JsonBuilder *builder;
    JsonGenerator *generator;
    JsonNode *root;
    GList *media_sessions, *item;
    GObject *media_session;
    gchar *media_type, *encoding_name;
    gboolean rtcp_mux;
    guint payload_type, clock_rate, channels;
    gboolean ccm_fir, nack_pli;
    GList *candidates, *list_item;
    OwrCandidate *candidate;
    gchar *ice_ufrag, *ice_password;
    gchar *fingerprint;
    gchar *json;
    gsize json_length;
    SoupSession *soup_session;
    SoupMessage *soup_message;
    gchar *url;

    builder = json_builder_new();
    generator = json_generator_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, "answer");
    json_builder_set_member_name(builder, "sessionDescription");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "mediaDescriptions");
    json_builder_begin_array(builder);

    media_sessions = g_object_get_data(G_OBJECT(transport_agent), "media-sessions");
    for (item = media_sessions; item; item = item->next) {
        media_session = G_OBJECT(item->data);
        json_builder_begin_object(builder);

        json_builder_set_member_name(builder, "type");
        media_type = g_object_steal_data(media_session, "media-type");
        json_builder_add_string_value(builder, media_type);

        json_builder_set_member_name(builder, "rtcp");
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "mux");
        g_object_get(media_session, "rtcp-mux", &rtcp_mux, NULL);
        json_builder_add_boolean_value(builder, rtcp_mux);
        json_builder_end_object(builder);

        json_builder_set_member_name(builder, "payloads");
        json_builder_begin_array(builder);

        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "encodingName");
        encoding_name = g_object_steal_data(media_session, "encoding-name");
        json_builder_add_string_value(builder, encoding_name);

        json_builder_set_member_name(builder, "type");
        payload_type = GPOINTER_TO_UINT(g_object_steal_data(media_session, "payload-type"));
        json_builder_add_int_value(builder, payload_type);

        json_builder_set_member_name(builder, "clockRate");
        clock_rate = GPOINTER_TO_UINT(g_object_steal_data(media_session, "clock-rate"));
        json_builder_add_int_value(builder, clock_rate);

        if (!g_strcmp0(media_type, "audio")) {
            json_builder_set_member_name(builder, "channels");
            channels = GPOINTER_TO_UINT(g_object_steal_data(media_session, "channels"));
            json_builder_add_int_value(builder, channels);
        } else if (!g_strcmp0(media_type, "video")) {
            json_builder_set_member_name(builder, "ccmfir");
            ccm_fir = GPOINTER_TO_UINT(g_object_steal_data(media_session, "ccm-fir"));
            json_builder_add_boolean_value(builder, ccm_fir);
            json_builder_set_member_name(builder, "nackpli");
            nack_pli = GPOINTER_TO_UINT(g_object_steal_data(media_session, "nack-pli"));
            json_builder_add_boolean_value(builder, nack_pli);

            if (!g_strcmp0(encoding_name, "H264")) {
                json_builder_set_member_name(builder, "parameters");
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "levelAsymmetryAllowed");
                json_builder_add_int_value(builder, 1);
                json_builder_set_member_name(builder, "packetizationMode");
                json_builder_add_int_value(builder, 1);
                json_builder_set_member_name(builder, "profileLevelId");
                json_builder_add_string_value(builder, "42e01f");
                json_builder_end_object(builder);
            }
        } else
            g_warn_if_reached();

        json_builder_end_object(builder);
        json_builder_end_array(builder); /* payloads */

        json_builder_set_member_name(builder, "ice");
        json_builder_begin_object(builder);
        candidates = g_object_steal_data(media_session, "local-candidates");
        candidate = OWR_CANDIDATE(candidates->data);
        g_object_get(candidate, "ufrag", &ice_ufrag, "password", &ice_password, NULL);
        json_builder_set_member_name(builder, "ufrag");
        json_builder_add_string_value(builder, ice_ufrag);
        json_builder_set_member_name(builder, "password");
        json_builder_add_string_value(builder, ice_password);
        json_builder_set_member_name(builder, "candidates");
        json_builder_begin_array(builder);
        for (list_item = candidates; list_item; list_item = list_item->next) {
            OwrCandidateType candidate_type;
            OwrComponentType component_type;
            OwrTransportType transport_type;
            gchar *foundation, *address, *related_address;
            gint port, priority, related_port;
            candidate = OWR_CANDIDATE(list_item->data);
            g_object_get(candidate, "type", &candidate_type, "component-type", &component_type,
                "foundation", &foundation, "transport-type", &transport_type, "address", &address,
                "port", &port, "priority", &priority, "base-address", &related_address,
                "base-port", &related_port, NULL);
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "foundation");
            json_builder_add_string_value(builder, foundation);
            json_builder_set_member_name(builder, "componentId");
            json_builder_add_int_value(builder, component_type);
            json_builder_set_member_name(builder, "transport");
            if (transport_type == OWR_TRANSPORT_TYPE_UDP)
                json_builder_add_string_value(builder, "UDP");
            else
                json_builder_add_string_value(builder, "TCP");
            json_builder_set_member_name(builder, "priority");
            json_builder_add_int_value(builder, priority);
            json_builder_set_member_name(builder, "address");
            json_builder_add_string_value(builder, address);
            json_builder_set_member_name(builder, "port");
            json_builder_add_int_value(builder, port);
            json_builder_set_member_name(builder, "type");
            json_builder_add_string_value(builder, candidate_types[candidate_type]);
            if (candidate_type != OWR_CANDIDATE_TYPE_HOST) {
                json_builder_set_member_name(builder, "relatedAddress");
                json_builder_add_string_value(builder, related_address);
                json_builder_set_member_name(builder, "relatedPort");
                json_builder_add_int_value(builder, related_port);
            }
            if (transport_type != OWR_TRANSPORT_TYPE_UDP) {
                json_builder_set_member_name(builder, "tcpType");
                json_builder_add_string_value(builder, tcp_types[transport_type]);
            }
            json_builder_end_object(builder);
            g_free(foundation);
            g_free(address);
            g_free(related_address);
        }
        g_list_free(candidates);
        json_builder_end_array(builder); /* candidates */
        json_builder_end_object(builder); /* ice */

        json_builder_set_member_name(builder, "dtls");
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "fingerprintHashFunction");
        json_builder_add_string_value(builder, "sha-256");
        json_builder_set_member_name(builder, "fingerprint");
        fingerprint = g_object_steal_data(media_session, "fingerprint");
        json_builder_add_string_value(builder, fingerprint);
        json_builder_set_member_name(builder, "setup");
        json_builder_add_string_value(builder, "active");
        json_builder_end_object(builder); /* dtls */

        json_builder_end_object(builder);

        g_free(fingerprint);
        g_free(ice_password);
        g_free(ice_ufrag);
        g_free(encoding_name);
        g_free(media_type);
    }
    json_builder_end_array(builder); /* mediaDescriptions */
    json_builder_end_object(builder); /* sessionDescription */
    json_builder_end_object(builder); /* root */

    json_generator_set_pretty(generator, TRUE);
    root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json = json_generator_to_data(generator, &json_length);
    json_node_free(root);
    g_object_unref(builder);
    g_object_unref(generator);

    url = g_strdup_printf(SERVER_URL"/ctos/%s/%u/%s", session_id, client_id, peer_id);
    soup_session = soup_session_new();
    soup_message = soup_message_new("POST", url);
    g_free(url);
    soup_message_set_request(soup_message, "application/json",
        SOUP_MEMORY_TAKE, json, json_length);
    soup_session_send_async(soup_session, soup_message, NULL,
        (GAsyncReadyCallback)answer_sent, NULL);
}

static void got_candidate(GObject *media_session, OwrCandidate *candidate, gpointer user_data)
{
    GList *local_candidates;
    g_return_if_fail(!user_data);

    local_candidates = g_object_get_data(media_session, "local-candidates");
    local_candidates = g_list_append(local_candidates, candidate);
    g_object_set_data(media_session, "local-candidates", local_candidates);
}

static void candidate_gathering_done(GObject *media_session, gpointer user_data)
{
    g_return_if_fail(!user_data);
    g_object_set_data(media_session, "gathering-done", GUINT_TO_POINTER(1));
    if (can_send_answer())
        send_answer();
}

static void got_dtls_certificate(GObject *media_session, GParamSpec *pspec, gpointer user_data)
{
    guint i;
    gchar *pem, *line;
    guchar *der, *tmp;
    gchar **lines;
    gint state = 0;
    guint save = 0;
    gsize der_length = 0;
    GChecksum *checksum;
    guint8 *digest;
    gsize digest_length;
    GString *fingerprint;

    g_return_if_fail(G_IS_PARAM_SPEC(pspec));
    g_return_if_fail(!user_data);

    g_object_get(media_session, "dtls-certificate", &pem, NULL);
    der = tmp = g_new0(guchar, (strlen(pem) / 4) * 3 + 3);
    lines = g_strsplit(pem, "\n", 0);
    for (i = 0, line = lines[i]; line; line = lines[++i]) {
        if (line[0] && !g_str_has_prefix(line, "-----"))
            tmp += g_base64_decode_step(line, strlen(line), tmp, &state, &save);
    }
    der_length = tmp - der;
    checksum = g_checksum_new(G_CHECKSUM_SHA256);
    digest_length = g_checksum_type_get_length(G_CHECKSUM_SHA256);
    digest = g_new(guint8, digest_length);
    g_checksum_update(checksum, der, der_length);
    g_checksum_get_digest(checksum, digest, &digest_length);
    fingerprint = g_string_new(NULL);
    for (i = 0; i < digest_length; i++) {
        if (i)
            g_string_append(fingerprint, ":");
        g_string_append_printf(fingerprint, "%02X", digest[i]);
    }
    g_object_set_data(media_session, "fingerprint", g_string_free(fingerprint, FALSE));

    g_free(digest);
    g_checksum_free(checksum);
    g_free(der);
    g_strfreev(lines);

    if (can_send_answer())
        send_answer();
}

static OwrCandidate * candidate_from_positioned_json_reader(JsonReader *reader)
{
    OwrCandidate *remote_candidate;
    OwrCandidateType candidate_type;
    OwrComponentType component_type;
    OwrTransportType transport_type;
    const gchar *cand_type, *foundation, *transport, *address, *tcp_type;
    gint priority, port;

    json_reader_read_member(reader, "type");
    cand_type = json_reader_get_string_value(reader);
    if (!g_strcmp0(cand_type, "host"))
        candidate_type = OWR_CANDIDATE_TYPE_HOST;
    else if (!g_strcmp0(cand_type, "srflx"))
        candidate_type = OWR_CANDIDATE_TYPE_SERVER_REFLEXIVE;
    else
        candidate_type = OWR_CANDIDATE_TYPE_RELAY;
    json_reader_end_member(reader);

    json_reader_read_member(reader, "componentId");
    component_type = (OwrComponentType)json_reader_get_int_value(reader);
    json_reader_end_member(reader);

    remote_candidate = owr_candidate_new(candidate_type, component_type);

    json_reader_read_member(reader, "foundation");
    foundation = json_reader_get_string_value(reader);
    g_object_set(remote_candidate, "foundation", foundation, NULL);
    json_reader_end_member(reader);

    json_reader_read_member(reader, "transport");
    transport = json_reader_get_string_value(reader);
    if (!g_strcmp0(transport, "UDP"))
        transport_type = OWR_TRANSPORT_TYPE_UDP;
    else
        transport_type = OWR_TRANSPORT_TYPE_TCP_ACTIVE;
    json_reader_end_member(reader);

    if (transport_type != OWR_TRANSPORT_TYPE_UDP) {
        json_reader_read_member(reader, "tcpType");
        tcp_type = json_reader_get_string_value(reader);
        if (!g_strcmp0(tcp_type, "active"))
            transport_type = OWR_TRANSPORT_TYPE_TCP_ACTIVE;
        else if (!g_strcmp0(tcp_type, "passive"))
            transport_type = OWR_TRANSPORT_TYPE_TCP_PASSIVE;
        else
            transport_type = OWR_TRANSPORT_TYPE_TCP_SO;
        json_reader_end_member(reader);
    }
    g_object_set(remote_candidate, "transport-type", transport_type, NULL);

    json_reader_read_member(reader, "address");
    address = json_reader_get_string_value(reader);
    g_object_set(remote_candidate, "address", address, NULL);
    json_reader_end_member(reader);

    json_reader_read_member(reader, "port");
    port = json_reader_get_int_value(reader);
    g_object_set(remote_candidate, "port", port, NULL);
    json_reader_end_member(reader);

    json_reader_read_member(reader, "priority");
    priority = json_reader_get_int_value(reader);
    g_object_set(remote_candidate, "priority", priority, NULL);
    json_reader_end_member(reader);

    return remote_candidate;
}

static void handle_offer(JsonReader *reader)
{
    gint i, number_of_media_descriptions;
    const gchar *mtype;
    OwrMediaType media_type = OWR_MEDIA_TYPE_UNKNOWN;
    gboolean rtcp_mux;
    OwrMediaSession *media_session;
    GObject *session;
    gint j, number_of_payloads, number_of_candidates;
    gint64 payload_type, clock_rate, channels = 0;
    gchar *encoding_name;
    gboolean ccm_fir = FALSE, nack_pli = FALSE;
    OwrCodecType codec_type;
    OwrCandidate *remote_candidate;
    OwrComponentType component_type;
    const gchar *ice_ufrag, *ice_password;
    OwrPayload *send_payload, *receive_payload;
    GList *list_item;
    OwrMediaSource *source;
    OwrMediaType source_media_type;
    GList *media_sessions;

    json_reader_read_member(reader, "mediaDescriptions");
    number_of_media_descriptions = json_reader_count_elements(reader);
    for (i = 0; i < number_of_media_descriptions; i++) {
        json_reader_read_element(reader, i);
        media_session = owr_media_session_new(TRUE);
        session = G_OBJECT(media_session);

        json_reader_read_member(reader, "type");
        mtype = json_reader_get_string_value(reader);
        g_object_set_data(session, "media-type", g_strdup(mtype));
        json_reader_end_member(reader);

        json_reader_read_member(reader, "rtcp");
        json_reader_read_member(reader, "mux");
        rtcp_mux = json_reader_get_boolean_value(reader);
        g_object_set(media_session, "rtcp-mux", rtcp_mux, NULL);
        json_reader_end_member(reader);
        json_reader_end_member(reader);

        json_reader_read_member(reader, "payloads");
        number_of_payloads = json_reader_count_elements(reader);
        codec_type = OWR_CODEC_TYPE_NONE;
        for (j = 0; j < number_of_payloads && codec_type == OWR_CODEC_TYPE_NONE; j++) {
            json_reader_read_element(reader, j);

            json_reader_read_member(reader, "encodingName");
            encoding_name = g_ascii_strup(json_reader_get_string_value(reader), -1);
            json_reader_end_member(reader);

            json_reader_read_member(reader, "type");
            payload_type = json_reader_get_int_value(reader);
            json_reader_end_member(reader);

            json_reader_read_member(reader, "clockRate");
            clock_rate = json_reader_get_int_value(reader);
            json_reader_end_member(reader);

            send_payload = receive_payload = NULL;
            if (!g_strcmp0(mtype, "audio")) {
                media_type = OWR_MEDIA_TYPE_AUDIO;
                if (ENABLE_PCMA && !g_strcmp0(encoding_name, "PCMA"))
                    codec_type = OWR_CODEC_TYPE_PCMA;
                else if (ENABLE_PCMU && !g_strcmp0(encoding_name, "PCMU"))
                    codec_type = OWR_CODEC_TYPE_PCMU;
                else if (ENABLE_OPUS && !g_strcmp0(encoding_name, "OPUS"))
                    codec_type = OWR_CODEC_TYPE_OPUS;
                else
                    goto end_payload;

                json_reader_read_member(reader, "channels");
                channels = json_reader_get_int_value(reader);
                json_reader_end_member(reader);

                send_payload = owr_audio_payload_new(codec_type, payload_type, clock_rate,
                    channels);
                receive_payload = owr_audio_payload_new(codec_type, payload_type, clock_rate,
                    channels);
            } else if (!g_strcmp0(mtype, "video")) {
                media_type = OWR_MEDIA_TYPE_VIDEO;
                if (ENABLE_H264 && !g_strcmp0(encoding_name, "H264"))
                    codec_type = OWR_CODEC_TYPE_H264;
                else if (ENABLE_VP8 && !g_strcmp0(encoding_name, "VP8"))
                    codec_type = OWR_CODEC_TYPE_VP8;
                else
                    goto end_payload;

                json_reader_read_member(reader, "ccmfir");
                ccm_fir = json_reader_get_boolean_value(reader);
                json_reader_end_member(reader);
                json_reader_read_member(reader, "nackpli");
                nack_pli = json_reader_get_boolean_value(reader);
                json_reader_end_member(reader);

                send_payload = owr_video_payload_new(codec_type, payload_type, clock_rate,
                    ccm_fir, nack_pli);
                receive_payload = owr_video_payload_new(codec_type, payload_type, clock_rate,
                    ccm_fir, nack_pli);
            } else
                g_warn_if_reached();

            if (send_payload && receive_payload) {
                g_object_set_data(session, "encoding-name", g_strdup(encoding_name));
                g_object_set_data(session, "payload-type", GUINT_TO_POINTER(payload_type));
                g_object_set_data(session, "clock-rate", GUINT_TO_POINTER(clock_rate));
                if (OWR_IS_AUDIO_PAYLOAD(send_payload))
                    g_object_set_data(session, "channels", GUINT_TO_POINTER(channels));
                else if (OWR_IS_VIDEO_PAYLOAD(send_payload)) {
                    g_object_set_data(session, "ccm-fir", GUINT_TO_POINTER(ccm_fir));
                    g_object_set_data(session, "nack-pli", GUINT_TO_POINTER(nack_pli));
                } else
                    g_warn_if_reached();

                owr_media_session_add_receive_payload(media_session, receive_payload);
                owr_media_session_set_send_payload(media_session, send_payload);
            }
end_payload:
            g_free(encoding_name);
            json_reader_end_element(reader);
        }
        json_reader_end_member(reader); /* payloads */

        json_reader_read_member(reader, "ice");
        json_reader_read_member(reader, "ufrag");
        ice_ufrag = json_reader_get_string_value(reader);
        g_object_set_data_full(session, "remote-ice-ufrag", g_strdup(ice_ufrag), g_free);
        json_reader_end_member(reader);
        json_reader_read_member(reader, "password");
        ice_password = json_reader_get_string_value(reader);
        g_object_set_data_full(session, "remote-ice-password", g_strdup(ice_password), g_free);
        json_reader_end_member(reader);
        if (json_reader_read_member(reader, "candidates")) {
            number_of_candidates = json_reader_count_elements(reader);
            for (j = 0; j < number_of_candidates; j++) {
                json_reader_read_element(reader, j);
                remote_candidate = candidate_from_positioned_json_reader(reader);
                g_object_set(remote_candidate, "ufrag", ice_ufrag, "password", ice_password, NULL);
                g_object_get(remote_candidate, "component-type", &component_type, NULL);
                if (!rtcp_mux || component_type != OWR_COMPONENT_TYPE_RTCP)
                    owr_session_add_remote_candidate(OWR_SESSION(media_session), remote_candidate);
                else
                    g_object_unref(remote_candidate);
                json_reader_end_element(reader);
            }
        }
        json_reader_end_member(reader); /* candidates */
        json_reader_end_member(reader); /* ice */

        json_reader_end_element(reader);

        g_signal_connect(media_session, "on-incoming-source", G_CALLBACK(got_remote_source), NULL);
        g_signal_connect(media_session, "on-new-candidate", G_CALLBACK(got_candidate), NULL);
        g_signal_connect(media_session, "on-candidate-gathering-done",
            G_CALLBACK(candidate_gathering_done), NULL);
        g_signal_connect(media_session, "notify::dtls-certificate",
            G_CALLBACK(got_dtls_certificate), NULL);

        for (list_item = local_sources; list_item; list_item = list_item->next) {
            source = OWR_MEDIA_SOURCE(list_item->data);
            g_object_get(source, "media-type", &source_media_type, NULL);
            if (source_media_type == media_type) {
                local_sources = g_list_remove(local_sources, source);
                owr_media_session_set_send_source(media_session, source);
                break;
            }
        }
        media_sessions = g_object_get_data(G_OBJECT(transport_agent), "media-sessions");
        media_sessions = g_list_append(media_sessions, media_session);
        g_object_set_data(G_OBJECT(transport_agent), "media-sessions", media_sessions);
        owr_transport_agent_add_session(transport_agent, OWR_SESSION(media_session));
    }
    json_reader_end_member(reader);
}

static void handle_remote_candidate(JsonReader *reader)
{
    gint index;
    GList *media_sessions;
    OwrMediaSession *media_session;
    OwrCandidate *remote_candidate;
    OwrComponentType component_type;
    gboolean rtcp_mux;
    gchar *ice_ufrag, *ice_password;

    json_reader_read_member(reader, "sdpMLineIndex");
    index = json_reader_get_int_value(reader);
    media_sessions = g_object_get_data(G_OBJECT(transport_agent), "media-sessions");
    media_session = OWR_MEDIA_SESSION(g_list_nth_data(media_sessions, index));
    json_reader_end_member(reader);
    if (!media_session)
        return;

    json_reader_read_member(reader, "candidateDescription");
    remote_candidate = candidate_from_positioned_json_reader(reader);
    json_reader_end_member(reader);

    ice_ufrag = g_object_get_data(G_OBJECT(media_session), "remote-ice-ufrag");
    ice_password = g_object_get_data(G_OBJECT(media_session), "remote-ice-password");
    g_object_set(remote_candidate, "ufrag", ice_ufrag, "password", ice_password, NULL);
    g_object_get(media_session, "rtcp-mux", &rtcp_mux, NULL);
    g_object_get(remote_candidate, "component-type", &component_type, NULL);
    if (!rtcp_mux || component_type != OWR_COMPONENT_TYPE_RTCP)
        owr_session_add_remote_candidate(OWR_SESSION(media_session), remote_candidate);
}

static void reset()
{
    GList *media_sessions, *item;
    OwrMediaRenderer *renderer;
    OwrMediaSession *media_session;

    if (renderers) {
        for (item = renderers; item; item = item->next) {
            renderer = OWR_MEDIA_RENDERER(item->data);
            owr_media_renderer_set_source(renderer, NULL);
        }
        g_list_free_full(renderers, g_object_unref);
        renderers = NULL;
    }
    if (transport_agent) {
        media_sessions = g_object_steal_data(G_OBJECT(transport_agent), "media-sessions");
        for (item = media_sessions; item; item = item->next) {
            media_session = OWR_MEDIA_SESSION(item->data);
            owr_media_session_set_send_source(media_session, NULL);
        }
        g_list_free(media_sessions);
        g_object_unref(transport_agent);
        transport_agent = NULL;
    }

    g_list_free(local_sources);
    local_sources = NULL;
    owr_get_capture_sources(OWR_MEDIA_TYPE_AUDIO | OWR_MEDIA_TYPE_VIDEO,
        (OwrCaptureSourcesCallback)got_local_sources, NULL);
}

static void eventstream_line_read(GDataInputStream *input_stream, GAsyncResult *result,
    GString *buffer)
{
    gchar *line, *pos = NULL;
    gsize line_length;
    JsonParser *parser;
    JsonReader *reader;

    line = g_data_input_stream_read_line_finish_utf8(input_stream, result, &line_length, NULL);
    if (line) {
        if (line_length) {
            if (g_strstr_len(line, MIN(line_length, 6), "event:"))
                pos = line;
            else if (g_strstr_len(line, MIN(line_length, 5), "data:"))
                pos = line + 5;
            if (pos) {
                if (!buffer)
                    buffer = g_string_new_len(pos, line_length - (pos - line));
                else
                    g_string_append_len(buffer, pos, line_length - (pos - line));
                g_string_append_c(buffer, '\n');
            }
        } else if (buffer) {
            if (g_str_has_prefix(buffer->str, "event:user-")) {
                pos = g_strstr_len(buffer->str, buffer->len, "\n");
                parser = json_parser_new();
                if (json_parser_load_from_data(parser, pos, strlen(pos), NULL)) {
                    reader = json_reader_new(json_parser_get_root(parser));
                    if (json_reader_read_member(reader, "sessionDescription"))
                        handle_offer(reader);
                    json_reader_end_member(reader);
                    if (json_reader_read_member(reader, "candidate"))
                        handle_remote_candidate(reader);
                    json_reader_end_member(reader);
                    g_object_unref(reader);
                }
                g_object_unref(parser);
            } else if (g_str_has_prefix(buffer->str, "event:join\n")) {
                g_free(peer_id);
                pos = g_strstr_len(buffer->str + 11, buffer->len - 11, "\n");
                if (pos) {
                    peer_id = g_strndup(buffer->str + 11, pos - buffer->str - 11);
                    g_message("Peer joined: %s", peer_id);
                } else
                    peer_id = NULL;
            } else if (g_str_has_prefix(buffer->str, "event:leave\n")) {
                g_message("Peer left");
                g_free(peer_id);
                peer_id = NULL;
                reset();
            }

            g_string_free(buffer, TRUE);
            buffer = NULL;
        }

        g_free(line);
    }

    read_eventstream_line(input_stream, buffer);
}

static void read_eventstream_line(GDataInputStream *input_stream, gpointer user_data)
{
    g_data_input_stream_read_line_async(input_stream, G_PRIORITY_DEFAULT, NULL,
        (GAsyncReadyCallback)eventstream_line_read, user_data);
}

static void eventsource_request_sent(SoupSession *soup_session, GAsyncResult *result,
    gpointer user_data)
{
    GInputStream *input_stream;
    GDataInputStream *data_input_stream;

    input_stream = soup_session_send_finish(soup_session, result, NULL);
    if (input_stream) {
        data_input_stream = g_data_input_stream_new(input_stream);
        read_eventstream_line(data_input_stream, user_data);
    } else
        g_warning("Failed to connect to the server");
}

static void send_eventsource_request(const gchar *url)
{
    SoupSession *soup_session;
    SoupMessage *soup_message;

    soup_session = soup_session_new();
    soup_message = soup_message_new("GET", url);
    soup_session_send_async(soup_session, soup_message, NULL,
        (GAsyncReadyCallback)eventsource_request_sent, NULL);
}

static void got_local_sources(GList *sources, gchar *url)
{
    local_sources = g_list_copy(sources);
    transport_agent = owr_transport_agent_new(FALSE);
    owr_transport_agent_add_helper_server(transport_agent, OWR_HELPER_SERVER_TYPE_STUN,
        "stun.services.mozilla.com", 3478, NULL, NULL);
    if (url) {
        send_eventsource_request(url);
        g_free(url);
    }
}

gint main(gint argc, gchar **argv)
{
    gchar *url;

    if (argc < 2) {
        g_print("Usage: %s <session id>\n", argv[0]);
        return -1;
    }

    session_id = argv[1];
    client_id = g_random_int();
    url = g_strdup_printf(SERVER_URL"/stoc/%s/%u", session_id, client_id);
    owr_init(NULL);
    owr_get_capture_sources(OWR_MEDIA_TYPE_AUDIO | OWR_MEDIA_TYPE_VIDEO,
        (OwrCaptureSourcesCallback)got_local_sources, url);
    owr_run();
    return 0;
}
