#!/usr/bin/env python

# Copyright (c) 2014, Ericsson AB. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or other
# materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
# OF SUCH DAMAGE.

import xml.etree.ElementTree as ET

NS = '{http://www.gtk.org/introspection/core/1.0}'
C_NS = '{http://www.gtk.org/introspection/c/1.0}'
GLIB_NS = '{http://www.gtk.org/introspection/glib/1.0}'

TAG_CLASS = NS + 'class'
TAG_NAMESPACE = NS + 'namespace'
TAG_INCLUDE = NS + 'include'
TAG_CONSTRUCTOR = NS + 'constructor'
TAG_RETURN_VALUE = NS + 'return-value'
TAG_TYPE = NS + 'type'
TAG_PARAMETERS = NS + 'parameters'
TAG_VIRTUAL_METHOD = NS + 'virtual-method'
TAG_PARAMETER = NS + 'parameter'
TAG_PROPERTY = NS + 'property'
TAG_RECORD = NS + 'record'
TAG_FIELD = NS + 'field'
TAG_ENUMERATION = NS + 'enumeration'
TAG_MEMBER = NS + 'member'
TAG_CALLBACK = NS + 'callback'
TAG_INSTANCE_PARAMETER = NS + 'instance-parameter'
TAG_METHOD = NS + 'method'
TAG_BITFIELD = NS + 'bitfield'
TAG_FUNCTION = NS + 'function'
TAG_SIGNAL = GLIB_NS + 'signal'

ATTR_NAME = 'name'
ATTR_WHEN = 'when'
ATTR_VALUE = 'value'
ATTR_PARENT = 'parent'
ATTR_READABLE = 'readable'
ATTR_WRITABLE = 'writable'
ATTR_CONSTRUCT_ONLY = 'construct-only'
ATTR_TRANSFER_ONWERSHIP = 'transfer-ownership'

ATTR_C_IDENTIFIER_PREFIXES = C_NS + 'identifier-prefixes'
ATTR_C_IDENTIFIER = C_NS + 'identifier'
ATTR_C_SYMBOL_PREFIXES = C_NS + 'symbol-prefixes'
ATTR_C_SYMBOL_PREFIX = C_NS + 'symbol-prefix'
ATTR_C_TYPE = C_NS + 'type'

ATTR_GLIB_NICK = GLIB_NS + 'nick'
ATTR_GLIB_TYPE_NAME = GLIB_NS + 'type-name'
ATTR_GLIB_GET_TYPE = GLIB_NS + 'get-type'


TYPE_TABLE_GIR_TO_JAVA = {
    'none': 'void',
    'utf8': 'java.lang.String',
    'gchar': 'char',
    'guchar': 'char',
    'gint': 'int',
    'guint': 'int',
    'gint64': 'long',
    'guint64': 'long',
    'gboolean': 'boolean',
    'gdouble': 'double',
    'guintptr': 'long',
    'gpointer': 'long',
    'GLib.List': 'java.util.List<>',
    'GLib.HashTable': 'java.util.Map<>'
}

TYPE_TABLE_GIR_TO_JNI = {
    'none': 'void',
    'utf8': 'jstring',
    'gchar': 'jbyte',
    'guchar': 'jbyte',
    'gint': 'jint',
    'guint': 'jint',
    'gint64': 'jlong',
    'guint64': 'jlong',
    'gboolean': 'jboolean',
    'gfloat': 'jfloat',
    'gdouble': 'jdouble',
    'guintptr': 'jlong',
    'gpointer': 'jlong',
    'GLib.List': 'jobject',
    'GLib.HashTable': 'jobject'
}


def title_case(st):
    return ''.join(x for x in st.title() if x.isalpha())

def camel_case(st):
    st = ''.join(x for x in st.title() if x.isalpha())
    return st[0].lower() + st[1:]

def snake_case(st):
    return '_'.join(w.lower() for w in re.split("([A-Z][a-z]*)", st) if w != '')

def to_jnitype(type):
    type = TYPE_TABLE_GIR_TO_JNI.get(type)
    return type or 'jobject'

