# Copyright (c) 2014-2015, Ericsson AB. All rights reserved.
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

from __future__ import print_function
import xml.etree.ElementTree as ET
import itertools
from standard_types import VoidType, IntType, LongPtrType, GParamSpecType, JObjectWrapperType
from standard_types import ClassCallbackMetaType, GObjectMetaType, CallbackMetaType
from standard_types import EnumMetaType, BitfieldMetaType, GWeakRefType, JDestroyType
from copy import copy

NS = '{http://www.gtk.org/introspection/core/1.0}'
C_NS = '{http://www.gtk.org/introspection/c/1.0}'
GLIB_NS = '{http://www.gtk.org/introspection/glib/1.0}'

TAG_CLASS = NS + 'class'
TAG_NAMESPACE = NS + 'namespace'
TAG_INCLUDE = NS + 'include'
TAG_CONSTRUCTOR = NS + 'constructor'
TAG_RETURN_VALUE = NS + 'return-value'
TAG_TYPE = NS + 'type'
TAG_ARRAY = NS + 'array'
TAG_PARAMETERS = NS + 'parameters'
TAG_VIRTUAL_METHOD = NS + 'virtual-method'
TAG_PARAMETER = NS + 'parameter'
TAG_PROPERTY = NS + 'property'
TAG_RECORD = NS + 'record'
TAG_FIELD = NS + 'field'
TAG_ENUMERATION = NS + 'enumeration'
TAG_MEMBER = NS + 'member'
TAG_DOC = NS + 'doc'
TAG_CALLBACK = NS + 'callback'
TAG_INSTANCE_PARAMETER = NS + 'instance-parameter'
TAG_METHOD = NS + 'method'
TAG_BITFIELD = NS + 'bitfield'
TAG_FUNCTION = NS + 'function'
TAG_SIGNAL = GLIB_NS + 'signal'
TAG_INTERFACE = NS + 'interface'
TAG_IMPLEMENTS = NS + 'implements'

ATTR_NAME = 'name'
ATTR_WHEN = 'when'
ATTR_VALUE = 'value'
ATTR_SCOPE = 'scope'
ATTR_LENGTH = 'length'
ATTR_PARENT = 'parent'
ATTR_CLOSURE = 'closure'
ATTR_DESTORY = 'destroy'
ATTR_READABLE = 'readable'
ATTR_WRITABLE = 'writable'
ATTR_ALLOW_NONE = 'allow-none'
ATTR_INTROSPECTABLE = 'introspectable'
ATTR_CONSTRUCT_ONLY = 'construct-only'
ATTR_SHARED_LIBRARY = 'shared-library'
ATTR_ZERO_TERMINATED = 'zero-terminated'
ATTR_TRANSFER_ONWERSHIP = 'transfer-ownership'

ATTR_C_IDENTIFIER_PREFIXES = C_NS + 'identifier-prefixes'
ATTR_C_IDENTIFIER = C_NS + 'identifier'
ATTR_C_SYMBOL_PREFIXES = C_NS + 'symbol-prefixes'
ATTR_C_SYMBOL_PREFIX = C_NS + 'symbol-prefix'
ATTR_C_TYPE = C_NS + 'type'

ATTR_GLIB_NICK = GLIB_NS + 'nick'
ATTR_GLIB_TYPE_NAME = GLIB_NS + 'type-name'
ATTR_GLIB_GET_TYPE = GLIB_NS + 'get-type'
ATTR_GLIB_TYPE_STRUCT = GLIB_NS + 'type-struct'


def printable(cls):
    cls.__repr__ = lambda self: str(self.__dict__)
    return cls


def partition(pred, iterable):
    t1, t2 = itertools.tee(iterable)
    return filter(pred, t1), filter(lambda x: not pred(x), t2)


def by_name(elements):
    return {e.name: e for e in elements}


def title_case(st):
    return ''.join(c for c in st.title() if c.isalpha())


def parse_doc(tag):
    text = tag.findtext(TAG_DOC)
    if text:
        text = text.replace('\n', ' ')
    return text


def camel_case(st):
    st = title_case(st)
    return st[0].lower() + st[1:]


