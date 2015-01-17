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
var reqQueue = [];
var busy = false;
var accepts = [];

var extensionServer = new WebSocketServer(10719, "127.0.0.1");
extensionServer.onaccept = function (event) {
    reqQueue = [];
    extensionConnect = true;
    extws = event.socket;
    var reqOrigin = event.origin;
    console.log("Extension-web-socket set up, origin: " + reqOrigin);
    var reqOriginFirst45 = reqOrigin.slice(0, 44);
    //check if reqOriginF45 is equal to "safari-extension://com.ericsson.research.owr"
    if (reqOriginFirst45 == validExtenstionOrigin) extensionApproved = true;
    extws.onlcose = function (evt) {
        console.log("extws closed");
        extws = null;
    }
}


var server = new WebSocketServer(10717, "127.0.0.1");
server.onaccept = function (event) {
    var ws = event.socket;
    var origin = event.origin;
    console.log("channel socket origin: " + origin);
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
                    accepts[requestId] = response.acceptSourceInfos;
                    client.gotSources(response.acceptSourceInfos);
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
                if (response.name == "reject") {
                    console.log("reject received on socket");
                }
                clearBlock(); //clearing regardless of whether reponse was "accept", "reject" or "timeout"
            }

            function clearBlock () {
                if (reqQueue.length == 0) {
                    busy = false;
                } else {
                    //request queued up
                    var requestMessage = reqQueue.pop();
                    extws.send(JSON.stringify(requestMessage));
                    console.log("sent request popped from reqQueue to bar");
                    extws.onmessage = handleExtResponse;                    
                }
            }

            if (extensionConnect) { //If an extension ever did try to connect
                if (extws && extensionApproved) {
                    requestId = Math.floor(Math.random() * 40000);
                    console.log("sourceInfos: " + sourceInfos);
                    var requestMessage = {
                        "name": "request",
                        "origin": origin,
                        "Id": requestId,
                        "requestSourceInfos": sourceInfos
                    }
                    if (!busy) {
                        busy = true;
                        extws.send(JSON.stringify(requestMessage));
                        console.log("sent incoming request to bar");
                        extws.onmessage = handleExtResponse;

                    } else {
                        //outstanding request to extension; queue up
                        reqQueue.push(requestMessage); 
                        console.log("an extension is connected but busy serving (i.e. get accept or reject for) an gUM call. Pushing to reqQueue");
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
        var imageServerPort = 0;
        var videoRenderer;
        if (videoSources.length > 0) {
            videoRenderer = new owr.ImageRenderer();
            videoRenderer.set_source(videoSources[0]);

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

        var controller = new RenderController(audioRenderer, videoRenderer, imageServerPort, tag);
        renderControllers.push(controller);
        jsonRpc.exportFunctions(controller.setAudioMuted, controller.stop);
        var controllerRef = jsonRpc.createObjectRef(controller, "setAudioMuted", "stop");

        return { "controller": controllerRef, "port": imageServerPort };
    };

    jsonRpc.exportFunctions(rpcScope.createPeerHandler, rpcScope.requestSources, rpcScope.renderSources);

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