def to_javatype(type):
    return TYPE_TABLE_GIR_TO_JAVA.get(type) or type

def parse_type_tag(tag, enums):
    inner_tags = tag.findall(TAG_TYPE)
    gir_type = tag.get(ATTR_NAME)
    c_type = tag.get(ATTR_C_TYPE)

    enum = enums.get(gir_type)
    if enum is not None:
        if enum['bitfield']:
            c_type = enum['c_name']
            gir_type = 'gint'
        else:
            c_type = enum['c_name']
            gir_type = enum['name']

    java_type = to_javatype(gir_type)
    jni_type = to_jnitype(gir_type)

    if java_type == 'WindowHandle':
        java_type = 'android.view.Surface'

    return dict(
        gir = gir_type,
        java = java_type,
        jni = jni_type,
        c = c_type,
        inner = [parse_type_tag(t, enums) for t in inner_tags] or None
    )


def parse_parameters(elem, enums):
    parameters = []
    tags = elem.findall(TAG_PARAMETERS + '/' + TAG_PARAMETER)
    if tags is not None:
        for tag in tags:
            name = tag.get(ATTR_NAME)
            types = parse_type_tag(tag.find(TAG_TYPE), enums)
            parameters.append(dict(
                title_name = title_case(name),
                camel_name = camel_case(name),
                c_name = name,
                transfer = tag.get(ATTR_TRANSFER_ONWERSHIP),
                types = types
            ))
    return parameters


def parse_return_value(elem, enums):
    tag = elem.find(TAG_RETURN_VALUE)
    return dict(
        transfer = tag.get(ATTR_TRANSFER_ONWERSHIP),
        types = parse_type_tag(tag.find(TAG_TYPE), enums)
    ).items()


def parse_callback(top, enums):
    name = top.get(ATTR_NAME)
    result = dict(dict(
        title_name = name,
        camel_name = 'on' + name.replace('Callback', ''),
        name = name,
        c_name = top.get(ATTR_C_TYPE),
        parameters = parse_parameters(top, enums),
    ).items() + parse_return_value(top, enums))

    return result


def parse_class(top, enums):
    parent = None
    constructor = None
    properties = []
    methods = []
    functions = []
    signals = []

    parent = top.get(ATTR_PARENT)
    if parent == 'GObject.Object':
        parent = None

    for tag in top.findall(TAG_PROPERTY):
        readable = tag.get(ATTR_READABLE)
        writable = tag.get(ATTR_WRITABLE)
        properties.append(dict(
            title_name = title_case(tag.get(ATTR_NAME)),
            camel_name = camel_case(tag.get(ATTR_NAME)),
            c_name = tag.get(ATTR_NAME),
            readable = bool(readable if readable is not None else True),
            writable = bool(writable if writable is not None else True),
            construct_only = bool(tag.get(ATTR_CONSTRUCT_ONLY)),
            transfer = tag.get(ATTR_TRANSFER_ONWERSHIP),
            types = parse_type_tag(tag.find(TAG_TYPE), enums)
        ))

    for tag in top.findall(TAG_METHOD):
        name = tag.get(ATTR_NAME)
        methods.append(dict(dict(
            title_name = title_case(name),
            camel_name = camel_case(name),
            c_name = tag.get(ATTR_C_IDENTIFIER),
            short_c_name = name,
            parameters = parse_parameters(tag, enums),
        ).items() + parse_return_value(tag, enums)))

    for tag in top.findall(TAG_FUNCTION):
        name = tag.get(ATTR_NAME)
        functions.append(dict(dict(
            title_name = title_case(name),
            camel_name = camel_case(name),
            c_name = tag.get(ATTR_C_IDENTIFIER),
            short_c_name = name,
            parameters = parse_parameters(tag, enums),
        ).items() + parse_return_value(tag, enums)))

    for tag in top.findall(TAG_SIGNAL):
        name = tag.get(ATTR_NAME)
        if name == 'on-new-stats': # TODO: Implement hash table conversion
            continue
        signals.append(dict(dict(
            title_name = title_case(name[3:] if name[:3] == 'on-' else name),
            camel_name = camel_case(name),
            c_name = name,
            parameters = parse_parameters(tag, enums),
            when = tag.get(ATTR_WHEN)
        ).items() + parse_return_value(tag, enums)))

    constructor_tag = top.find(TAG_CONSTRUCTOR)

    if constructor_tag is not None:
        tag = constructor_tag
        parameters = parse_parameters(tag, enums)
        constructor = dict(dict(
            title_name = 'NativeConstructor',
            camel_name = 'nativeConstructor',
            c_name = tag.get(ATTR_C_IDENTIFIER),
            short_c_name = tag.get(ATTR_NAME),
            parameters = parse_parameters(tag, enums),
        ).items() + parse_return_value(tag, enums))

    return dict(
        c_name = top.get(ATTR_C_TYPE),
        title_name = 'Self',
        camel_name = 'self',
        symbol_prefix = top.get(ATTR_C_SYMBOL_PREFIX),
        name = top.get(ATTR_NAME),
        parent = parent,
        constructor = constructor,
        properties = properties,
        methods = methods,
        functions = functions,
        signals = signals
    )