def parse_tag_value(type_registry, tag, name=None):
    def lookup_type(tag):
        if tag.tag == TAG_ARRAY:
            inner_tag = tag.find(TAG_TYPE)
            gir_type = inner_tag.get(ATTR_NAME)
            c_type = inner_tag.get(ATTR_C_TYPE)
            return type_registry.lookup(gir_type, c_type, is_array=True)
        else:
            gir_type = tag.get(ATTR_NAME)
            c_type = tag.get(ATTR_C_TYPE)
            return type_registry.lookup(gir_type, c_type)

    transfer = tag.get(ATTR_TRANSFER_ONWERSHIP)
    type_tag = tag.find(TAG_TYPE)
    if type_tag is None:
        type_tag = tag.find(TAG_ARRAY)

    scope = tag.get(ATTR_SCOPE)
    allow_none = tag.get(ATTR_ALLOW_NONE) == '1'
    inner_type_tags = type_tag.findall(TAG_TYPE)

    if name is None:
        name = tag.get(ATTR_NAME)
    assert name

    typ = lookup_type(type_tag)
    value = None

    if typ.is_container:
        assert inner_type_tags
        types = enumerate(map(lookup_type, inner_type_tags))
        type_params = [c(name + '_' + str(i), transfer == 'full') for i, c in types]
        value = typ(name, transfer != 'none', allow_none, *type_params)
    else:
        assert transfer != 'container'
        if typ.is_array:
            c_array_type = type_tag.get(ATTR_C_TYPE)
            value = typ(name, transfer == 'full', allow_none, c_array_type)
        else:
            if scope is not None:
                value = typ(name, transfer == 'full', allow_none, scope)
            else:
                value = typ(name, transfer == 'full', allow_none)
    value.doc = parse_doc(tag)
    return value


@printable
class Parameters(object):
    def __init__(self, return_value, instance_param, params=None, java_params=None):
        params = params or []
        self.instance_param = instance_param
        if return_value is None:
            return_value = VoidType()
        self.return_value = return_value
        self.params = params
        if instance_param is not None:
            self.all_params = [instance_param] + params
        else:
            self.all_params = params

        def is_closure_param(param):
            return isinstance(param, JObjectWrapperType)

        self.closure_params, self.java_params = partition(is_closure_param, params)

        def is_length_param(param):
            return param.is_length_param

        self.length_params, self.java_params = partition(is_length_param, self.java_params)

        if java_params:
            self.java_params = java_params

        def set_parent(param):
            if param is not None:
                param.parent = self

        map(set_parent, [return_value, instance_param] + params)

    def __iter__(self):
        return iter(self.all_params)

    @classmethod
    def from_tag(cls, type_registry, tag):
        return_value = parse_tag_value(type_registry, tag.find(TAG_RETURN_VALUE), 'result')

        params_tag = tag.find(TAG_PARAMETERS)
        if params_tag is None:
            return cls(return_value, None)

        closure_refs = {}
        destroy_refs = {}
        array_refs = {}
        for tag_index, tag in enumerate(params_tag.findall(TAG_PARAMETER)):
            closure = tag.get(ATTR_CLOSURE)
            if closure is not None:
                closure_refs[int(closure)] = tag_index
            destroy = tag.get(ATTR_DESTORY)
            if destroy is not None:
                destroy_refs[int(destroy)] = tag_index
            array_tag = tag.find(TAG_ARRAY)
            if array_tag is not None:
                length = array_tag.get(ATTR_LENGTH)
                if length is not None:
                    array_refs[int(length)] = tag_index

        params = []
        instance_param = None
        real_tag_index = 0

        for tag in params_tag:
            if tag.tag == TAG_INSTANCE_PARAMETER:
                assert real_tag_index == 0
                instance_param = parse_tag_value(type_registry, tag)
            else:
                if closure_refs.get(real_tag_index) is not None:
                    name = tag.get(ATTR_NAME)
                    closure_index = closure_refs.get(real_tag_index)
                    closure = None
                    if closure_index == real_tag_index - 1:
                        closure = params[-1]
                    else:
                        assert closure_index == real_tag_index
                    params.append(JObjectWrapperType(name, closure, transfer_ownership=True))
                elif destroy_refs.get(real_tag_index) is not None:
                    name = tag.get(ATTR_NAME)
                    destroy_index = destroy_refs.get(real_tag_index)
                    assert destroy_index == real_tag_index - 2
                    params[-2].scope == 'notified'
                    params.append(JDestroyType(name))
                elif array_refs.get(real_tag_index) is not None:
                    array_index = array_refs.get(real_tag_index)
                    assert array_index == real_tag_index - 1
                    array = params[-1]
                    value = parse_tag_value(type_registry, tag)
                    value.is_length_param = True
                    value.array = array
                    array.length = value
                    params.append(value)
                else:
                    params.append(parse_tag_value(type_registry, tag))
                real_tag_index += 1

        return cls(return_value, instance_param, params)


