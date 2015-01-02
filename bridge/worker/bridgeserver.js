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

var imageServers = {};
var imageServerBasePort = 10000 + Math.floor(Math.random() * 40000);
var nextImageServerPort = imageServerBasePort;
var extensionConnect = false;
var validExtenstionOrigin = "safari-extension://com.ericsson.research.owr";
var extensionApproved = false;
var extws;
//var reqQueue = []; for future work
var busy = false;
var accepts = [];

var extensionServer = new WebSocketServer(10719, "127.0.0.1");
extensionServer.onaccept = function (event) {
    extensionConnect = true;
    extws = event.socket;
    var reqOrigin = event.origin;
    console.log("Extension-web-socket set up, origin: " + reqOrigin);
    var reqOriginFirst45 = reqOrigin.slice(0, 44);
    //check if reqOriginF45 is equal to "safari-extension://com.ericsson.research.owr"
    if (reqOriginFirst45 == validExtenstionOrigin) extensionApproved = true;
    extws.onlcose = function (evt) {
        extws = null;
    }
}

var server = new WebSocketServer(10717, "127.0.0.1");
server.onaccept = function (event) {
    extensionConnect = true;
    var ws = event.socket;
    var mainOrigin = event.origin;
    console.log("channel socket origin: " + mainOrigin);
    var channel = {
        "postMessage": function (message) {
            ws.send(btoa(message));
        },
        "onmessage": null
    };

    ws.onmessage = function (event) {
        var message = atob(event.data);
        if (channel.onmessage)
            channel.onmessage({"data": message});
    };

    var rpcScope = {};
    var jsonRpc = new JsonRpc(channel, {"scope": rpcScope, "noRemoteExceptions": true});
    var peerHandlers = [];

    ws.onclose = function (event) {
        for (var i = 0; i < peerHandlers.length; i++) {
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

    rpcScope.createPeerHandler = function (configuration, client) {
        var peerHandler = new PeerHandler(configuration, client, jsonRpc);
        peerHandlers.push(peerHandler);
        var exports = [ "prepareToReceive", "prepareToSend", "addRemoteCandidate" ];
        for (var i = 0; i < exports.length; i++)
            jsonRpc.exportFunctions(peerHandler[exports[i]]);
        return jsonRpc.createObjectRef(peerHandler, exports);
    };

    rpcScope.requestSources = function (options, client) {
        var mediaTypes = 0;
        var requestId;
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
                            "source": jsonRpc.createObjectRef(sources[i])
                        });
                        break;
                    }
                }
            }


            function returnToClient (sourcesToUse) {
                sourcesToUse.forEach(function (el, ix, ar) {
                    usedSources.push(el);
                    console.log("pushed source " + el + " to usedSources");
                });
                console.log("usedSources: " + usedSources);
                client.gotSources(sourcesToUse);
            }
            function deRef (accept_el, accept_ix, accept_ar) {
                accept_el.forEach(function (source_el, source_ix, source_ar) {
                    console.log("deref, accept_ix: " +accept_ix + "source_ix: " +source_ix + " el: " + source_el + " el.source: " + source_el.source);
                    jsonRpc.removeObjectRef(source_el.source);
                });
            }

            function handleExtResponse (evt) {
                console.log("message received on extsocket");
                var response = JSON.parse(evt.data);
                if (response.name == "accept" && response.Id == requestId) {
                    console.log("accept received on socket for requestId " + requestId);
                    accepts[requestId] = JSON.parse(response.acceptSourceInfos);
                    client.gotSources(JSON.parse(response.acceptSourceInfos));
                }
                if (response.name == "revokeAll") {
                    console.log("revokeAll received on socket");
                    accepts.forEach(deRef);
                    accepts = [];
                }
                if (response.name == "revoke") {
                    console.log("revoke received on socket for requestId " + response.Id);
                    if (accepts[response.Id]) {
                        deRef(accepts[response.Id], response.Id, accepts);
                        accepts[response.Id] = null;
                    }
                }
                clearBlock(); //clearing regardless of whether reponse was "accept", "reject" or "timeout"
            }

            function clearBlock () {
                busy = false;
            }

            if (extensionConnect) { //If an extension ever did try to connect
                if (extws && extensionApproved) {
                    requestId = Math.floor(Math.random() * 40000);
                    console.log("sourceInfos: " + sourceInfos);
                    var requestMessage = {
                        "name": "request",
                        "origin": mainOrigin,
                        "Id": requestId,
                        "requestSourceInfos": JSON.stringify(sourceInfos)
                    }
                    if (!busy) {
                        busy = true;
                        extws.send(JSON.stringify(requestMessage));
                        console.log("sent request to bar");
                        extws.onmessage = handleExtResponse;

                    } else {
                        //outstanding request to extension; queue up
                        //reqQueue.push(requestMessage); -- for a later version, just drop on floor now
                        console.log("an extension is connected but busy serving (i.e. get accept or reject for) an gUM call. Dropping on floor.");
                    }

                } else {
                    console.log("an extension has been (at least) trying to connect, but can for some reason not serve the gUM request at this time. Dropping on floor.");
                }
            } else {
                console.log("no extension");
                client.gotSources(sourcesToUse);
            }
        });
    };

    rpcScope.renderSources = function (audioSources, videoSources, tag) {

        var audioRenderer;
        if (audioSources.length > 0) {
            audioRenderer = new owr.AudioRenderer({ "disabled": true });
            audioRenderer.set_source(audioSources[0]);
        }
        var imageServer;
        var videoRenderer;
        if (videoSources.length > 0) {
            videoRenderer = new owr.ImageRenderer();
            videoRenderer.set_source(videoSources[0]);

            if (nextImageServerPort > imageServerBasePort + 10)
                nextImageServerPort = imageServerBasePort;
            imageServer = imageServers[nextImageServerPort];
            if (!imageServer)
                imageServer = imageServers[nextImageServerPort] = new owr.ImageServer({ "port": nextImageServerPort });
            imageServer.add_image_renderer(videoRenderer, tag);
            nextImageServerPort++;
        }

        var controller = new RenderController(audioRenderer, videoRenderer);
        jsonRpc.exportFunctions(controller.setAudioMuted);
        var controllerRef = jsonRpc.createObjectRef(controller, "setAudioMuted");

        return { "controller": controllerRef, "port": imageServer ? imageServer.port : 0 };
    };

    jsonRpc.exportFunctions(rpcScope.createPeerHandler, rpcScope.requestSources, rpcScope.renderSources);

};

function RenderController(audioRenderer, videoRenderer) {
    this.setAudioMuted = function (isMuted) {
        if (audioRenderer)
            audioRenderer.disabled = isMuted;
    };
}

var owr_js = "(function () {\n" + wbjsonrpc_js + domutils_js + sdp_js + webrtc_js + "\n})();";

server.onrequest = function (event) {
    var response = {"headers": {}};
    if (event.request.url == "/owr.js") {
        response.status = 200;
        response.headers["Content-Type"] = "text/javascript";
        response.headers["Access-Control-Allow-Origin"] = "*";
        response.body = owr_js;
    } else {
        response.status = 404;
        response.headers["Content-Type"] = "text/html";
        response.body = "<!doctype html><html><body><h1>404 Not Found</h1></body></html>";
    }
    event.request.respond(response);
};