def parse_enum(top):
    members = []

    name = top.get(ATTR_NAME)
    glib_type = top.get(ATTR_GLIB_TYPE_NAME)
    bitfield = top.tag == TAG_BITFIELD

    if glib_type is not None:
        return name

    for member in top.findall(TAG_MEMBER):
        members.append(dict(
            original_name = member.get(ATTR_NAME),
            name = member.get(ATTR_NAME).upper(),
            value = member.get(ATTR_VALUE),
            c_name = member.get(ATTR_C_IDENTIFIER)
        ))

    return dict(
        name = name,
        c_name = top.get(ATTR_C_TYPE),
        members = members,
        bitfield = bitfield
    )


def parse_function(top, enums):
    name = top.get(ATTR_NAME)

    return dict(dict(
        title_name = title_case(name),
        camel_name = camel_case(name),
        c_name = top.get(ATTR_C_IDENTIFIER),
        short_c_name = name,
        parameters = parse_parameters(top, enums)
    ).items() + parse_return_value(top, enums))


def parse_namespace(namespace):
    callbacks = {}
    classes = []
    enums = {}
    glib_enums = []
    functions = []

    for top in namespace:
        if top.tag == TAG_ENUMERATION or top.tag == TAG_BITFIELD:
            enum = parse_enum(top)
            if type(enum) is str:
                glib_enums.append(enum)
            else:
                enums[enum['name']] = enum

    for glib_name in glib_enums:
        for name, enum in enums.items():
            if glib_name[0:-1] == name:
                enums[glib_name] = enum

    for top in namespace:
        if top.tag == TAG_CLASS:
            classes.append(parse_class(top, enums))

    for clazz in classes:
        parent = clazz['parent']
        if parent is not None:
            for parentclass in (c for c in classes if c['name'] == parent):
                parentclass['is_parent'] = True

    for top in namespace:
        if top.tag == TAG_CALLBACK:
            callback = parse_callback(top, enums)
            callbacks[callback['name']] = callback

    for top in namespace: # TODO: GMainContext implementation?
        if top.tag == TAG_FUNCTION and top.get(ATTR_NAME) not in [
                'init_with_main_context']:
            functions.append(parse_function(top, enums))

    return dict(
        name = namespace.get(ATTR_NAME),
        callbacks = callbacks,
        classes = classes,
        enums = enums,
        functions = functions,
        symbol_prefix = namespace.get(ATTR_C_SYMBOL_PREFIXES),
        identifier_prefix = namespace.get(ATTR_C_IDENTIFIER_PREFIXES)
    )


def parse_gir_file(path):
    tree = ET.parse(path)
    root = tree.getroot()
    namespaces = []

    for elem in root:
        if elem.tag == TAG_NAMESPACE:
            namespaces.append(parse_namespace(elem))

    return namespaces


