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

var glib = imports.gi.GLib;
var gio = imports.gi.Gio;

function WebSocket() {
    if (arguments.length < 1) {
        print("*** WebSocket: not enough arguments");
        return;
    }

    var _this = this;
    var state = { "CONNECTING": 0, "OPEN": 1, "CLOSING": 2, "CLOSED": 3 };
    var readyState = state.OPEN;
    var prio = glib.PRIORITY_DEFAULT;
    var connection;
    var outputStream;
    var inputStream;
    var sendQueue = [];
    var socketClient;
    var protocolsArg;
    var protocol = "";

    if (!(arguments[0] instanceof gio.SocketConnection)) {
        // client socket mode
        var hostAndPort = arguments[0].substr(5); // trim "ws://"
        protocolsArg = arguments[1];
        socketClient = new gio.SocketClient({
            "type": gio.SocketType.STREAM,
            "protocol": gio.SocketProtocol.TCP
        });
        socketClient.connect_to_host_async(hostAndPort, 80, null, connectCallback);
        readyState = state.CONNECTING;
    } else {
        // dispatched from server socket
        connection = arguments[0];
        protocol = arguments[1];
        gotConnection();
    }

    function connectCallback(client, result) {
        connection = client.connect_to_host_finish(result, null);
        if (!connection)
            print("*** WebSocket: unable to create connection");

        connection.get_socket().set_blocking(false);
        connection.dataInputStream = new gio.DataInputStream({
            "base_stream": connection.get_input_stream(),
            "newline_type": gio.DataStreamNewlineType.ANY
        });

        sendHandshakeRequest(connection.get_output_stream(), function () {
            readHandshakeResponse(connection.dataInputStream, function (response) {
                protocol = response.headers["sec-websocket-protocol"] || "";

                gotConnection();

                if (typeof(_this.onopen) == "function")
                    _this.onopen();
            });
        });
    }

    function sendHandshakeRequest(outputStream, callback) {
        var key = [];
        for (var i = 0; i < 16; i++)
            key.push(Math.round(Math.random() * 255));
        var keyDigest = glib.base64_encode(key, key.length);

        var request = "GET /foo HTTP/1.1 101\r\n" +
              "Upgrade: websocket\r\n" +
              "Connection: Upgrade\r\n" +
              (protocolsArg ? "Sec-WebSocket-Protocol: " + protocolsArg + "\r\n" : "") +
              "Sec-WebSocket-Key: " + keyDigest + "\r\n\r\n";

        var buf = [];
        for (i = 0; i < request.length; i++)
            buf.push(request.charCodeAt(i));
        outputStream.write_async(buf, buf.length, prio, null, function (o, result) {
            outputStream.write_finish(result);
            outputStream.flush_async(prio, null, function (o, result) {
                outputStream.flush_finish(result);
                callback();
            });
        });
    }

    function readHandshakeResponse(inputStream, callback) {
        var response = { "headers": {} };
        function gotHeaderLine(inputStream, result) {
            var line = inputStream.read_line_finish_utf8(result);
            if (!line || !line.length) {
                callback(response);
                return;
            }

            if (!response.method) {
                var parts = line.split(" ");
                response.method = parts[0];
                response.url = parts[1];
                response.protocol = parts[2];
            } else {
                var match = line.match(/([A-Za-z0-9\-]+): *(.*)/);
                if (match && match.length > 2)
                    response.headers[match[1].toLowerCase()] = match[2];
            }

            inputStream.read_line_async(prio, null, gotHeaderLine);
        }

        inputStream.read_line_async(prio, null, gotHeaderLine);
    }

    function gotConnection() {
        readyState = state.OPEN;

        outputStream = connection.get_output_stream();
        inputStream = connection.dataInputStream;

        receiveFrame(gotFrame, gotCloseFrame, closeConnection);
    }

    function receiveFrame(frameCallback, closeFrameCallback, closeCallback) {
        var minHeaderLen = 2;
        // read minimal header
        inputStream.fill_async(minHeaderLen, prio, null, function (is, result) {
            if (!connection.is_connected()) {
                if (readyState != state.CLOSED)
                    print("websocket: unexpected state (connection is closed)");
                return;
            }

            var fillSize = inputStream.fill_finish(result);
            if (fillSize < minHeaderLen)
                return closeCallback();

            var opCode = inputStream.read_byte() & 0xf;
            var secondByte = inputStream.read_byte();
            var maskBit = secondByte & 0x80;
            var payloadLen = secondByte & 0x7f;

            var isCloseFrame = opCode == 0x8;

            var extraHeaderLen = 0;
            if (maskBit)
                extraHeaderLen += 4;
            if (payloadLen == 126)
                extraHeaderLen += 2;
            else if (payloadLen == 127)
                extraHeaderLen += 8;

            var mask = [];

            if (extraHeaderLen)
                // read extended header (if any), then payload
                readExtendedHeader();
            else
                // read payload directly (no extended header)
                readPayload();

            function readExtendedHeader() {
                inputStream.fill_async(extraHeaderLen, prio, null, function (is, result) {
                    var fillSize = inputStream.fill_finish(result);
                    if (fillSize < extraHeaderLen)
                        return closeCallback();

                    var payloadLenBytes = extraHeaderLen - (maskBit ? 4 : 0);

                    if (payloadLenBytes) {
                        payloadLen = 0;
                        for (var i = (8 * payloadLenBytes); i > 0; i -= 8)
                            payloadLen |= (inputStream.read_byte() << (i - 8));
                    }

                    if (maskBit)
                        for (var i = 0; i < 4; i++)
                            mask[i] = inputStream.read_byte();

                    readPayload();
                });
            }

            function readPayload() {
                if (payloadLen > inputStream.buffer_size) {
                    inputStream.buffer_size = Math.min(payloadLen, 20 * 1024 * 1024);
                    if (inputStream.buffer_size < payloadLen)
                        print("Warning: buffer too small to receive frame");
                }
                inputStream.fill_async(payloadLen, prio, null, function (is, result) {
                    var fillSize = inputStream.fill_finish(result);
                    if (fillSize < payloadLen)
                        return closeCallback();

                    var i;
                    var payload = [];
                    if (mask.length)
                        for (i = 0; i < payloadLen; i++)
                            payload[i] = inputStream.read_byte() ^ mask[i % 4];
                    else
                        for (i = 0; i < payloadLen; i++)
                            payload[i] = inputStream.read_byte();

                    if (isCloseFrame) {
                        var statusCode;
                        var reason;
                        if (payload.length >= 2) {
                            statusCode = (payload[0] << 8) | payload[1];
                            payload.splice(0, 2);
                            reason = String.fromCharCode.apply(this, payload);
                        }
                        closeFrameCallback(statusCode, reason);
                    } else {
                        var data = "";
                        for (i = 0; i < payload.length; i++)
                            data += String.fromCharCode(payload[i]);
                        frameCallback(decodeURIComponent(escape(data)));
                    }
                });
            }
        });
    }

    function closeConnection() {
        if (readyState == state.CLOSING || readyState == state.CLOSED)
            return;
        readyState = state.CLOSING;

        connection.close_async(prio, null, function(c, result) {
            connection.close_finish(result);
            readyState = state.CLOSED;
            if (typeof(_this.onclose) == "function")
                _this.onclose({ "wasClean": false, "code": 0, "reason": "" });
        });
    }

    function gotFrame(data) {
        if (typeof(_this.onmessage) == "function")
            _this.onmessage({"data": data});
        receiveFrame(gotFrame, gotCloseFrame, closeConnection);
    }

    function gotCloseFrame(statusCode, reason) {
        closeConnection();
    }

    function processSendQueue() {
        var i;
        var buf = [129];
        var message = unescape(encodeURIComponent(sendQueue[0]));
        var messageLen = message.length;
        if (messageLen < 126)
            buf[1] = messageLen & 0x7f;
        else {
            buf[1] = (messageLen <= 0xffff) ? 126 : 127;
            for (i = (messageLen <= 0xffff) ? 8 : 56 ; i >= 0; i -= 8)
                buf.push((messageLen & (0xff << i)) >> i);
        }

        for (i = 0; i < messageLen; i++)
            buf.push(message.charCodeAt(i));

        outputStream.write_async(buf, buf.length, prio, null, function (o, result) {
            outputStream.write_finish(result);
            outputStream.flush_async(prio, null, function (o, result) {
                outputStream.flush_finish(result);
                sendQueue.shift();
                if (sendQueue.length > 0)
                    processSendQueue();
            });
        });
    }

    Object.defineProperty(this, "CONNECTING", { "value": state.CONNECTING });
    Object.defineProperty(this, "OPEN", { "value": state.OPEN });
    Object.defineProperty(this, "CLOSING", { "value": state.CLOSING });
    Object.defineProperty(this, "CLOSED", { "value": state.CLOSED });

    Object.defineProperty(this, "readyState", {
        "get": function () {
            return readyState;
        }
    });

    Object.defineProperty(this, "protocol", {
        "get": function () {
            return protocol;
        }
    });

    this.onopen = null;
    this.onerror = null;
    this.onclose = null;

    this.close = function (code, reason) {
        closeConnection();
    };

    this.send = function (data) {
        if (readyState == state.CONNECTING)
            throw new Error("invalid.state.err");

        if (readyState != state.OPEN)
            return;

        if (sendQueue.push(data) == 1)
            processSendQueue();
    };

    this.toString = function () {
        return "[object WebSocket]";
    };
};