@printable
class Property(object):
    def __init__(self, name, value, class_value, readable, writable, construct_only):
        self.name = name
        self.value = value
        self.readable = readable
        self.writable = writable
        self.construct_only = construct_only

        if readable:
            get_value = copy(value)
            get_value.transfer_ownership = not get_value.transfer_ownership
            self.getter = Method(
                c_name=None,
                name='get' + title_case(name),
                params=Parameters(get_value, class_value),
            )
            self.signal = Signal(
                name='on' + title_case(name) + 'Changed',
                params=Parameters(None, class_value, [
                    GParamSpecType('pspec', transfer_ownership=False),
                    JObjectWrapperType('listener', None, transfer_ownership=False),
                ], java_params=[value]),
                signal_name='notify::' + name,
                interface_name=title_case(name) + 'ChangeListener',
                class_value=class_value,
                when='first',
            )

        if writable:
            self.setter = Method(
                c_name=None,
                name='set' + title_case(name),
                params=Parameters(None, class_value, [value]),
            )

    @classmethod
    def from_tag(cls, type_registry, class_value, tag):
        name = tag.get(ATTR_NAME)

        return cls(
            name=name,
            value=parse_tag_value(type_registry, tag, camel_case(name)),
            class_value=class_value,
            readable=str(tag.get(ATTR_READABLE)) != '0',
            writable=str(tag.get(ATTR_WRITABLE)) == '1' and str(tag.get(ATTR_CONSTRUCT_ONLY)) != '1',
            construct_only=bool(tag.get(ATTR_CONSTRUCT_ONLY)),
        )


@printable
class BaseFunction(object):
    def __init__(self, name, params, c_name=None, doc=None):
        self.name = name
        self.c_name = c_name
        self.params = params
        self.doc = doc

    @property
    def method_signature(self):
        arg_signature = ''.join((p.java_signature for p in self.params.java_params if p.java_signature is not None))
        return '(' + arg_signature + ')' + self.params.return_value.java_signature

    @classmethod
    def from_tag(cls, type_registry, tag):
        return cls(
            doc=parse_doc(tag),
            name=camel_case(tag.get(ATTR_NAME)),
            c_name=tag.get(ATTR_C_IDENTIFIER),
            params=Parameters.from_tag(type_registry, tag),
        )


class Function(BaseFunction):
    pass


class Method(BaseFunction):
    pass


class Constructor(BaseFunction):
    def __init__(self, **kwargs):
        super(Constructor, self).__init__(**kwargs)
        p = self.params
        self.params = Parameters(GWeakRefType('instance_pointer'), p.instance_param, p.params)
        self.name = 'nativeConstructor'


class Callback(BaseFunction):
    def __init__(self, value, **kwargs):
        super(Callback, self).__init__(**kwargs)
        self.value = value

    @classmethod
    def from_tag(cls, type_registry, tag):
        callback_name = tag.get(ATTR_NAME)
        callback_value = type_registry.lookup(callback_name, None)('listener', False)
        return cls(
            doc=parse_doc(tag),
            name='on' + callback_name,
            value=callback_value,
            params=Parameters.from_tag(type_registry, tag),
        )


