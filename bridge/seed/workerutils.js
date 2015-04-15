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

var setTimeout, setInterval, clearTimeout, clearInterval;

(function () {
    function create_timeout_add(repeat) {
        return function (func, delay) {
            if (typeof(delay) != "number" || delay < 15)
                delay = 15;
            var args = Array.prototype.slice.call(arguments, 2);
            return glib.timeout_add(glib.PRIORITY_DEFAULT, delay || 0, function () {
                func.apply(this, args);
                return repeat;
            });
        };
    }

    setTimeout = create_timeout_add(false);
    setInterval = create_timeout_add(true);

    clearTimeout = clearInterval = function (tag) {
        glib.Source.remove(tag);
    }
})();

function btoa(data) {
    var buf = [];
    var d = String(data);
    for (var i = 0; i < d.length; i++)
        buf.push(d.charCodeAt(i));
    return glib.base64_encode(buf, buf.length);
}

function atob(data) {
    var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    var buf = [];
    var a, b;
    var pos, offset, mask;
    for (var i = 0; i < data.length; i++) {
        a = chars.indexOf(data[i]);
        if (a < 0)
            break;

        pos = i % 4;
        if (pos) {
            b |= a >> (4 - offset);
            buf.push(b);
        }

        offset = pos * 2;
        mask = 0x3f >> offset;
        b = (a & mask) << (offset + 2);
    }

    return String.fromCharCode.apply(this, buf);
}

if (typeof(ArrayBuffer) == "undefined" || typeof(Uint8Array) == "undefined") {
    var ArrayBuffer = function (length) {
        this.length = length;
    };

    var Uint8Array = function (arrayBuffer) {
        var _this = this;
        Object.defineProperty(_this, "length", {
            "get": function () { return arrayBuffer.length; }
        });
        for (var i = 0; i < arrayBuffer.length; i++) {
            (function (j) {
                Object.defineProperty(_this, j, {
                    "get": function () { return arrayBuffer[j]; },
                    "set": function (value) { arrayBuffer[j] = value; }
                });
            })(i);
        }
    };
}

var crypto = {};
crypto.subtle = {
    "digest": function (algorithm, data) {
        function then(resolve, reject) {
            if (!algorithm || !data) {
                if (typeof(reject) == "function")
                    reject(new TypeError("invalid.argument"));
                return;
            }

            var csumtype = (algorithm.name || algorithm).toString().toUpperCase().replace(/-/, "");

            if (!glib.ChecksumType[csumtype]) {
                if (typeof(reject) == "function")
                    reject(new Error("not.supported"));
                return;
            }

            var i;
            var d = [];
            var bufferView = new Uint8Array(data);
            for (i = 0; i < bufferView.length; i++)
                d[i] = bufferView[i];
            var hash = glib.compute_checksum_for_data(glib.ChecksumType[csumtype], d, d.length);
            var digest = new ArrayBuffer(hash.length / 2);
            var bufView = new Uint8Array(digest);
            for (i = 0; i < hash.length; i += 2)
                bufView[i / 2] = parseInt(hash.slice(i, i + 2), 16);
            if (typeof(resolve) == "function")
                resolve(digest);
        }
        return {"then": function (resolve, reject) { setTimeout(then, 0, resolve, reject); }};
    }
};
