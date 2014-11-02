/*
 * Copyright (C) 2014 Ericsson AB. All rights reserved.
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

"use strict";

function PeerHandler(configuration, client, jsonRpc) {

    var transportAgent;
    var mediaSessions = [];
    var numberOfReceivePreparedMediaSessions = 0;
    var numberOfSendPreparedMediaSessions = 0;

    this.prepareToReceive = function (localSessionInfo, isInitiator) {
        var dtlsRoles = [];
        for (var i = mediaSessions.length; i < localSessionInfo.mediaDescriptions.length; i++)
            dtlsRoles.push(localSessionInfo.mediaDescriptions[i].dtls.mode);

        ensureTransportAgentAndMediaSessions(isInitiator, dtlsRoles);

        for (var i = numberOfReceivePreparedMediaSessions; i < mediaSessions.length; i++)
            prepareMediaSession(mediaSessions[i], localSessionInfo.mediaDescriptions[i]);

        numberOfReceivePreparedMediaSessions = mediaSessions.length;

        function prepareMediaSession(mediaSession, mdesc) {
            mediaSession.rtcp_mux = !isInitiator && !!(mdesc.rtcp && mdesc.rtcp.mux);

            mediaSession.signal.connect("notify::send-ssrc", function () {
                var mdescIndex = localSessionInfo.mediaDescriptions.indexOf(mdesc);
                client.gotSendSSRC(mdescIndex, mediaSession.send_ssrc, mediaSession.cname);
            });

            mediaSession.signal.connect("notify::dtls-certificate", function () {
                var der = atob(mediaSession.dtls_certificate.split(/\r?\n/).slice(1, -2).join(""));
                var buf = new ArrayBuffer(der.length);
                var bufView = new Uint8Array(buf);
                for (var i = 0; i < der.length; i++)
                    bufView[i] = der.charCodeAt(i);

                crypto.subtle.digest("sha-256", buf).then(function (digest) {
                    var fingerprint = "";
                    var bufView = new Uint8Array(digest);
                    for (var i = 0; i < bufView.length; i++) {
                        if (fingerprint)
                            fingerprint += ":";
                        fingerprint += ("0" + bufView[i].toString(16)).substr(-2);
                    }

                    var dtlsInfo = {
                        "fingerprintHashFunction": "sha-256",
                        "fingerprint": fingerprint.toUpperCase()
                    };
                    var mdescIndex = localSessionInfo.mediaDescriptions.indexOf(mdesc);
                    client.gotDtlsFingerprint(mdescIndex, dtlsInfo);
                });
            });

            mediaSession.signal.on_new_candidate.connect(function (m, candidate) {
                var cand = {
                    "type": ["host", "srflx", "prflx", "relay"][candidate.type],
                    "foundation": candidate.foundation,
                    "componentId": candidate.component_type,
                    "transport": candidate.transport_type == owr.TransportType.UDP ? "UDP" : "TCP",
                    "priority": candidate.priority,
                    "address": candidate.address,
                    "port": candidate.port || 9,
                    "tcpType": [ null, "active", "passive", "so" ][candidate.transport_type]
                };

                if (cand.type != "host") {
                    cand.relatedAddress = candidate.base_address;
                    cand.relatedPort = candidate.base_port || 9;
                }

                var mdescIndex = localSessionInfo.mediaDescriptions.indexOf(mdesc);
                client.gotIceCandidate(mdescIndex, cand, candidate.ufrag, candidate.password);
            });

            mediaSession.signal.on_candidate_gathering_done.connect(function () {
                var mdescIndex = localSessionInfo.mediaDescriptions.indexOf(mdesc);
                client.candidateGatheringDone(mdescIndex);
            });

            mediaSession.signal.on_incoming_source.connect(function (m, remoteSource) {
                var mdescIndex = localSessionInfo.mediaDescriptions.indexOf(mdesc);
                client.gotRemoteSource(mdescIndex, jsonRpc.createObjectRef(remoteSource));
            });

            mdesc.payloads.forEach(function (payload) {
                var receivePayload = (mdesc.type == "audio") ?
                    new owr.AudioPayload({
                        "payload_type": payload.type,
                        "codec_type": owr.CodecType[payload.encodingName.toUpperCase()],
                        "clock_rate": payload.clockRate,
                        "channels": payload.channels
                    }) :
                    new owr.VideoPayload({
                        "payload_type": payload.type,
                        "codec_type": owr.CodecType[payload.encodingName.toUpperCase()],
                        "clock_rate": payload.clockRate,
                        "ccm_fir": payload.ccmfir,
                        "nack_pli": payload.nackpli
                    });
                mediaSession.add_receive_payload(receivePayload);
            });

            transportAgent.add_session(mediaSession);
        }
    };

    this.prepareToSend = function (remoteSessionInfo, isInitiator) {
        var i;
        var dtlsRoles = [];
        for (i = mediaSessions.length; i < remoteSessionInfo.mediaDescriptions.length; i++) {
            var ourRole = remoteSessionInfo.mediaDescriptions[i].dtls.mode == "active" ?
                "passive" : "active";
            dtlsRoles.push(ourRole);
        }

        ensureTransportAgentAndMediaSessions(isInitiator, dtlsRoles);

        for (i = 0; i < mediaSessions.length; i++) {
            var mediaSession = mediaSessions[i];
            var mdesc = remoteSessionInfo.mediaDescriptions[i];
            if (!mdesc)
                continue;

            mediaSession.rtcp_mux = !!(mdesc.rtcp && mdesc.rtcp.mux);

            if (mdesc.ice && mdesc.ice.candidates) {
                mdesc.ice.candidates.forEach(function (candidate) {
                    internalAddRemoteCandidate(mediaSession, candidate,
                        mdesc.ice.ufrag, mdesc.ice.password);
                });
            }

            if (!mdesc.source || i < numberOfSendPreparedMediaSessions)
                continue;

            var sendPayload = (mdesc.type == "audio") ?
                new owr.AudioPayload({
                    "payload_type": mdesc.payloads[0].type,
                    "codec_type": owr.CodecType[mdesc.payloads[0].encodingName.toUpperCase()],
                    "clock_rate": mdesc.payloads[0].clockRate,
                    "channels": mdesc.payloads[0].channels
                }) :
                new owr.VideoPayload({
                    "payload_type": mdesc.payloads[0].type,
                    "codec_type": owr.CodecType[mdesc.payloads[0].encodingName.toUpperCase()],
                    "clock_rate": mdesc.payloads[0].clockRate,
                    "ccm_fir": !!mdesc.payloads[0].ccmfir,
                    "nack_pli": !!mdesc.payloads[0].nackpli
                });
            mediaSession.set_send_payload(sendPayload);
            mediaSession.set_send_source(mdesc.source);
            numberOfSendPreparedMediaSessions = i + 1;
        }
    }

    this.addRemoteCandidate = function (candidate, mediaSessionIndex, ufrag, password) {
        internalAddRemoteCandidate(mediaSessions[mediaSessionIndex], candidate, ufrag, password);
    };

    this.stop = function () {
        console.log("PeerHandler.stop() called (not implemented)");
    };

    function internalAddRemoteCandidate(mediaSession, candidate, ufrag, password) {
        if (mediaSession.rtcp_mux && candidate.componentId == owr.ComponentType.RTCP)
            return;

        var transportType = candidate.tcpType ?
            owr.TransportType["TCP_" + candidate.tcpType.toUpperCase()] : owr.TransportType.UDP;
        mediaSession.add_remote_candidate(new owr.Candidate({
            "type": ["host", "srflx", "prflx", "relay"].indexOf(candidate.type),
            "component_type": candidate.componentId,
            "transport_type": transportType,
            "address": candidate.address,
            "port": candidate.port,
            "base_address": candidate.relatedAddress || "",
            "base_port": candidate.relatedPort || 0,
            "priority": candidate.priority,
            "foundation": candidate.foundation,
            "ufrag": ufrag,
            "password": password
        }));
    }

    function ensureTransportAgentAndMediaSessions(isInitiator, newMediaSessionsDtlsRoles) {
        if (!transportAgent) {
            transportAgent = new owr.TransportAgent();

            configuration.iceServers.forEach(function (iceServer) {
                var urls = [];
                if (iceServer.urls instanceof Array)
                    Array.prototype.push.apply(urls, iceServer.urls);
                else if (iceServer.urls)
                    urls.push(iceServer.urls);
                if (iceServer.url)
                    urls.push(iceServer.url)

                urls.forEach(function (url) {
                    var urlParts = url.match(/^(stun|turn):([\w.\-]+):?(\d+)?(\?transport=(udp|tcp))?.*/);
                    if (!urlParts)
                        return;
                    var type;
                    if (urlParts[1] == "stun")
                        type = owr.HelperServerType.STUN;
                    else if (urlParts[5] == "tcp")
                        type = owr.HelperServerType.TURN_TCP;
                    else
                        type = owr.HelperServerType.TURN_UDP;
                    var address = urlParts[2];
                    var port = parseInt(urlParts[3]) || 3478;

                    transportAgent.add_helper_server(type, address, port,
                        iceServer.username || "", iceServer.credential || "");
                });
            });
        }
        transportAgent.ice_controlling_mode = isInitiator;

        newMediaSessionsDtlsRoles.forEach(function (role) {
            var mediaSession = new owr.MediaSession({
                "rtcp_mux": false,
                "dtls_client_mode": role == "active"
            });
            mediaSessions.push(mediaSession);
        });
    }
}