class Signal(BaseFunction):
    def __init__(self, signal_name, interface_name, class_value, when, **kwargs):
        BaseFunction.__init__(self, **kwargs)
        self.signal_name = signal_name
        self.when = when

        listener_value = ClassCallbackMetaType(
            java_type=interface_name,
            outer=class_value,
        )('listener')
        handle_value = IntType('handle', transfer_ownership=False)
        closure_value = JObjectWrapperType('user_data', listener_value, transfer_ownership=False)
        self.add_listener = Method(
            c_name=None,
            name='connect' + listener_value.java_type,
            params=Parameters(handle_value, class_value, [listener_value, closure_value]),
        )
        self.remove_listener = Method(
            c_name=None,
            name='disconnect' + listener_value.java_type,
            params=Parameters(None, class_value, [handle_value]),
        )
        self.public_add_listener = Method(
            c_name=None,
            name='add' + listener_value.java_type,
            params=Parameters(None, None, [listener_value]),
        )
        self.public_remove_listener = Method(
            c_name=None,
            name='remove' + listener_value.java_type,
            params=Parameters(None, None, [listener_value]),
        )
        self.value = listener_value

    @classmethod
    def from_tag(cls, type_registry, class_value, tag):
        signal_name = tag.get(ATTR_NAME)

        parsed_params = Parameters.from_tag(type_registry, tag)
        return_value = parsed_params.return_value
        params = parsed_params.all_params if parsed_params is not None else []
        params = [return_value, class_value] + [params + [JObjectWrapperType('listener', None, transfer_ownership=False)]]

        return cls(
            name=camel_case(signal_name),
            signal_name=signal_name,
            interface_name=title_case(signal_name) + 'Listener',
            class_value=class_value,
            when=tag.get(ATTR_WHEN),
            params=Parameters(*params),
        )


@printable
class Class(object):
    def __init__(self, **kwargs):
        self.__dict__.update(**kwargs)

    @classmethod
    def from_tag(cls, type_registry, tag, interfaces=None):
        parent = tag.get(ATTR_PARENT)
        if parent == 'GObject.Object':
            parent = None

        name = tag.get(ATTR_NAME)
        value = type_registry.lookup(name, None)('self')

        return cls(
            name=name,
            parent=parent,
            c_type=tag.get(ATTR_C_TYPE),
            value=value,
            c_symbol_prefix=tag.get(ATTR_C_SYMBOL_PREFIX),
            glib_type_name=tag.get(ATTR_GLIB_TYPE_NAME),
            glib_get_type=tag.get(ATTR_GLIB_GET_TYPE),
            glib_type_struct=tag.get(ATTR_GLIB_TYPE_STRUCT),
            constructors=[Constructor.from_tag(type_registry, t) for t in tag.findall(TAG_CONSTRUCTOR) if t.get(ATTR_INTROSPECTABLE) != '0'],
            properties=[Property.from_tag(type_registry, value, t) for t in tag.findall(TAG_PROPERTY) if t.get(ATTR_INTROSPECTABLE) != '0'],
            methods=[Method.from_tag(type_registry, t) for t in tag.findall(TAG_METHOD) if t.get(ATTR_INTROSPECTABLE) != '0'],
            functions=[Function.from_tag(type_registry, t) for t in tag.findall(TAG_FUNCTION) if t.get(ATTR_INTROSPECTABLE) != '0'],
            signals=[Signal.from_tag(type_registry, value, t) for t in tag.findall(TAG_SIGNAL) if t.get(ATTR_INTROSPECTABLE) != '0'],
            interfaces=[interfaces[t.get(ATTR_NAME)] for t in tag.findall(TAG_IMPLEMENTS)],
        )


@printable
class EnumMember(object):
    def __init__(self, value, name, c_name, nick=None, description=None):
        self.value = value
        self.name = name
        self.c_name = c_name
        self.nick = nick
        self.description = description

    @classmethod
    def from_tag(cls, tag, glib_tag=None):
        value = tag.get(ATTR_VALUE)
        if glib_tag is not None:
            assert value == glib_tag.get(ATTR_VALUE)
            return cls(
                value=value,
                name=tag.get(ATTR_NAME).upper(),
                c_name=tag.get(ATTR_C_IDENTIFIER),
                nick=glib_tag.get(ATTR_GLIB_NICK),
                description=glib_tag.get(ATTR_C_IDENTIFIER),
            )
        else:
            return cls(
                value=value,
                name=tag.get(ATTR_NAME).upper(),
                c_name=tag.get(ATTR_C_IDENTIFIER),
            )


