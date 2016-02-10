/*
 * Copyright (C) 2014-2015 Ericsson AB. All rights reserved.
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

var imageServers = {};
var imageServerBasePort = 10000 + Math.floor(Math.random() * 40000);
var nextImageServerPort = imageServerBasePort;
var originTokens = {};
var consentRequestQueue;

var extensionServer = new WebSocketServer(10719, "127.0.0.1");
extensionServer.onaccept = function (event) {
    if (event.origin.slice(0, 44) == "safari-extension://com.ericsson.research.owr" && !consentRequestQueue) {
        consentRequestQueue = new function () {
            var queue = [];
            var extws = event.socket;

            this.add = function (message, client) {
                queue.push({ "reqMsg": message, "client": client }); // only push if the extension origin is OK
                if (queue.length == 1)
                    extws.send(JSON.stringify(message)); // send only if there was no queue
            };

            function handleResponse(evt) {
                var outstandingRequest = queue.shift();
                var response = JSON.parse(evt.data);
                if (response.name == "accept" && response.Id == outstandingRequest.reqMsg.Id)
                    outstandingRequest.client.gotSources(response.acceptSourceInfos);
                else if (response.name == "reject" && response.Id == outstandingRequest.reqMsg.Id)
                    outstandingRequest.client.noSources("rejected");

                if (queue.length > 0)
                    extws.send(JSON.stringify(queue[0].reqMsg));
            }
            extws.onmessage = handleResponse;
            extws.onclose = function () {
                console.log("extws unexpectedly closed");
                consentRequestQueue = null;
            };
        }
    } else {
        console.log("Origin of extension incorrect, or a socket to the extension did already exist; closing");
        event.socket.close();
    }
};

var server = new WebSocketServer(10717, "127.0.0.1");
server.onaccept = function (event) {
    var ws = event.socket;
    var origin = event.origin;
    if (!origin || origin == "null") {
        origin = Object.keys(originTokens).find(function (origin) {
            return originTokens[origin] == ws.protocol;
        }) || null;
    }
    var channel = {
        "postMessage": function (message) {
            ws.send(message);
        },
        "onmessage": null
    };

    ws.onmessage = function (event) {
        if (channel.onmessage)
            channel.onmessage(event);
    };

    var rpcScope = {};
    var jsonRpc = new JsonRpc(channel, {"scope": rpcScope, "noRemoteExceptions": true});
    var peerHandlers = [];
    var renderControllers = [];

    ws.onclose = function (event) {
        var i;
        for (i = 0; i < renderControllers.length; i++) {
            renderControllers[i].stop();
            jsonRpc.removeObjectRef(renderControllers[i]);
            delete renderControllers[i];
        }
        renderControllers = null;
        for (i = 0; i < peerHandlers.length; i++) {
            peerHandlers[i].stop();
            jsonRpc.removeObjectRef(peerHandlers[i]);
            delete peerHandlers[i];
        }
        peerHandlers = null;
        rpcScope = null;
        jsonRpc = null;
        channel = null;
        ws = null;
    };

    rpcScope.createPeerHandler = function (configuration, keyCert, client) {
        var peerHandler = new PeerHandler(configuration, keyCert, client, jsonRpc);
        peerHandlers.push(peerHandler);
        var exports = [ "prepareToReceive", "prepareToSend", "addRemoteCandidate",
            "createDataChannel" ];
        for (var i = 0; i < exports.length; i++)
            jsonRpc.exportFunctions(peerHandler[exports[i]]);
        return jsonRpc.createObjectRef(peerHandler, exports);
    };

    rpcScope.createKeys = function (client) {
        var localcertificate, localprivatekey, localfingerprint;
        owr.crypto_create_crypto_data(function (privatekey, certificate, fingerprint, fingerprintHashFunction) {
            if ((privatekey && certificate && fingerprint && fingerprintHashFunction)) {
                if (fingerprint == "Failure") {
                    console.log("generation of crypto data has failed");
                    client.dtlsInfoGenerationDone();
                }
                else {
                    client.dtlsInfoGenerationDone({
                        "certificate": certificate,
                        "privatekey": privatekey,
                        "fingerprint": fingerprint,
                        "fingerprintHashFunction": fingerprintHashFunction
                    });
                }
            }
        });
    }

    rpcScope.requestSources = function (options, client) {
        var mediaTypes = 0;
        if (options.audio)
            mediaTypes |= owr.MediaType.AUDIO;
        if (options.video)
            mediaTypes |= owr.MediaType.VIDEO;

        owr.get_capture_sources(mediaTypes, function (sources) {
            var sourceInfos = [];
            if (options.audio)
                pushSourceInfo("audio");
            if (options.video)
                pushSourceInfo("video");

            function pushSourceInfo(mediaType) {
                for (var i = 0; i < sources.length; i++) {
                    if (sources[i].media_type == owr.MediaType[mediaType.toUpperCase()]) {
                        if (mediaType == "video" && options.video.facingMode == "environment") {
                            delete options.video.facingMode;
                            continue;
                        }
                        sourceInfos.push({
                            "mediaType": mediaType,
                            "label": sources[i].name,
                            "source": jsonRpc.createObjectRef(sources[i]),
                            "type": ["unknown", "capture", "test"][sources[i].type]
                        });
                        break;
                    }
                }
            }

            if (sourceInfos.length > 0) {
                if (consentRequestQueue) { // If an extension ever did try to connect
                    var requestId = Math.random().toString(16).substr(2);
                    var requestMessage = {
                        "name": "request",
                        "origin": origin,
                        "Id": requestId,
                        "requestSourceInfos": sourceInfos
                    };
                    consentRequestQueue.add(requestMessage, client);
                } else {
                    // This option should be removed eventually - only extensions that ask for consent should be allowed to use owr
                    client.gotSources(sourceInfos);
                }
            }    
            else
                client.noSources("notavailable");

        });
    };

    rpcScope.renderSources = function (audioSources, videoSources, tag, useVideoOverlay) {
        var audioRenderer;
        if (audioSources.length > 0) {
            audioRenderer = new owr.AudioRenderer({ "disabled": true });
            audioRenderer.set_source(audioSources[0]);
        }
        var imageServer;
        var imageServerPort = 0;
        var videoRenderer;
        if (videoSources.length > 0) {
            videoRenderer = useVideoOverlay ? new owr.VideoRenderer({ "tag": tag })
                : new owr.ImageRenderer();
            videoRenderer.set_source(videoSources[0]);

            if (!useVideoOverlay) {
                if (nextImageServerPort > imageServerBasePort + 10)
                    nextImageServerPort = imageServerBasePort;
                imageServerPort = nextImageServerPort++;
                imageServer = imageServers[imageServerPort];
                if (!imageServer) {
                    imageServer = imageServers[imageServerPort] = new owr.ImageServer({
                        "port": imageServerPort,
                        "allow-origin": origin
                    });
                } else if (imageServer.allow_origin.split(" ").indexOf(origin) == -1)
                    imageServer.allow_origin += " " + origin;
                imageServer.add_image_renderer(videoRenderer, tag);
            }
        }

        var controller = new RenderController(audioRenderer, videoRenderer, imageServerPort, tag);
        renderControllers.push(controller);
        jsonRpc.exportFunctions(controller.setAudioMuted, controller.stop);
        var controllerRef = jsonRpc.createObjectRef(controller, "setAudioMuted", "stop");

        return { "controller": controllerRef, "port": imageServerPort };
    };

    jsonRpc.exportFunctions(rpcScope.createPeerHandler, rpcScope.requestSources, rpcScope.renderSources, rpcScope.createKeys);

};

function RenderController(audioRenderer, videoRenderer, imageServerPort, tag) {
    this.setAudioMuted = function (isMuted) {
        if (audioRenderer)
            audioRenderer.disabled = isMuted;
    };

    this.stop = function () {
        if (audioRenderer)
            audioRenderer.set_source(null);
        if (videoRenderer)
            videoRenderer.set_source(null);
        if (imageServerPort) {
            var imageServer = imageServers[imageServerPort];
            if (imageServer)
                imageServer.remove_image_renderer(tag);
        }

        audioRenderer = videoRenderer = imageServerPort = null;
    };
}

var owr_js = wbjsonrpc_js + domutils_js + sdp_js + webrtc_js;

server.onrequest = function (event) {
    var response = {"headers": {}};
    if (event.request.url == "/owr.js") {
        response.status = 200;
        response.headers["Content-Type"] = "text/javascript";
        response.headers["Access-Control-Allow-Origin"] = "*";

        var origin = event.request.headers["origin"] || null;
        var token = originTokens[origin];
        if (!token) {
            token = Date.now().toString(16) + Math.random().toString(16).substr(2);
            originTokens[origin] = token;
        }
        var body = "(function () {\nvar originToken = \"" + token + "\";\n" + owr_js + "\n})();";
        response.body = body;
    } else {
        response.status = 404;
        response.headers["Content-Type"] = "text/html";
        response.body = "<!doctype html><html><body><h1>404 Not Found</h1></body></html>";
    }
    event.request.respond(response);
};
