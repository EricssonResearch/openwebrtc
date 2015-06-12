/*
 * Copyright (C) 2009-2015 Ericsson AB. All rights reserved.
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

// A lightweight JavaScript-to-JavaScript JSON RPC utility.
//
// msgLink   : An object supporting the HTML5 Web Messaging API
//             (http://dev.w3.org/html5/postmsg) used for communicating between
//             the RPC endpoints.
// options   : An object containing options used to configure this RPC endpoint.
//             Supported options:
//             - unrestricted
//                 Disables the security feature that requires this enpoint to
//                 export functions before the other endpoint can call them.
//             - noRemoteExceptions
//                 Prevents this endpoint from throwing exceptions returned
//                 from the other endpoint as a result of a call.
//             - scope
//                 The scope from which the other endpoint can import functions.
//

"use strict";

var JsonRpc = function (msgLink, optionsOrRestricted) {
    var thisObj = this;
    var id = String(Math.random()).substr(2);
    var count = 0;
    var callbacks = {};
    var exports = [];
    var referencedObjects = {};
    var refObjects = {};
    var onerror;

    var restricted = !!optionsOrRestricted;
    var noRemoteExceptions = false;
    var scope;

    if (typeof optionsOrRestricted == "object") {
        var options = optionsOrRestricted;

        restricted = !options.unrestricted;
        noRemoteExceptions = !!options.noRemoteExceptions;
        scope = options.scope ? options.scope : self;
    } else
        scope = self;

    // Setter replaces the message link provided when constructing the object.
    // This enables an RPC to connect to a new endpoint while keeping internal
    // data such as imported and exported functions.
    Object.defineProperty(this, "messageLink", {
        "get": function () { return msgLink; },
        "set": function (ml) {
            msgLink = ml;
            msgLink.onmessage = onmessage;
        }
    });

    Object.defineProperty(this, "scope", {
        "get": function () { return scope; },
        "set": function (s) { scope = s; }
    });

    Object.defineProperty(this, "onerror", {
        "get": function () { return onerror; },
        "set": function (cb) { onerror = cb instanceof Function ? cb : null; },
        "enumerable": true
    });

    // Import one or several functions from the other side to make them callable
    // on this RPC object. The functions can be imported regardless if they
    // exist on the other side or not.
    // args: [fnames | fname1...fnamen]
    this.importFunctions = function () {
        var args = arguments[0] instanceof Array ? arguments[0] : arguments;
        internalImport(this, args);
    };

    // Export one or several functions on this RPC object. If this RPC object is
    // restricted, a function needs to be exported in order to make it callable
    // from the other side.
    // args: [functions | function1...functionn]
    this.exportFunctions = function () {
        var args = arguments[0] instanceof Array ? arguments[0] : arguments;
        for (var i = 0; i < args.length; i++) {
            for (var j = 0; j < exports.length; j++) {
                if (exports[j] === args[i])
                    break;
            }
            if (j == exports.length)
                exports.push(args[i]);
        }
    };

    // Remove one or several functions from the list of exported in order to
    // make them non-callable from the other side.
    // args: [functions | function1...functionn]
    this.unexportFunctions = function () {
        var args = arguments[0] instanceof Array ? arguments[0] : arguments;
        for (var i = 0; i < args.length; i++) {
            for (var j = 0; j < exports.length; j++) {
                if (exports[j] === args[i]) {
                    exports.splice(j, 1);
                    break;
                }
            }
        }
    };

    // Create a reference object which can be sent to the other side as an
    // argument to an RPC call. Calling an exported function on the reference
    // object results in calling the corresponding function on the source
    // object. The exported functions must be properties on the source objects.
    // args: srcObj [, fpnames | fpname1...fpnamen]
    this.createObjectRef = function () {
        var srcObj = arguments[0];
        var refObj = {
            "__refId": id + "_" + count++,
            "__methods": []
        };
        var i = 1;
        var args = arguments[1] instanceof Array ? arguments[i--] : arguments;

        // FIXME: need to handle circular references (e.g. double linked list)
        for (; i < args.length; i++)
            if (srcObj[args[i]] instanceof Function)
                refObj.__methods.push(args[i]);
        referencedObjects[refObj.__refId] = srcObj;
        return refObj;
    };

    // Remove the reference object (i.e. the link between the reference and the
    // source object).
    this.removeObjectRef = function (obj) {
        var refId;
        if (obj.__refId)
            refId = obj.__refId;
        else for (var pname in referencedObjects) {
            if (referencedObjects[pname] === obj) {
                refId = pname;
                break;
            }
        }
        if (refId)
            delete referencedObjects[refId];
    };

    // -----------------------------------------------------------------------------

    function internalImport(destObj, names, refObjId) {
        for (var i = 0; i < names.length; i++) {
            var nparts = names[i].split(".");
            var targetObj = destObj;
            for (var j = 0; j < nparts.length-1; j++) {
                var n = nparts[j];
                if (!targetObj[n])
                    targetObj[n] = {};
                targetObj = targetObj[n];
            }
            targetObj[nparts[nparts.length-1]] = (function (name) {
                return function () {
                    var requestId = null;
                    var params = [];
                    params.push.apply(params, arguments);
                    if (params[params.length-1] instanceof Function) {
                        requestId = id + "_" + count++;
                        callbacks[requestId] = params.pop();
                    }
                    for (var j = 0; j < params.length; j++) {
                        var p = params[j];
                        if (p instanceof ArrayBuffer || ArrayBuffer.isView(p))
                            params[j] = encodeArrayBufferArgument(p);
                        else
                            substituteRefObject(params, j);
                    }

                    var request = {
                        "id": requestId,
                        "method": name,
                        "params": params
                    };
                    if (refObjId)
                        request.__refId = refObjId;
                    msgLink.postMessage(JSON.stringify(request));
                };
            })(names[i]);
        }
    }

    function encodeArrayBufferArgument(buffer) {
        return {
            "__argumentType": buffer.constructor.name,
            "base64": btoa(Array.prototype.map.call(new Uint8Array(buffer),
                function (byte) {
                    return String.fromCharCode(byte);
                }).join(""))
        };
    }

    function decodeArrayBufferArgument(obj) {
        var data = atob(obj.base64 || "");
        var arr = new Uint8Array(data.length);
        for (var i = 0; i < data.length; i++)
            arr[i] = data.charCodeAt(i);

        var constructor = self[obj.__argumentType];
        if (constructor && constructor.BYTES_PER_ELEMENT)
            return new constructor(arr);

        return arr.buffer;
    }

    function substituteRefObject(parent, pname) {
        if (!parent[pname] || typeof parent[pname] != "object")
            return;

        var obj = parent[pname];
        for (var refId in refObjects) {
            if (refObjects[refId] === obj) {
                parent[pname] = { "__refId": refId };
                return;
            }
        }

        for (var p in obj) {
            if (obj.hasOwnProperty(p))
                substituteRefObject(obj, p);
        }
    }

    function substituteReferencedObject(parent, pname) {
        if (!parent[pname] || typeof parent[pname] != "object")
            return;

        var obj = parent[pname];
        if (obj.__refId && referencedObjects[obj.__refId]) {
            parent[pname] = referencedObjects[obj.__refId];
            return;
        }

        for (var p in obj) {
            if (obj.hasOwnProperty(p))
                substituteReferencedObject(obj, p);
        }
    }

    function prepareRefObj(obj) {
        if (!obj)
            return;
        try { // give up if __refId can't be read from obj
            obj.__refId;
        } catch (e) {
            return;
        }
        if (obj.__refId) {
            internalImport(obj, obj.__methods, obj.__refId);
            refObjects[obj.__refId] = obj;
            delete obj.__methods;
            delete obj.__refId;
            return;
        }
        if (typeof obj == "object") {
            for (var pname in obj) {
                if (obj.hasOwnProperty(pname))
                    prepareRefObj(obj[pname]);
            }
        }
    }

    function onmessage(evt) {
        var msg = JSON.parse(evt.data);
        if (msg.method) {
            var nparts = msg.method.split(".");
            var obj = msg.__refId ? referencedObjects[msg.__refId] : scope;
            if (!obj)
                throw "referenced object not found";
            for (var i = 0; i < nparts.length-1; i++) {
                obj = obj[nparts[i]];
                if (!obj) {
                    obj = {};
                    break;
                }
            }
            var f = obj[nparts[nparts.length-1]];
            if (restricted) {
                for (var j = 0; j < exports.length; j++) {
                    if (f === exports[j])
                        break;
                }
                if (j == exports.length)
                    f = "not.exported";
            }
            var response = {};
            response.id = msg.id;
            if (f instanceof Function) {
                try {
                    for (var i = 0; i < msg.params.length; i++) {
                        var p = msg.params[i];
                        if (p && p.__argumentType)
                            msg.params[i] = decodeArrayBufferArgument(p);
                        else
                            substituteReferencedObject(msg.params, i);
                    }
                    prepareRefObj(msg.params);
                    //var functionScope = !msg.__refId ? thisObj : obj; // FIXME: !!
                    var functionScope = !msg.__refId && obj == scope ? thisObj : obj;
                    response.result = f.apply(functionScope, msg.params);
                    var resultType = response.__resultType = typeof response.result;
                    if (resultType == "function" || resultType == "undefined")
                        response.result = null;
                }
                catch (e) {
                    response.error = msg.method + ": " + (e.message || e);
                }
            }
            else if (f == "not.exported")
                response.error = msg.method + ": restricted mode and not exported";
            else
                response.error = msg.method + ": not a function";

            if (msg.id != null || response.error)
                msgLink.postMessage(JSON.stringify(response));
        }
        else if (msg.hasOwnProperty("result")) {
            var cb = callbacks[msg.id];
            if (cb) {
                delete callbacks[msg.id];
                if (msg.__resultType == "undefined")
                    delete msg.result;
                else if (msg.__resultType == "function")
                    msg.result = function () { throw "can't call remote function"; };
                prepareRefObj(msg.result);
                cb(msg.result);
            }
        }
        else if (msg.error) {
            if (!noRemoteExceptions)
                throw msg.error;
            else if (onerror)
                onerror({
                    "type": "error",
                    "message": msg.error,
                    "filename": "",
                    "lineno": 0,
                    "colno": 0,
                    "error": null
                });
        }
    }
    msgLink.onmessage = onmessage;
};

// for node.js
if (typeof exports !== "undefined") {
    global.btoa = function (s) {
        return new Buffer(s).toString("base64");
    };
    global.atob = function (s) {
        return new Buffer(s, "base64").toString();
    };
    module.exports = JsonRpc;
}
