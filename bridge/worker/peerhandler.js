/*
 * Copyright (C) 2014-2015 Ericsson AB. All rights reserved.
 * Copyright (C) 2015 Collabora Ltd.
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

function PeerHandler(configuration, keyCert, client, jsonRpc) {
    var transportAgent;
    var sessions = [];
    var numberOfReceivePreparedSessions = 0;
    var numberOfSendPreparedSessions = 0;
    var remoteSources = [];
    var dataSession;
    var nextDataChannelId;

    this.prepareToReceive = function (localSessionInfo, isInitiator) {
        var i;
        var sessionConfigs = [];
        for (i = sessions.length; i < localSessionInfo.mediaDescriptions.length; i++) {
            var lmdesc = localSessionInfo.mediaDescriptions[i];
            var sessionConfig = {
                "dtlsRole": lmdesc.dtls.setup,
                "type": lmdesc.type == "application" ? "data" : "media"
            };

            sessionConfigs.push(sessionConfig);
        }

        ensureTransportAgentAndSessions(isInitiator, sessionConfigs);

        for (i = numberOfReceivePreparedSessions; i < sessions.length; i++) {
            var lmdesc = localSessionInfo.mediaDescriptions[i];
            if (lmdesc.type == "application")
                prepareDataSession(sessions[i], lmdesc);
            else
                prepareMediaSession(sessions[i], lmdesc);

            transportAgent.add_session(sessions[i]);
        }

        numberOfReceivePreparedSessions = sessions.length;

        function prepareSession(session, mdesc) {

            session.dtls_certificate = keyCert.certificate;

            session.dtls_key = keyCert.key;

            session.signal.on_new_candidate.connect(function (m, candidate) {
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

                if (mdesc.ice && mdesc.ice.ufrag && mdesc.ice.password) {
                    candidate.ufrag = mdesc.ice.ufrag;
                    candidate.password = mdesc.ice.password;
                }

                var mdescIndex = localSessionInfo.mediaDescriptions.indexOf(mdesc);
                client.gotIceCandidate(mdescIndex, cand, candidate.ufrag, candidate.password);
            });

            session.signal.on_candidate_gathering_done.connect(function () {
                var mdescIndex = localSessionInfo.mediaDescriptions.indexOf(mdesc);
                client.candidateGatheringDone(mdescIndex);
            });

        }

        function prepareDataSession(dataSession, mdesc) {
            prepareSession(dataSession, mdesc);
            dataSession.sctp_local_port = mdesc.sctp.port;

            dataSession.signal.on_data_channel_requested.connect(function (d,
                ordered, maxPacketLifeTime, maxRetransmits, protocol, negotiated, id, label) {
                var settings = {
                    "ordered": ordered,
                    "maxPacketLifeTime": maxPacketLifeTime != -1 ? maxPacketLifeTime : null,
                    "maxRetransmits": maxRetransmits != -1 ? maxRetransmits : null,
                    "protocol": protocol,
                    "negotiated": negotiated,
                    "id": id,
                    "label": label
                };

                client.dataChannelRequested(settings);
            });

            client.dataChannelsEnabled();
        }

        function prepareMediaSession(mediaSession, mdesc) {
            prepareSession(mediaSession, mdesc);
            mediaSession.rtcp_mux = !isInitiator && !!(mdesc.rtcp && mdesc.rtcp.mux);

            if (mdesc.cname && mdesc.ssrcs && mdesc.ssrcs.length) {
                mediaSession.cname = mdesc.cname;
                mediaSession.send_ssrc = mdesc.ssrcs[0];
            }

            mediaSession.signal.on_incoming_source.connect(function (m, remoteSource) {
                var mdescIndex = localSessionInfo.mediaDescriptions.indexOf(mdesc);
                remoteSources[mdescIndex] = remoteSource;
                client.gotRemoteSource(mdescIndex, jsonRpc.createObjectRef(remoteSource));
            });

            mdesc.payloads.forEach(function (payload) {
                if (payload.encodingName.toUpperCase() == "RTX")
                    return;
                var rtxPayload = findRtxPayload(mdesc.payloads, payload.type);
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
                        "nack_pli": payload.nackpli,
                        "rtx_payload_type": rtxPayload ? rtxPayload.type : -1,
                        "rtx_time": rtxPayload && rtxPayload.parameters.rtxTime || 0
                    });
                mediaSession.add_receive_payload(receivePayload);
            });
        }
    };

    this.prepareToSend = function (remoteSessionInfo, isInitiator) {
        var i;
        var sessionConfigs = [];
        for (var i = sessions.length; i < remoteSessionInfo.mediaDescriptions.length; i++) {
            var rmdesc = remoteSessionInfo.mediaDescriptions[i];
            var sessionConfig = {
                "dtlsRole": rmdesc.dtls.setup == "active" ? "passive" : "active",
                "type": rmdesc.type == "application" ? "data" : "media"
            };
            sessionConfigs.push(sessionConfig);
        }

        ensureTransportAgentAndSessions(isInitiator, sessionConfigs);

        for (i = 0; i < sessions.length; i++) {
            var session = sessions[i];
            var mdesc = remoteSessionInfo.mediaDescriptions[i];
            if (!mdesc)
                continue;

            if (mdesc.type == "audio" || mdesc.type == "video")
                session.rtcp_mux = !!(mdesc.rtcp && mdesc.rtcp.mux);

            if (mdesc.ice && mdesc.ice.candidates) {
                mdesc.ice.candidates.forEach(function (candidate) {
                    internalAddRemoteCandidate(session, candidate,
                        mdesc.ice.ufrag, mdesc.ice.password);
                });
            }

            if (i < numberOfSendPreparedSessions)
                continue;

            if (mdesc.type == "application") {
                session.sctp_remote_port = mdesc.sctp.port;
                numberOfSendPreparedSessions = i + 1;
                continue;
            }

            var payload;
            mdesc.payloads.some(function (p) {
                if (p.encodingName.toUpperCase() != "RTX") {
                    payload = p;
                    return true;
                }
            });

            if (!mdesc.source || !payload)
                continue;

            var adapt = payload.ericscream ? owr.AdaptationType.SCREAM :
                owr.AdaptationType.DISABLED;
            var rtxPayload = findRtxPayload(mdesc.payloads, payload.type);
            var sendPayload = (mdesc.type == "audio") ?
                new owr.AudioPayload({
                    "payload_type": payload.type,
                    "codec_type": owr.CodecType[payload.encodingName.toUpperCase()],
                    "clock_rate": payload.clockRate,
                    "channels": payload.channels,
                    "adaptation": adapt
                }) :
                new owr.VideoPayload({
                    "payload_type": payload.type,
                    "codec_type": owr.CodecType[payload.encodingName.toUpperCase()],
                    "clock_rate": payload.clockRate,
                    "ccm_fir": !!payload.ccmfir,
                    "nack_pli": !!payload.nackpli,
                    "adaptation": adapt,
                    "rtx_payload_type": rtxPayload ? rtxPayload.type : -1,
                    "rtx_time": rtxPayload && rtxPayload.parameters.rtxTime || 0
                });
            session.set_send_payload(sendPayload);
            session.set_send_source(mdesc.source);
            numberOfSendPreparedSessions = i + 1;
        }
    }

    this.addRemoteCandidate = function (candidate, sessionIndex, ufrag, password) {
        internalAddRemoteCandidate(sessions[sessionIndex], candidate, ufrag, password);
    };

    this.stop = function () {
        var i;
        for (i = 0; i < remoteSources.length; i++) {
            if (remoteSources[i]) {
                jsonRpc.removeObjectRef(remoteSources[i]);
                delete remoteSources[i];
            }
        }
        remoteSources = null;
        for (i = 0; i < sessions.length; i++) {
            if (sessions[i].set_send_source)
                sessions[i].set_send_source(null);
            delete sessions[i];
        }
        sessions = null;
        transportAgent = null;
    };

    function findRtxPayload(payloads, apt) {
        var rtxPayload;
        payloads.some(function (payload) {
            if (payload.encodingName.toUpperCase() == "RTX"
                && payload.parameters && payload.parameters.apt == apt) {
                rtxPayload = payload;
                return true;
            }
        });
        return rtxPayload;
    }

    function internalAddRemoteCandidate(session, candidate, ufrag, password) {
        if (session.rtcp_mux && candidate.componentId == owr.ComponentType.RTCP)
            return;

        var transportType = candidate.tcpType ?
            owr.TransportType["TCP_" + candidate.tcpType.toUpperCase()] : owr.TransportType.UDP;
        session.add_remote_candidate(new owr.Candidate({
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

    function ensureTransportAgentAndSessions(isInitiator, sessionConfigs) {
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

        sessionConfigs.forEach(function (config) {
            if (config.type == "data") {
                dataSession = new owr.DataSession({
                    "dtls_client_mode": config.dtlsRole == "active",
                    "use_sock_stream": true
                });
                sessions.push(dataSession);
            } else {
                sessions.push(new owr.MediaSession({
                    "rtcp_mux": false,
                    "dtls_client_mode": config.dtlsRole == "active"
                }));
            }
        });
    }

    this.createDataChannel = function (settings, client) {
        if (!settings.negotiated && settings.id == 65535) {
            if (!nextDataChannelId)
                nextDataChannelId = +!dataSession.dtls_client_mode; // client uses even ids
            settings.id = nextDataChannelId;
            nextDataChannelId += 2;
        }

        if (settings.maxPacketLifeTime == null)
            settings.maxPacketLifeTime = -1;
        if (settings.maxRetransmits == null)
            settings.maxRetransmits = -1;

        var internalDataChannel = new InternalDataChannel(settings, dataSession, client);
        return {
            "channel": createInternalDataChannelRef(internalDataChannel, jsonRpc),
            "id": settings.id
        };
    };
}

function createInternalDataChannelRef(internalDataChannel, jsonRpc) {
    var exports = [ "send", "sendBinary", "close" ];
    exports.forEach(function (name) {
        jsonRpc.exportFunctions(internalDataChannel[name]);
    });
    return jsonRpc.createObjectRef(internalDataChannel, exports)
}

function InternalDataChannel(settings, dataSession, client) {
    var dataChannelReadyStateNames = [ "connecting", "open", "closing", "closed" ];

    var channel = new owr.DataChannel({
        "ordered": settings.ordered,
        "max_packet_life_time": settings.maxPacketLifeTime,
        "max_retransmits": settings.maxRetransmits,
        "protocol": settings.protocol,
        "negotiated": settings.negotiated,
        "id": settings.id,
        "label": settings.label
    });

    channel.signal.connect("notify::ready-state", function (ch) {
        client.readyStateChanged(dataChannelReadyStateNames[ch.ready_state]);
    });

    channel.signal.on_data.connect(function (ch, data) {
        client.gotData(data);
    });

    dataSession.add_data_channel(channel);

    this.send = function (data) {
        channel.send(data);
        client.setBufferedAmount(channel.buffered_amount);
    };

    this.sendBinary = function (data) {
        var buf = Array.prototype.slice.call(new Uint8Array(data));
        channel.send_binary(buf, buf.length);
        client.setBufferedAmount(channel.buffered_amount);
    };

    this.close = function () {
        channel.close();
    };
}
