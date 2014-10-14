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

(function (global) {

    var signalingStateMap = {
        "stable": {
            "setLocal:offer": "have-local-offer",
            "setRemote:offer": "have-remote-offer"
        },
        "have-local-offer": {
            "setLocal:offer": "have-local-offer",
            "setRemote:answer": "stable"
        },
        "have-remote-offer": {
            "setLocal:answer": "stable",
            "setRemote:offer": "have-remote-offer"
        }
    };

    var defaultPayloads = {
        "audio" : [
            { "encodingName": "OPUS", "type": 111, "clockRate": 48000, "channels": 2 },
            { "encodingName": "PCMA", "type": 8, "clockRate": 8000, "channels": 1 },
            { "encodingName": "PCMU", "type": 0, "clockRate": 8000, "channels": 1 },
        ],
        "video": [
            { "encodingName": "H264", "type": 103, "clockRate": 90000,
                "ccmfir": true, "nackpli": true },
            { "encodingName": "VP8", "type": 100, "clockRate": 90000,
                "ccmfir": true, "nackpli": true }
        ]
    };


    var messageChannel = new function () {
        var _this = this;
        var ws;
        var sendQueue = [];

        function ensureWebSocket() {
            if (ws && ws.readyState <= ws.OPEN)
                return;

            ws = new WebSocket("ws://localhost:10717/bridge");
            ws.onopen = processSendQueue;
            ws.onmessage = function (evt) {
                if (_this.onmessage instanceof Function) {
                    _this.onmessage({"data": atob(evt.data)});
                }
            };
            ws.onclose = ws.onerror = function () {
                ws = null;
            };
        }

        function processSendQueue() {
            if (!ws || ws.readyState != ws.OPEN)
                return;
            for (var i = 0; i < sendQueue.length; i++)
                ws.send(sendQueue[i]);
            sendQueue = [];
        }

        this.postMessage = function (message) {
            sendQueue.push(btoa(message));
            ensureWebSocket();
            processSendQueue();
        };

        this.onmessage = null;
    };

    var sourceInfoMap = {};
    var bridge = new JsonRpc(messageChannel);
    bridge.importFunctions("createPeerHandler", "requestSources", "renderSources");

    function getUserMedia(options, successCallback, errorCallback) {
        checkArguments("getUserMedia", "object, function, function", 3, arguments);
        checkDictionary("MediaStreamConstraints", options, {
            "audio": "object | boolean",
            "video": "object | boolean"
        });

        if (!options.audio && !options.video) {
            throw new MediaStreamError({
                "name": "NotSupportedError",
                "message": "Options has no media"
            });
        }

        var client = {};
        client.gotSources = function (sourceInfos) {
            var trackList = sourceInfos.map(function (sourceInfo) {
                return new MediaStreamTrack(sourceInfo);
            });
            bridge.removeObjectRef(client);
            setTimeout(successCallback, 0, new MediaStream(trackList));
        };

        bridge.requestSources(options, bridge.createObjectRef(client, "gotSources"));
    }

    getUserMedia.toString = function () {
        return "function getUserMedia() { [not native code] }";
    };

    //
    // MediaStream
    //
    MediaStream.prototype = Object.create(EventTarget.prototype);
    MediaStream.prototype.constructor = MediaStream;

    function MediaStream() { // (MediaStream or sequence<MediaStreamTrack>)
        checkArguments("MediaStream", "webkitMediaStream | Array", 1, arguments);

        EventTarget.call(this, {
            "onactive": null,
            "oninactive": null,
            "odaddtrack": null,
            "onremavetrack": null
        });

        var a = { // attributes
            "id": mediaStreamPrivateInit.id || randomString(),
            "active": false
        };
        domObject.addReadOnlyAttributes(this, a);

        var trackSet = {};

        var constructorTracks = arguments[0] instanceof MediaStream ? arguments[0].getTracks() : arguments[0];
        constructorTracks.forEach(function (track) {
            if (!(track instanceof MediaStreamTrack))
                throw TypeError("MediaStream: list item is not a MediaStreamTrack");

            if (!a.active && track.readyState != "ended")
                a.active = true;
            trackSet[track.id] = track;
        });
        arguments[0] = constructorTracks = null;


        this.onended = null;
        this.toString = MediaStream.toString;

        this.getAudioTracks = function () {
            return toTrackList("audio");
        };

        this.getVideoTracks = function () {
            return toTrackList("video");
        };

        this.getTracks = function () {
            return toTrackList();
        };

        this.getTrackById = function (id) {
            checkArguments("getTrackById", "string", 1, arguments);

            return trackSet[id] || null;
        }

        this.clone = function () {
            var trackClones = toTrackList().map(function (track) {
                return track.clone();
            });
            return new MediaStream(trackClones);
        }

        function toTrackList(kind) {
            var list = [];
            Object.keys(trackSet).forEach(function (key) {
                if (!kind || trackSet[key].kind == kind)
                    list.push(trackSet[key]);
            });
            return list;
        }
    }

    MediaStream.toString = function () {
        return "[object MediaStream]";
    };

    var mediaStreamPrivateInit = {};
    function createMediaStream(trackListOrStream, id) {
        mediaStreamPrivateInit = { "id": id };
        var stream = new MediaStream(trackListOrStream);
        mediaStreamPrivateInit = {};
        return stream;
    }

    //
    // MediaStreamTrack
    //
    MediaStreamTrack.prototype = Object.create(EventTarget.prototype);
    MediaStreamTrack.prototype.constructor = MediaStreamTrack;

    function MediaStreamTrack(sourceInfo, id) {

        EventTarget.call(this, {
            "onmute": null,
            "onunmute": null,
            "onended": null,
            "onoverconstrained": null
        });

        var a = { // attributes
            "kind": sourceInfo.mediaType,
            "id": id || randomString(),
            "label": sourceInfo.label,
            "muted": false,
            "readyState": "live"
        };
        domObject.addReadOnlyAttributes(this, a);

        sourceInfoMap[a.id] = sourceInfo;

        this.toString = MediaStreamTrack.toString;

        this.clone = function () {
            return new MediaStreamTrack(sourceInfo);
        };

        this.stop = function () {

        };
    }

    MediaStreamTrack.toString = function () {
        return "[object MediaStreamTrack]";
    };

    function MediaStreamError(initDict) {
        if (!initDict)
            initDict = {};

        var a = { // attributes
            "name": initDict.name || "",
            "message": initDict.message || null,
            "constraintName": initDict.constraintName || null
        };
        domObject.addReadOnlyAttributes(this, a);

        this.toString = function () {
            return "MediaStreamError: " + a.name + ": " + (a.message ? a.message : "");
        };
    }

    //
    // RTCPeerConnection
    //
    RTCPeerConnection.prototype = Object.create(EventTarget.prototype);
    RTCPeerConnection.prototype.constructor = RTCPeerConnection;

    function RTCPeerConnection(configuration) {
        var _this = this;

         EventTarget.call(this, {
            "onnegotiationneeded": null,
            "onicecandidate": null,
            "onsignalingstatechange": null,
            "onaddstream": null,
            "onremovestream": null,
            "oniceconnectionstatechange": null
        });

        var a = { // attributes
            "localDescription": getLocalDescription,
            "remoteDescription": getRemoteDescription,
            "signalingState": "stable",
            "iceGatheringState": "new",
            "iceConnectionState": "new"
        };
        domObject.addReadOnlyAttributes(this, a);

        checkArguments("RTCPeerConnection", "object", 1, arguments);
        checkConfigurationDictionary(configuration);

        if (!configuration.iceTransports)
            configuration.iceTransports = "all"

        var localStreams = [];
        var remoteStreams = [];

        var peerHandler;
        var peerHandlerClient = createPeerHandlerClient();
        var clientRef = bridge.createObjectRef(peerHandlerClient, "gotSendSSRC",
            "gotDtlsFingerprint", "gotIceCandidate", "candidateGatheringDone", "gotRemoteSource");
        var deferredPeerHandlerCalls = [];

        bridge.createPeerHandler(configuration, clientRef, function (ph) {
            peerHandler = ph;

            var func;
            while ((func = deferredPeerHandlerCalls.shift()))
                func();
        });

        function whenPeerHandler(func) {
            if (peerHandler)
                func();
            else
                deferredPeerHandlerCalls.push(func);
        }

        var negotiationNeededTimerHandle;
        var localSessionInfo = null;
        var remoteSessionInfo = null;
        var remoteSourceStatus = [];
        var lastSetLocalDescriptionType;
        var lastSetRemoteDescriptionType;
        var queuedOperations = [];

        function enqueueOperation(operation) {
            queuedOperations.push(operation);
            if (queuedOperations.length == 1)
                setTimeout(queuedOperations[0]);
        }

        function completeQueuedOperation(callback) {
            queuedOperations.shift();
            if (queuedOperations.length)
                setTimeout(queuedOperations[0]);

            try {
                callback();
            } catch (e) {
                setTimeout(function () {
                    throw e;
                });
            }

            if (!queuedOperations.length)
                maybeDispatchNegotiationNeeded();
        }

        function updateMediaDescriptionsWithTracks(mediaDescriptions, trackInfos) {
            mediaDescriptions.forEach(function (mdesc) {
                var index = indexOfByProperty(trackInfos, "mediaStreamTrackId",
                    mdesc.mediaStreamTrackId);
                if (index != -1)
                    trackInfos.splice(index, 1);
                else {
                    mdesc.mediaStreamId = null;
                    mdesc.mediaStreamTrackId = null;
                }
            });

            mediaDescriptions.forEach(function (mdesc) {
                if (mdesc.mediaStreamTrackId)
                    return;

                var index = indexOfByProperty(trackInfos, "kind", mdesc.type);
                if (index != -1) {
                    mdesc.mediaStreamId = trackInfos[index].mediaStreamId;
                    mdesc.mediaStreamTrackId = trackInfos[index].mediaStreamTrackId;
                    mdesc.mode = "sendrecv";
                    trackInfos.splice(index, 1);
                } else
                    mdesc.mode = "recvonly";
            });
        }

        this.createOffer = function (successCallback, failureCallback, options) {
            checkArguments("createOffer", "function, function, object", 2, arguments);
            if (options) {
                checkDictionary("RTCOfferOptions", options, {
                    "offerToReceiveVideo": "number | boolean",
                    "offerToReceiveAudio": "number | boolean"
                });
            }
            checkClosedState();

            enqueueOperation(function () {
                queuedCreateOffer(successCallback, failureCallback, options);
            });
        };

        function queuedCreateOffer(successCallback, failureCallback, options) {
            options = options || {};
            options.offerToReceiveAudio = +options.offerToReceiveAudio || 0;
            options.offerToReceiveVideo = +options.offerToReceiveVideo || 0;

            var localSessionInfoSnapshot = localSessionInfo ?
                JSON.parse(JSON.stringify(localSessionInfo)) : { "mediaDescriptions": [] };

            var localTrackInfos = getTrackInfos(localStreams);
            updateMediaDescriptionsWithTracks(localSessionInfoSnapshot.mediaDescriptions,
                localTrackInfos);

            localTrackInfos.forEach(function (trackInfo) {
                localSessionInfoSnapshot.mediaDescriptions.push({
                    "mediaStreamId": trackInfo.mediaStreamId,
                    "mediaStreamTrackId": trackInfo.mediaStreamTrackId,
                    "type": trackInfo.kind,
                    "payloads": JSON.parse(JSON.stringify(defaultPayloads[trackInfo.kind])),
                    "rtcp": { "mux": true },
                    "dtls": { "setup": "actpass" }
                });
            });

            [ "Audio", "Video" ].forEach(function (mediaType) {
                for (var i = 0; i < options["offerToReceive" + mediaType]; i++) {
                    var kind = mediaType.toLowerCase();
                    localSessionInfoSnapshot.mediaDescriptions.push({
                        "type": kind,
                        "payloads": JSON.parse(JSON.stringify(defaultPayloads[kind])),
                        "rtcp": { "mux": true },
                        "dtls": { "setup": "actpass" },
                        "mode": "recvonly"
                    });
                }
            });

            completeQueuedOperation(function () {
                successCallback(new RTCSessionDescription({
                    "type": "offer",
                    "sdp": SDP.generate(localSessionInfoSnapshot)
                }));
            });
        }

        this.createAnswer = function (successCallback, failureCallback, options) {
            checkArguments("createAnswer", "function, function, object", 2, arguments);
            if (options) {
                checkDictionary("RTCOfferOptions", options, {
                    "offerToReceiveVideo": "number | boolean",
                    "offerToReceiveAudio": "number | boolean"
                });
            }
            checkClosedState();

            enqueueOperation(function () {
                queuedCreateAnswer(successCallback, failureCallback, options);
            });
        };

        function queuedCreateAnswer(successCallback, failureCallback, options) {

            if (!remoteSessionInfo) {
                completeQueuedOperation(function () {
                    failureCallback(new Error("InvalidStateError"));
                });
                return;
            }

            var localSessionInfoSnapshot = localSessionInfo ?
                JSON.parse(JSON.stringify(localSessionInfo)) : { "mediaDescriptions": [] };

            for (var i = 0; i < remoteSessionInfo.mediaDescriptions.length; i++) {
                var lmdesc = localSessionInfoSnapshot.mediaDescriptions[i];
                var rmdesc = remoteSessionInfo.mediaDescriptions[i];
                if (!lmdesc) {
                    lmdesc = {
                        "type": remoteSessionInfo.mediaDescriptions[i].type,
                        "payloads": remoteSessionInfo.mediaDescriptions[i].payloads,
                        "rtcp": {},
                        "dtls": { "setup": rmdesc.dtls.setup == "active" ? "passive" : "active" }
                    };
                    localSessionInfoSnapshot.mediaDescriptions.push(lmdesc);
                }

                lmdesc.payloads = rmdesc.payloads;
                lmdesc.rtcp.mux = rmdesc.rtcp.mux;
                if (lmdesc.dtls.setup == "actpass")
                    lmdesc.dtls.setup = "passive";
            }

            var localTrackInfos = getTrackInfos(localStreams);
            updateMediaDescriptionsWithTracks(localSessionInfoSnapshot.mediaDescriptions,
                localTrackInfos);

            completeQueuedOperation(function () {
                successCallback(new RTCSessionDescription({
                    "type": "answer",
                    "sdp": SDP.generate(localSessionInfoSnapshot)
                }));
            });
        }

        var latestLocalDescriptionCallback;

        this.setLocalDescription = function (description, successCallback, failureCallback) {
            checkArguments("setLocalDescription", "RTCSessionDescription, function, function", 3, arguments);
            checkClosedState();

            enqueueOperation(function () {
                queuedSetLocalDescription(description, successCallback, failureCallback);
            });
        };

        function queuedSetLocalDescription(description, successCallback, failureCallback) {
            var targetState = signalingStateMap[a.signalingState]["setLocal:" + description.type];
            if (!targetState) {
                completeQueuedOperation(function () {
                    failureCallback(new Error("InvalidSessionDescriptionError"));
                });
                return;
            }

            var previousNumberOfMediaDescriptions = localSessionInfo ?
                localSessionInfo.mediaDescriptions.length : 0;

            localSessionInfo = SDP.parse(description.sdp);
            lastSetLocalDescriptionType = description.type;

            var hasNewMediaDescriptions = localSessionInfo.mediaDescriptions.length >
                previousNumberOfMediaDescriptions;

            var isInitiator = description.type == "offer";
            whenPeerHandler(function () {
                latestLocalDescriptionCallback = function () {
                    a.signalingState = targetState;
                    successCallback();
                };

                if (hasNewMediaDescriptions)
                    peerHandler.prepareToReceive(localSessionInfo, isInitiator);

                if (remoteSessionInfo)
                    peerHandler.prepareToSend(remoteSessionInfo, isInitiator);

                if (!hasNewMediaDescriptions)
                    completeQueuedOperation(latestLocalDescriptionCallback);
            });
        }

        this.setRemoteDescription = function (description, successCallback, failureCallback) {
            checkArguments("setRemoteDescription", "RTCSessionDescription, function, function", 3, arguments);
            checkClosedState();

            enqueueOperation(function () {
                queuedSetRemoteDescription(description, successCallback, failureCallback);
            });
        };

        function queuedSetRemoteDescription(description, successCallback, failureCallback) {
            var targetState = signalingStateMap[a.signalingState]["setRemote:" + description.type];
            if (!targetState) {
                completeQueuedOperation(function () {
                    failureCallback(new Error("InvalidSessionDescriptionError"));
                });
                return;
            }

            remoteSessionInfo = SDP.parse(description.sdp);
            lastSetRemoteDescriptionType = description.type;

            remoteSessionInfo.mediaDescriptions.forEach(function (mdesc, i) {
                if (!remoteSourceStatus[i])
                    remoteSourceStatus[i] = {};

                remoteSourceStatus[i].sourceExpected = mdesc.mode != "recvonly";

                if (!mdesc.ice) {
                    console.warn("setRemoteDescription: m-line " + i +
                        " is missing ICE credentials");
                    mdesc.ice = {};
                }
            });

            var allTracks = getAllTracks(localStreams);
            remoteSessionInfo.mediaDescriptions.forEach(function (mdesc) {
                var filteredPayloads = mdesc.payloads.filter(function (payload) {
                    return indexOfByProperty(defaultPayloads[mdesc.type],
                        "encodingName", payload.encodingName.toUpperCase()) != -1;

                });
                mdesc.payloads = filteredPayloads;

                var trackIndex = indexOfByProperty(allTracks, "kind", mdesc.type);
                if (trackIndex != -1) {
                    var track = allTracks.splice(trackIndex, 1)[0];
                    mdesc.source = sourceInfoMap[track.id].source;
                }
            });

            var isInitiator = description.type == "answer";
            whenPeerHandler(function () {
                peerHandler.prepareToSend(remoteSessionInfo, isInitiator);
                completeQueuedOperation(function () {
                    a.signalingState = targetState;
                    successCallback();
                });
            });
        };

        this.updateIce = function (configuration) {
            checkArguments("updateIce", "object", 1, arguments);
            checkConfigurationDictionary(configuration);
            checkClosedState();
        };

        this.addIceCandidate = function (candidate, successCallback, failureCallback) {
            checkArguments("addIceCandidate", "RTCIceCandidate, function, function", 3, arguments);
            checkClosedState();

            enqueueOperation(function () {
                queuedAddIceCandidate(candidate, successCallback, failureCallback);
            });
        };

        function queuedAddIceCandidate(candidate, successCallback, failureCallback) {
            if (!remoteSessionInfo) {
                completeQueuedOperation(function () {
                    failureCallback(new Error("InvalidStateError"));
                });
                return;
            }

            /* handle candidate values in the form <candidate> and a=<candidate>
             * to workaround https://code.google.com/p/webrtc/issues/detail?id=1142
             */
            var candidateAttribute = candidate.candidate;
            if (candidateAttribute.substr(0, 2) != "a=")
                candidateAttribute = "a=" + candidateAttribute;
            var candidateInfo = SDP.parse("m=application 0 NONE\r\n" + candidateAttribute + "\r\n");

            if (!candidateInfo.mediaDescriptions[0]
                || !candidateInfo.mediaDescriptions[0].ice
                || !candidateInfo.mediaDescriptions[0].ice.candidates
                || !candidateInfo.mediaDescriptions[0].ice.candidates[0]) {
                completeQueuedOperation(function () {
                    failureCallback(new Error("SyntaxError"));
                });
                return;
            }

            var mdesc = remoteSessionInfo.mediaDescriptions[candidate.sdpMLineIndex];
            if (!mdesc) {
                completeQueuedOperation(function () {
                    failureCallback(new Error("SyntaxError"));
                });
                return;
            }

            whenPeerHandler(function () {
                peerHandler.addRemoteCandidate(candidateInfo.mediaDescriptions[0].ice.candidates[0],
                    candidate.sdpMLineIndex, mdesc.ice.ufrag, mdesc.ice.password);
                completeQueuedOperation(successCallback);
            });
        };

        this.getConfiguration = function () {
            return JSON.parse(JSON.stringify(configuration));
        };

        this.getLocalStreams = function () {
            return localStreams.slice(0);
        };

        this.getRemoteStreams = function () {
            return remoteStreams.slice(0);
        };

        this.getStreamById = function (streamId) {
            checkArguments("getStreamById", "string", 1, arguments);
            streamId = String(streamId);

            return findInArrayById(localStreams, streamId) || findInArrayById(remoteStreams, streamId);
        };

        this.addStream = function (stream) {
            checkArguments("addStream", "webkitMediaStream", 1, arguments);
            checkClosedState();

            if (findInArrayById(localStreams, stream.id) || findInArrayById(remoteStreams, stream.id))
                return;

            localStreams.push(stream);
            setTimeout(maybeDispatchNegotiationNeeded);
        };

        this.removeStream = function (stream) {
            checkArguments("removeStream", "webkitMediaStream", 1, arguments);
            checkClosedState();

            var index = localStreams.indexOf(stream);
            if (index == -1)
                return;

            localStreams.splice(index, 1);
            setTimeout(maybeDispatchNegotiationNeeded);
        };

        this.close = function () {
            if (a.signalingState == "closed")
                return;

            a.signalingState = "closed";
        };

        this.toString = RTCPeerConnection.toString;

        function getLocalDescription() {
            if (!localSessionInfo)
                return null;
            return new RTCSessionDescription({
                "type": lastSetLocalDescriptionType,
                "sdp": SDP.generate(localSessionInfo)
            });
        }

        function getRemoteDescription() {
            if (!remoteSessionInfo)
                return null;
            return new RTCSessionDescription({
                "type": lastSetRemoteDescriptionType,
                "sdp": SDP.generate(remoteSessionInfo)
            });
        }

        function checkConfigurationDictionary(configuration) {
            checkDictionary("RTCConfiguration", configuration, {
                "iceServers": "Array",
                "iceTransports": "string"
            });

            if (configuration.iceServers) {
                configuration.iceServers.forEach(function (iceServer) {
                    checkType("RTCConfiguration.iceServers", iceServer, "object");
                    checkDictionary("RTCIceServer", iceServer, {
                        "urls": "Array | string",
                        "url": "string", // legacy support
                        "username": "string",
                        "credential": "string"
                    });
                });
            }
        }

        function checkClosedState() {
            if (a.signalingState == "closed")
                throw new Error("signalingState is 'closed'.");
        }

        function maybeDispatchNegotiationNeeded() {
            if (negotiationNeededTimerHandle || queuedOperations.length
                || a.signalingState != "stable")
                return;

            var mediaDescriptions = localSessionInfo ? localSessionInfo.mediaDescriptions : [];
            var allTracks = getAllTracks(localStreams);
            var i = 0;
            for (; i < allTracks.length; i++) {
                if (indexOfByProperty(mediaDescriptions, "mediaStreamTrackId",
                    allTracks[i].id) == -1)
                    break;
            }
            if (i == allTracks.length)
                return;

            negotiationNeededTimerHandle = setTimeout(function () {
                negotiationNeededTimerHandle = 0;
                if (a.signalingState == "stable")
                    _this.dispatchEvent({ "type": "negotiationneeded", "target": _this });
            }, 0);
        }

        function maybeDispatchGatheringDone() {
            if (isAllGatheringDone() && isLocalSessionInfoComplete()) {
                _this.dispatchEvent({ "type": "icecandidate", "candidate": null,
                    "target": _this });
            }
        }

        function dispatchMediaStreamEvent(trackInfos, id) {
            var trackList = trackInfos.map(function (trackInfo) {
                return new MediaStreamTrack(trackInfo.sourceInfo, trackInfo.id);
            });

            var mediaStream = createMediaStream(trackList, id);
            remoteStreams.push(mediaStream);

            _this.dispatchEvent({ "type": "addstream", "stream": mediaStream, "target": _this });
        }

        function getAllTracks(streamList) {
            var allTracks = [];
            streamList.forEach(function (stream) {
                Array.prototype.push.apply(allTracks, stream.getTracks());
            });
            return allTracks;
        }

        function getTrackInfos(streams) {
            var trackInfos = [];
            streams.forEach(function (stream) {
                var trackInfosForStream = stream.getTracks().map(function (track) {
                    return {
                        "kind": track.kind,
                        "mediaStreamTrackId": track.id,
                        "mediaStreamId": stream.id
                    };
                });
                Array.prototype.push.apply(trackInfos, trackInfosForStream);
            });
            return trackInfos;
        }

        function findInArrayById(array, id) {
            for (var i = 0; i < array.length; i++)
                if (array[i].id == id);
                    return array[i];
            return null;
        }

        function indexOfByProperty(array, propertyName, propertyValue) {
            for (var i = 0; i < array.length; i++) {
                if (array[i][propertyName] == propertyValue)
                    return i;
            }
            return -1;
        }

        function isLocalSessionInfoComplete() {
            for (var i = 0; i < localSessionInfo.mediaDescriptions.length; i++) {
                var mdesc = localSessionInfo.mediaDescriptions[i];
                if (!mdesc.dtls.fingerprint || !mdesc.ice || !mdesc.ssrcs || !mdesc.cname)
                    return false;
            }
            return true;
        }

        function dispatchIceCandidate(c, mdescIndex) {
            var candidateAttribute = "candidate:" + c.foundation + " " + c.componentId + " "
                + c.transport + " " + c.priority + " " + c.address + " " + c.port
                + " typ " + c.type;
            if (c.relatedAddress)
                candidateAttribute += " raddr " + c.relatedAddress + " rport " + c.relatedPort;
            if (c.tcpType)
                candidateAttribute += " tcptype " + c.tcpType;

            var candidate = new RTCIceCandidate({
                "candidate": candidateAttribute,
                "sdpMid": null,
                "sdpMLineIndex": mdescIndex
            });
            _this.dispatchEvent({ "type": "icecandidate", "candidate": candidate,
                "target": _this });
        }

        function isAllGatheringDone() {
            for (var i = 0; i < localSessionInfo.mediaDescriptions.length; i++) {
                var mdesc = localSessionInfo.mediaDescriptions[i];
                if (!mdesc.ice.gatheringDone)
                    return false;
            }
            return true;
        }

        function createPeerHandlerClient() {
            var client = {};

            client.gotSendSSRC = function (mdescIndex, ssrc, cname) {
                var mdesc = localSessionInfo.mediaDescriptions[mdescIndex];
                if (!mdesc.ssrcs)
                    mdesc.ssrcs = [];
                mdesc.ssrcs.push(ssrc);
                mdesc.cname = cname;

                if (isLocalSessionInfoComplete() && latestLocalDescriptionCallback) {
                    completeQueuedOperation(latestLocalDescriptionCallback);
                    latestLocalDescriptionCallback = null;
                }
            };

            client.gotDtlsFingerprint = function (mdescIndex, dtlsInfo) {
                var mdesc = localSessionInfo.mediaDescriptions[mdescIndex];
                mdesc.dtls.fingerprintHashFunction = dtlsInfo.fingerprintHashFunction;
                mdesc.dtls.fingerprint = dtlsInfo.fingerprint;

                if (isLocalSessionInfoComplete() && latestLocalDescriptionCallback) {
                    completeQueuedOperation(latestLocalDescriptionCallback);
                    latestLocalDescriptionCallback = null;
                    maybeDispatchGatheringDone();
                }
            };

            client.gotIceCandidate = function (mdescIndex, candidate, ufrag, password) {
                var mdesc = localSessionInfo.mediaDescriptions[mdescIndex];
                if (!mdesc.ice) {
                    mdesc.ice = {
                        "ufrag": ufrag,
                        "password": password,
                        "candidates": []
                    };
                }
                mdesc.ice.candidates.push(candidate);

                if (candidate.address.indexOf(":") == -1) { // not IPv6
                    if (candidate.componentId == 1) { // RTP
                        if (mdesc.address == "0.0.0.0") {
                            mdesc.address = candidate.address;
                            mdesc.port = candidate.port;
                        }
                    } else { // RTCP
                        if (!mdesc.rtcp.address || !mdesc.rtcp.port) {
                            mdesc.rtcp.address = candidate.address;
                            mdesc.rtcp.port = candidate.port;
                        }
                    }
                }

                if (isLocalSessionInfoComplete()) {
                    if (latestLocalDescriptionCallback) {
                        completeQueuedOperation(latestLocalDescriptionCallback);
                        latestLocalDescriptionCallback = null;
                        maybeDispatchGatheringDone();
                    } else
                        dispatchIceCandidate(candidate, mdescIndex);
                }
            };

            client.candidateGatheringDone = function (mdescIndex) {
                var mdesc = localSessionInfo.mediaDescriptions[mdescIndex];
                mdesc.ice.gatheringDone = true;
                maybeDispatchGatheringDone();
            };

            client.gotRemoteSource = function (mdescIndex, remoteSource) {
                remoteSourceStatus[mdescIndex].source = remoteSource;
                remoteSourceStatus[mdescIndex].isUpdated = true;

                for (var i = 0; i < remoteSourceStatus.length; i++) {
                    var status = remoteSourceStatus[i];
                    if (!status.source && status.sourceExpected)
                        return;
                }

                var legacyRemoteTrackInfos = [];
                var remoteTrackInfos = {};
                remoteSourceStatus.forEach(function (status, i) {
                    if (status.isUpdated) {
                        status.isUpdated = false;
                        var mdesc = remoteSessionInfo.mediaDescriptions[i];
                        var sourceInfo = {
                            "mediaType": mdesc.type,
                            "label": "Remote " + mdesc.type + " source",
                            "source": status.source
                        };

                        if (mdesc.mediaStreamId) {
                            if (!remoteTrackInfos[mdesc.mediaStreamId])
                                remoteTrackInfos[mdesc.mediaStreamId] = [];

                            remoteTrackInfos[mdesc.mediaStreamId].push({
                                "sourceInfo": sourceInfo,
                                "id": mdesc.mediaStreamTrackId
                            });
                        } else {
                            legacyRemoteTrackInfos.push({
                                "sourceInfo": sourceInfo,
                            });
                        }
                    }
                });

                Object.keys(remoteTrackInfos).forEach(function (mediaStreamId) {
                    dispatchMediaStreamEvent(remoteTrackInfos[mediaStreamId], mediaStreamId);
                });

                if (legacyRemoteTrackInfos.length)
                    dispatchMediaStreamEvent(legacyRemoteTrackInfos);
            };

            return client;
        }
    }

    RTCPeerConnection.toString = function () {
        return "[object RTCPeerConnection]";
    };

    function RTCSessionDescription(initDict) {
        checkArguments("RTCSessionDescription", "object", 0, arguments);
        if (initDict) {
            checkDictionary("RTCSessionDescriptionInit", initDict, {
                "type": "string",
                "sdp": "string"
            });
        } else
            initDict = {};

        this.type = initDict.hasOwnProperty("type") ? String(initDict["type"]) : null;
        this.sdp = initDict.hasOwnProperty("sdp") ? String(initDict["sdp"]) : null;

        this.toJSON = function () {
            return { "type": this.type, "sdp": this.sdp };
        };

        this.toString = RTCSessionDescription.toString;
    }

    RTCSessionDescription.toString = function () {
        return "[object RTCSessionDescription]";
    };

    function RTCIceCandidate(initDict) {
        checkArguments("RTCIceCandidate", "object", 0, arguments);
        if (initDict) {
            checkDictionary("RTCIceCandidateInit", initDict, {
                "candidate": "string",
                "sdpMid": "string",
                "sdpMLineIndex": "number"
            });
        } else
            initDict = {};

        this.candidate = initDict.hasOwnProperty("candidate") ? String(initDict["candidate"]) : null;
        this.sdpMid = initDict.hasOwnProperty("sdpMid") ? String(initDict["sdpMid"]) : null;
        this.sdpMLineIndex = parseInt(initDict["sdpMLineIndex"]) || 0;

        this.toJSON = function () {
            return { "candidate": this.candidate, "sdpMid": this.sdpMid, "sdpMLineIndex": this.sdpMLineIndex };
        };

        this.toString = RTCIceCandidate.toString;
    }

    RTCIceCandidate.toString = function () {
        return "[object RTCIceCandidate]";
    };

    function MediaStreamURL(mediaStream) {
        if (!MediaStreamURL.nextId)
            MediaStreamURL.nextId = 1;

        var url = "mediastream:" + randomString();

        function ensureImgDiv(video) {
            if (video.className.indexOf("owr-video") != -1)
                return video;

            var imgDiv = document.createElement("div");
            imgDiv.__src = url;

            var styleString = "display:inline-block;";
            if (video.width)
                styleString += "width:" + video.width + "px;";
            if (video.height)
                styleString += "height:" + video.height + "px;";

            if (video.ownerDocument.defaultView.getMatchedCSSRules) {
                var rules = video.ownerDocument.defaultView.getMatchedCSSRules(video, "");
                if (rules) {
                    for (var i = 0; i < rules.length; i++)
                        styleString += rules[i].style.cssText;
                }
            }

            var styleElement = document.createElement("style");
            var styleClassName = "owr-video" + MediaStreamURL.nextId++;
            styleElement.innerHTML = "." + styleClassName + " {" + styleString + "}";
            document.head.insertBefore(styleElement, document.head.firstChild);

            for (var p in global) {
                if (global[p] === video)
                    global[p] = imgDiv;
            }

            imgDiv.autoplay = video.autoplay;
            imgDiv.currentTime = 0;

            imgDiv.style.cssText = video.style.cssText;
            imgDiv.id = video.id;
            imgDiv.className = video.className + " owr-video " + styleClassName;

            var img = new Image();
            img.style.display = "inline-block";
            img.style.verticalAlign = "middle";
            imgDiv.appendChild(img);

            var ghost = document.createElement("div");
            ghost.style.display = "inline-block";
            ghost.style.height = "100%";
            ghost.style.verticalAlign = "middle";
            imgDiv.appendChild(ghost);

            Object.defineProperty(imgDiv, "src", {
                "get": function () { return url; },
                "set": function (src) {
                    url = String(src);
                    if (!(src instanceof MediaStreamURL))
                        setTimeout(scanVideoElements, 0);
                }
            });

            var videoParent = video.parentNode;
            videoParent.insertBefore(imgDiv, video);
            delete videoParent.removeChild(video);

            if (parseInt(getComputedStyle(imgDiv, null).width))
                img.style.width = "100%";

            return imgDiv;
        }

        function scanVideoElements() {
            var elements = document.querySelectorAll("video, div.owr-video");
            var i;
            for (i = 0; i < elements.length; i++) {
                if (elements[i].src == url && elements[i].__src != url)
                    break;
            }
            if (i == elements.length)
                return;

            var audioSources = mediaStream.getAudioTracks().map(getTrackSource);
            var videoSources = mediaStream.getVideoTracks().map(getTrackSource);

            function getTrackSource(track) {
                return sourceInfoMap[track.id].source;
            }

            var video = elements[i];
            var imgDiv = ensureImgDiv(video);
            var img = imgDiv.firstChild;
            if (!imgDiv.play)
                img.style.visibility = "hidden";

            var tag = randomString();
            bridge.renderSources(audioSources, videoSources, tag, function (renderInfo) {
                var count = Math.round(Math.random() * 100000);
                var roll = navigator.userAgent.indexOf("(iP") < 0 ? 100 : 1000000;
                var retryTime;
                var imgUrl;

                if (renderInfo.port)
                    imgUrl = "http://127.0.0.1:" + renderInfo.port + "/__" + tag + "-";

                if (imgDiv.play)
                    return;

                img.onload = function () {
                    imgDiv.videoWidth = img.naturalWidth;
                    imgDiv.videoHeight = img.naturalHeight;
                    imgDiv.currentTime++;

                    if (!retryTime) {
                        img.style.visibility = "visible";
                        retryTime = 500;
                    } else if (shouldAbort())
                        return;

                    img.src = imgUrl + (++count % roll);
                };

                img.onerror = function () {
                    if (retryTime) {
                        retryTime += 300;
                        if (shouldAbort())
                            return;
                    }

                    setTimeout(function () {
                        img.src = imgUrl + (++count % roll);
                        return;
                    }, retryTime || 100);
                };

                var muted = video.muted;
                Object.defineProperty(imgDiv, "muted", {
                    "get": function () { return muted; },
                    "set": function (isMuted) {
                        muted = !!isMuted;
                        renderInfo.controller.setAudioMuted(muted);
                    }
                });

                imgDiv.play = function () {
                    imgDiv.muted = imgDiv.muted;
                    if (imgUrl)
                        img.src = imgUrl;
                };
                if (imgDiv.autoplay)
                    imgDiv.play();

                function shouldAbort() {
                    return retryTime > 2000 || !imgDiv.parentNode;
                }
            });

        }

        this.toString = function () {
            setTimeout(scanVideoElements, 0);
            return url;
        };
    }

    global.webkitMediaStream = MediaStream;
    global.webkitRTCPeerConnection = RTCPeerConnection;
    global.RTCSessionDescription = RTCSessionDescription;
    global.RTCIceCandidate = RTCIceCandidate;
    global.navigator.webkitGetUserMedia = getUserMedia;

    var url = global.webkitURL || global.URL;
    if (!url)
        url = global.URL = {};

    var origCreateObjectURL = url.createObjectURL;
    url.createObjectURL = function (obj) {
        if (obj instanceof MediaStream)
            return new MediaStreamURL(obj);
        else if (origCreateObjectURL)
            return origCreateObjectURL(obj);
       // this will always fail
       checkArguments("createObjectURL", "Blob", 1, arguments);
    };

})(self);