function WebSocketServer(port, bindAddress) {
    if (!bindAddress)
        bindAddress = "0.0.0.0";

    var _this = this;
    var prio = glib.PRIORITY_DEFAULT;
    var socketService = new gio.SocketService();
    socketService.add_address(new gio.InetSocketAddress({
        "address": new gio.InetAddress.from_string(bindAddress),
        "port": port
    }), gio.SocketType.STREAM, gio.SocketProtocol.TCP);

    socketService.signal.incoming.connect(function (service, connection) {
        connection.get_socket().set_blocking(false);
        var outputStream = connection.get_output_stream();
        var inputStream = new gio.DataInputStream({
            "base_stream": connection.get_input_stream(),
            "newline_type": gio.DataStreamNewlineType.ANY
        });
        connection.dataInputStream = inputStream;

        readHandshakeRequest(inputStream, function (request) {
            if (!request.headers["sec-websocket-key"]) {
                request.respond = function (response) {
                    sendFallbackResponse(outputStream, request, response, function () {
                        connection.close_async(prio, null, function(c, result) {
                            connection.close_finish(result);
                        });
                    });
                };
                if (typeof(_this.onrequest) == "function")
                    _this.onrequest({ "type": "request", "request": request, "target": _this });
                else
                    request.respond({ "status": 503 });

                return;
            }

            sendHandshakeResponse(outputStream, request, function (serverProtocol, origin) {
                if (typeof(_this.onaccept) == "function")
                    _this.onaccept({
                        "socket": new WebSocket(connection, serverProtocol),
                        "origin": origin
                    });
            });
        });

        return false;
    });
    socketService.start();

    function readHandshakeRequest(inputStream, callback) {
        var request = { "headers": {} };

        function gotHeaderLine(inputStream, result) {
            var line = inputStream.read_line_finish_utf8(result);
            if (!line || !line.length) {
                callback(request);
                return;
            }

            if (!request.method) {
                var parts = line.split(" ");
                request.method = parts[0];
                request.url = parts[1];
                request.protocol = parts[2];
            } else {
                var match = line.match(/([A-Za-z0-9\-]+): *(.*)/);
                if (match && match.length > 2)
                    request.headers[match[1].toLowerCase()] = match[2];
            }

            inputStream.read_line_async(prio, null, gotHeaderLine);
        }

        inputStream.read_line_async(prio, null, gotHeaderLine);
    }

    function sendHandshakeResponse(outputStream, request, callback) {
        var i;
        var key = [];
        var keyHeader = request.headers["sec-websocket-key"];
        for (i = 0; i < keyHeader.length; i++)
            key.push(keyHeader.charCodeAt(i));
        var token = key.concat([
            50, 53, 56, 69, 65, 70, 65, 53, 45, 69, 57, 49, 52, 45, 52, 55, 68,
            65, 45, 57, 53, 67, 65, 45, 67, 53, 65, 66, 48, 68, 67, 56, 53, 66,
            49, 49
        ]);
        var hash = glib.compute_checksum_for_data(glib.ChecksumType.SHA1, token, token.length);
        var digest = [];
        for (i = 0; i < hash.length; i += 2)
            digest.push(parseInt(hash.slice(i, i + 2), 16));
        var acceptKey = glib.base64_encode(digest, digest.length);
        var serverProtocol = request.headers["sec-websocket-protocol"] || "";
        var origin = request.headers["origin"];

        var response =  "HTTP/1.1 101 Switching Protocols\r\n" +
                        "Upgrade: websocket\r\n" +
                        "Connection: Upgrade\r\n" +
                        (serverProtocol ? "Sec-WebSocket-Protocol: " +
                            serverProtocol + "\r\n" : "") +
                        "Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n";
        var buf = [];
        for (i = 0; i < response.length; i++)
            buf.push(response.charCodeAt(i));
        outputStream.write_async(buf, buf.length, prio, null, function (o, result) {
            outputStream.write_finish(result);
            outputStream.flush_async(prio, null, function (o, result) {
                outputStream.flush_finish(result);
                callback(serverProtocol, origin);
            });
        });
    }

    function sendFallbackResponse(outputStream, request, response, callback) {
        var status, phrase, headers, body;
        if (typeof(response) != "object")
            response = {};
        status = response.status || 503;
        phrase = response.statusText || {
            100: "Continue", 101: "Switching Protocols",
            200: "OK", 201: "Created", 202: "Accepted", 204: "No Content",
            203: "Non-Authoritative Information", 205: "Reset Content",
            206: "Partial Content", 300: "Multiple Choices",
            301: "Moved Permanently", 302: "Found", 303: "See Other",
            304: "Not Modified", 305: "Use Proxy", 307: "Temporary Redirect",
            400: "Bad Request", 401: "Unauthorized", 402: "Payment Required",
            403: "Forbidden", 404: "Not Found", 405: "Method Not Allowed",
            406: "Not Acceptable" , 407: "Proxy Authentication Required",
            408: "Request Timeout", 409: "Conflict", 410: "Gone",
            411: "Length Required", 412: "Precondition Failed",
            413: "Request Entity Too Large", 414: "Request-URI Too Long",
            415: "Unsupported Media Type", 417: "Expectation Failed",
            416: "Request Range Not Satisfiable",
            500: "Internal Server Error", 501: "Not Implemented",
            502: "Bad Gateway", 503: "Service Unavailable",
            504: "Gateway Timeout", 505: "HTTP Version Not Supported"
        }[status] || "Other";
        body = typeof(response.body) == "string" ? response.body : "";
        headers = typeof(response.headers) == "object" ? response.headers : {};
        headers["Connection"] = "close";
        headers["Content-Length"] = body.length;

        var responseText = "HTTP/1.1 " + status + " " + phrase + "\r\n";
        for (var key in headers)
            if (headers.hasOwnProperty(key))
                responseText += key + ": " + headers[key] + "\r\n";
        responseText += "\r\n" + unescape(encodeURIComponent(body));
        var buf = [];
        for (var i = 0; i < responseText.length; i++)
            buf.push(responseText.charCodeAt(i));
        outputStream.write_async(buf, buf.length, prio, null, function (o, result) {
            outputStream.write_finish(result);
            outputStream.flush_async(prio, null, function (o, result) {
                outputStream.flush_finish(result);
                callback();
            });
        });
    }

    this.onaccept = null;
    this.onrequest = null;

    this.stop = function () {
        socketService.stop();
    };

    this.toString = function () {
        return "[object WebSocketServer]";
    };
}
