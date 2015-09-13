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

var domObject = (function () {

    function createAttributeDescriptor(name, attributes) {
        return {
            "get": function () {
                var attribute = attributes[name];
                return typeof attribute == "function" ? attribute() : attribute;
            },
            "enumerable": true
        };
    }

    return {
        "addReadOnlyAttributes": function (target, attributes) {
            for (var name in attributes)
                Object.defineProperty(target, name, createAttributeDescriptor(name, attributes));
        },

        "addConstants": function (target, constants) {
            for (var name in constants)
                Object.defineProperty(target, name, {
                    "value": constants[name],
                    "enumerable": true
                });
        }
    };
})();

function EventTarget(attributes) {
    var _this = this;
    var listenersMap = {};

    if (attributes)
        addEventListenerAttributes(this, attributes);

    this.addEventListener = function (type, listener, useCapture) {
        if (typeof(listener) != "function")
            throw new TypeError("listener argument (" + listener + ") is not a function");
        var listeners = listenersMap[type];
        if (!listeners)
            listeners = listenersMap[type] = [];

        if (listeners.indexOf(listener) < 0)
            listeners.push(listener);
    };

    this.removeEventListener = function (type, listener, useCapture) {
        var listeners = listenersMap[type];
        if (!listeners)
            return;

        var i = listeners.indexOf(listener);
        if (i >= 0)
            listeners.splice(i, 1);
    };

    this.dispatchEvent = function (evt) {
        var listeners = [];

        var attributeListener = _this["on" + evt.type];
        if (attributeListener)
            listeners.push(attributeListener);

        if (listenersMap[evt.type])
            Array.prototype.push.apply(listeners, listenersMap[evt.type]);

        var errors = [];
        var result = true;
        listeners.forEach(function (listener) {
            try {
                result = !(listener(evt) === false) && result;
            } catch (e) {
                errors.push(e);
            }
        });

        errors.forEach(function (e) {
            setTimeout(function () {
                throw e;
            });
        });

        return result;
    };

    function addEventListenerAttributes(target, attributes) {
        for (var name in attributes)
            Object.defineProperty(target, name, createEventListenerDescriptor(name, attributes));
    }

    function createEventListenerDescriptor(name, attributes) {
        return {
            "get": function () { return attributes[name]; },
            "set": function (cb) { attributes[name] = (typeof(cb) == "function") ? cb : null; },
            "enumerable": true
        };
    }
}

function checkDictionary(name, dict, typeMap) {
    for (var memberName in dict) {
        if (!dict.hasOwnProperty(memberName) || !typeMap.hasOwnProperty(memberName))
            continue;

        var message = name + ": Dictionary member " + memberName;
        checkType(message, dict[memberName], typeMap[memberName]);
    }
}

function checkArguments(name, argsTypeTemplate, numRequired, args) {
    var error = getArgumentsError(argsTypeTemplate, numRequired, args);
    if (error)
        throw createError("TypeError", name + ": " + error);
}

function checkType(name, value, typeTemplate) {
    var error = getTypeError(name, value, typeTemplate);
    if (error)
        throw createError("TypeError", name + ": " + error);
}

function getArgumentsError(argsTypeTemplate, numRequired, args) {
    if (args.length < numRequired)
        return "Too few arguments (got " + args.length + " expected " + numRequired + ")";

    var typeTemplates = argsTypeTemplate.split(/\s*,\s*/);

    for (var i = 0; i < args.length && i < typeTemplates.length; i++) {
        var prefix = "Argument " + (i + 1);
        var error = getTypeError(prefix, args[i], typeTemplates[i]);
        if (error)
            return error;
    }
    return null;
}

function getTypeError(prefix, value, typeTemplate) {
    var expectedTypes = typeTemplate.split(/\s*\|\s*/);
    if (!canConvert(value, expectedTypes))
        return prefix + " is of wrong type (expected " + expectedTypes.join(" or ") + ")";
    return null;
}

function canConvert(value, expectedTypes) {
    var type = typeof value;
    for (var i = 0; i < expectedTypes.length; i++) {
        var expectedType = expectedTypes[i];
        if (expectedType == "string" || expectedType == "boolean")
            return true; // type conversion will never throw
        if (expectedType == "number") {
            var asNumber = +value;
            if (!isNaN(asNumber) && asNumber != -Infinity && asNumber != Infinity)
                return true;
        }
        if (expectedType == "dictionary") { // object, undefined and null can be converted
            if (type == "object" || type == "undefined" || type == "null")
                return true;
        }
        if (type == "object") {
            if (expectedType == "object")
                return true;
            // could be a specific object type or host object (e.g. Array)
            var constructor = self[expectedType];
            if (constructor && value instanceof constructor)
                return true;
        }
        if (type == expectedType && expectedType == "function")
            return true;
    }
    return false;
}

function getDictionaryMember(dict, name, type, defaultValue) {
    if (!dict.hasOwnProperty(name) || dict[name] == null)
        return defaultValue;
    if (type == "string")
        return String(dict[name]);
    if (type == "boolean")
        return !!dict[name];
    if (type == "number")
        return +dict[name];
}

function randomNumber(bits) {
    return Math.floor(Math.random() * Math.pow(2, bits));
}

function randomString(length) {
    var randomValues = new Uint8Array(Math.ceil(length * 3 / 4));
    crypto.getRandomValues(randomValues);
    return btoa(String.fromCharCode.apply(null, randomValues)).substr(0, length);
}

function createError(name, message) {
    var constructor = self[name] || self.Error;
    var error = new constructor(message);
    error.name = name;
    return error;
}

function entityReplace(str) {
    return escape(str).replace(/%([0-9A-F]{2})/g, function (match) {
        return "&#" + parseInt(match.substr(1), 16) + ";"
    });
}