@printable
class Enum(object):
    def __init__(self, name, c_name, type, is_bitfield, members, has_nick=False, has_description=False):
        self.name = name
        self.c_name = c_name
        self.type = type
        self.is_bitfield = is_bitfield
        self.members = members
        self.has_nick = has_nick
        self.has_description = has_description

    @classmethod
    def from_tag(cls, type_registry, tag, glib_tag=None):
        members = tag.findall(TAG_MEMBER)
        name = tag.get(ATTR_NAME)
        c_name = tag.get(ATTR_C_TYPE)
        type = type_registry.lookup(name, c_name);
        if glib_tag is not None:
            glib_members = glib_tag.findall(TAG_MEMBER)
            return cls(
                name=name,
                c_name=c_name,
                type=type,
                is_bitfield=tag.tag == TAG_BITFIELD,
                members=[EnumMember.from_tag(*tags) for tags in zip(members, glib_members)],
                has_nick=True,
                has_description=True,
            )
        else:
            return cls(
                name= name,
                c_name= c_name,
                type=type,
                is_bitfield=tag.tag == TAG_BITFIELD,
                members=[EnumMember.from_tag(tag) for tag in members],
            )


@printable
class Namespace(object):
    def __init__(self, type_registry, tag):
        def find_enum_pairs():
            enum_tags = tag.findall(TAG_ENUMERATION) + tag.findall(TAG_BITFIELD);
            c_enums, glib_enums = partition(lambda top: top.get(ATTR_GLIB_TYPE_NAME) is None, enum_tags)
            glib_enum_dict = {enum.get(ATTR_NAME): enum for enum in glib_enums}

            def glib_from_c(c_enum):
                glib_enum = glib_enum_dict.get(c_enum.get(ATTR_NAME) + 's')
                if glib_enum is not None:
                    return [c_enum, glib_enum]
                else:
                    return [c_enum]

            return map(glib_from_c, c_enums)

        interfaces = [Class.from_tag(type_registry, t) for t in tag.findall(TAG_INTERFACE)]
        interface_map = {interface.name: interface for interface in interfaces}

        self.name = tag.get(ATTR_NAME)
        self.symbol_prefix = tag.get(ATTR_C_SYMBOL_PREFIXES)
        self.identifier_prefix = tag.get(ATTR_C_IDENTIFIER_PREFIXES)
        self.shared_library = tag.get(ATTR_SHARED_LIBRARY)
        self.interfaces = interfaces
        self.enums = [Enum.from_tag(type_registry, *tags) for tags in find_enum_pairs()]
        self.callbacks = [Callback.from_tag(type_registry, t) for t in tag.findall(TAG_CALLBACK)]
        self.classes = [Class.from_tag(type_registry, t, interface_map) for t in tag.findall(TAG_CLASS)]
        self.functions = [Function.from_tag(type_registry, t) for t in tag.findall(TAG_FUNCTION)]


class GirParser(object):
    def __init__(self, xml_root):
        self.xml_root = xml_root

    def parse_types(self):
        types = []

        for namespace in self.xml_root.findall(TAG_NAMESPACE):
            prefix = namespace.get(ATTR_C_SYMBOL_PREFIXES)
            tag_types = {
                TAG_CLASS: GObjectMetaType,
                TAG_INTERFACE: GObjectMetaType,
                TAG_CALLBACK: CallbackMetaType,
                TAG_ENUMERATION: EnumMetaType,
                TAG_BITFIELD: BitfieldMetaType,
            }
            tags = sum(map(namespace.findall, tag_types.keys()), [])
            for tag in tags:
                gir_type = tag.get(ATTR_NAME)
                c_type = tag.get(ATTR_C_TYPE)
                MetaType = tag_types[tag.tag]

                if MetaType == EnumMetaType or MetaType == BitfieldMetaType:
                    if tag.get(ATTR_GLIB_TYPE_NAME) is not None:
                        continue

                types.append(MetaType(
                    gir_type=gir_type,
                    c_type=c_type,
                    prefix=prefix,
                ))

        return types

    def parse_enum_aliases(self):
        aliases = {}
        for namespace in self.xml_root.findall(TAG_NAMESPACE):
            enum_tags = namespace.findall(TAG_ENUMERATION) + namespace.findall(TAG_BITFIELD)
            for tag in enum_tags:
                if tag.get(ATTR_GLIB_TYPE_NAME) is not None:
                    alias = tag.get(ATTR_NAME)
                    name = alias[:-1]
                    aliases[alias] = name
        return aliases

    def parse_full(self, type_registry):
        return [Namespace(type_registry, tag) for tag in self.xml_root.findall(TAG_NAMESPACE)]

