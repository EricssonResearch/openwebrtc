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


import collections
import config
from functools import partial
from base_generator import *

J = BaseGenerator(
    default_line_prefix=config.JAVA_INDENTATION,
)


def java_param(param):
    if hasattr(param, 'java_full_class'):
        return param.java_full_class + ' ' + param.name
    if param.java_type:
        return param.java_type + ' ' + param.name
    return ()


def java_arg(param):
    if param.java_type:
        return param.name
    return ()


@add_to(J)
class JavaDoc(J.Block):
    _line_prefix = ' * '
    def __init__(self, text, params, ret):
        self.text = text
        self.params = params
        self.ret = ret

    @property
    def start(self):
        return ('/**' if self.text or self.params or self.ret else [])

    @property
    def end(self):
        return (' */' if self.text or self.params or self.ret else [])

    @property
    def body(self):
        return [self.text if self.text else []] + [
            '@param %s %s' % kv for kv in self.params.items()
        ] + ['@return ' + self.ret if self.ret else []]


@add_to(J)
class Class(J.Block):
    def __init__(self,
            name,
            variation='class',
            visibility='public',
            static=False,
            abstract=False,
            extends=None,
            implements=None,
            imports=None,
            package=None,
            **kwargs):
        super(Class, self).__init__(**kwargs)

        self.name = name
        self.variation = variation
        self.visibility = visibility
        self.static = static
        self.abstract = abstract
        self.extends = extends or []
        self.implements = implements or []
        self.imports = imports or []
        self.package = package

    @property
    def start(self):
        lst = []
        if self.visibility != 'default':
            lst.append(self.visibility)
        if self.static:
            lst.append('static')
        if self.abstract:
            lst.append('abstract')
        lst.append(self.variation)
        lst.append(self.name)
        if self.extends:
            lst.append('extends ' + flatjoin(self.extends, ', '))
        if self.implements:
            lst.append('implements ' + flatjoin(self.implements, ', '))
        lst.append('{')
        package_decl = 'package ' + self.package + ';' if self.package else None
        imports = ['import ' + i + ';' for i in self.imports]
        return intersperse(prune_empty([package_decl, imports, ' '.join(lst)]), '')

    @staticmethod
    def create_callback(callback, **kwargs):
        args = {
            'name': callback.value.java_type,
            'static': True,
            'body': [J.Method.default(callback, native=False)],
            'variation': 'interface',
        }
        args.update(kwargs)
        return Class(**args)


@add_to(J)
class Method(J.FunctionBlock):
    def __init__(self,
            visibility='public',
            return_type='void',
            name='',
            params=None,
            static=False,
            abstract=False,
            native=False,
            synchronized=False,
            doc=None,
            **kwargs):
        super(Method, self).__init__(**kwargs)

        self.name = name
        self.return_type = return_type
        self.params = params or []

        self.visibility = visibility
        self.static = static
        self.synchronized = synchronized
        self.abstract = abstract
        self.native = native
        self.doc = doc

    @property
    def modifiers(self):
        lst = []
        if self.visibility != 'default':
            lst.append(self.visibility)
        if self.static:
            lst.append('static')
        if self.synchronized:
            lst.append('synchronized')
        if self.abstract:
            lst.append('abstract')
        if self.native:
            lst.append('native')
        return lst

    @property
    def start(self):
        row = self.definition + (' {' if len(self.body) else ';')
        if self.doc:
            return [self.doc, row]
        else:
            return row

    @property
    def end(self):
        return ('}' if len(self.body) else [])

    @staticmethod
    def default(method, **kwargs):
        return_type = method.params.return_value.java_type
        if hasattr(method.params.return_value, 'java_full_class'):
            return_type = method.params.return_value.java_full_class
        args = {
            'visibility': 'public',
            'return_type': return_type,
            'name': method.name,
            'params': map(java_param, method.params.java_params),
            'native': True,
            'doc': JavaDoc(method.doc,
                {p.name: getattr(p, 'doc', None) for p in method.params.java_params if getattr(p, 'doc', None) is not None},
                getattr(method.params.return_value, 'doc', None),
            ),
        }
        args.update(kwargs)
        return Method(**args)


@add_to(J)
def gen_signal(signal):
    mapName = 'handleMap' + signal.value.java_type
    mapType = 'java.util.HashMap<{signal_type}, {handle_type}>'.format(
        signal_type=signal.value.java_type,
        handle_type=signal.add_listener.params.return_value.object_type,
    )
    ensure_map_and_remove = [
        J.If(mapName + ' == null', mapName + ' = new ' + mapType + '();'),
        '',
        'Integer current = ' + mapName + '.remove(listener);',
        J.If('current != null', J.Call(signal.remove_listener.name, 'current')),
    ]
    callback = J.Class.create_callback(signal)
    return [
        'private %s %s;' % (mapType, mapName),
        callback,
        Method.default(signal.add_listener, visibility='private'),
        Method.default(signal.remove_listener, visibility='private'),
        Method.default(signal.public_add_listener,
            native=False,
            synchronized=True,
            body=ensure_map_and_remove + [
                '',
                'int handle = ' + signal.add_listener.name + '(listener);',
                mapName + '.put(listener, handle);',
            ],
        ),
        Method.default(signal.public_remove_listener,
            native=False,
            synchronized=True,
            body=ensure_map_and_remove,
        ),
    ]


@add_to(J)
def gen_class(clazz, interfaces):
    # public constructors
    body = [(
        Method(
            visibility='public',
            return_type=[],
            name=clazz.name,
            params=map(java_param, constructor.params),
            body=[
                J.Call('super', J.Call('_newNativePointer', '0')),
                J.Assign('long pointer', J.Call(constructor.name, *map(java_arg, constructor.params))),
                J.Call('_setInternalPointer', 'pointer'),
            ]
        ),
        Method(
            visibility='default',
            return_type='long',
            native=True,
            name=constructor.name,
            params=map(java_param, constructor.params)
        ),
    ) for constructor in clazz.constructors]

    # private constructor
    body += [Method(
        visibility='default',
        return_type=[],
        name=clazz.name,
        params=['NativePointer nativePointer'],
        body=[J.Call('super', 'nativePointer')],
    )]

    # methods
    body += map(Method.default, clazz.methods)
    body += map(partial(Method.default, static=True), clazz.functions)

    # interface methods
    body += [['@Override', Method.default(method)] for method in flatten(interface.methods for interface in clazz.interfaces)];

    # properties
    body += sum(sum([[
        [Method.default(prop.setter)] if prop.writable else [],
        [Method.default(prop.getter)] if prop.readable else [],
        gen_signal(prop.signal) if prop.readable else [],
    ] for prop in clazz.properties], []), [])

    #signals
    body += sum(map(gen_signal, clazz.signals), [])

    return J.Class(clazz.name,
        extends=clazz.parent or 'NativeInstance',
        implements=[interface.name for interface in clazz.interfaces],
        imports=[
            config.PACKAGE_ROOT + '.NativeInstance',
            config.PACKAGE_ROOT + '.NativePointer',
        ],
        body=intersperse(prune_empty(body), ''),
    )


@add_to(J)
def gen_interface(interface):
    body = [Method.default(method, native=False) for method in interface.methods]

    return J.Class(interface.name,
        extends=interface.parent,
        variation='interface',
        body=intersperse(prune_empty(body), ''),
    )


@add_to(J)
def gen_enum(enum):
    format_func = ('{0.name}({0.value}, "{0.nick}")' if enum.has_nick else '{0.name}({0.value})').format
    members = [format_func(member) for member in enum.members]
    members = intersperse(members, ',') + [';']
    members = [''.join(chunk) for chunk in chunks(members, 2)]


    body = [members, [
            'private final int mValue;',
            'private final String mNick;' if enum.has_nick else ()
        ],
        Method(
            visibility='private',
            name=enum.name,
            return_type=[],
            params=['int value', enum.has_nick and 'String nick'],
            body=['mValue = value;', enum.has_nick and 'mNick = nick;'],
        ),
        Method('public', 'int', 'getValue',
            body=['return mValue;'],
        ),
        enum.has_nick and Method('public', 'String', 'getNick',
            body=['return mNick;'],
        ),
        enum.has_nick and Method(
            static=True,
            name='valueOfNick',
            params=['String nick'],
            return_type=enum.name,
            body=[J.IfElse(
                ifs=['"%s".equals(nick)' % member.nick for member in enum.members],
                bodies=['return %s;' % member.name for member in enum.members] +
                    ['throw new IllegalArgumentException("Invalid enum nick: " + nick);'],
            )]
        )
    ]

    return J.Class(enum.name, variation='enum',
        imports=[config.PACKAGE_ROOT + '.ValueEnum'],
        implements=['ValueEnum'],
        body=intersperse(prune_empty(body), ''),
    )


@add_to(J)
def gen_namespace(namespace):
    classes = map(gen_class, namespace.classes, namespace.interfaces)
    interfaces = map(gen_interface, namespace.interfaces)
    enums = map(gen_enum, namespace.enums)
    callbacks = map(partial(J.Class.create_callback, static=False), namespace.callbacks)

    main_class = J.Class(
        name=namespace.name,
        body=[
            J.Block(
                _start='static {',
                body=['System.loadLibrary("%s");' % namespace.shared_library[3:-3]],
            ),
            '',
            Method('private', [], namespace.name, body=['']),
            '',
        ] + intersperse(map(partial(Method.default, static=True), namespace.functions), '')
    )

    all_classes = classes + interfaces + enums + callbacks + [main_class]

    for clazz in all_classes:
        clazz.package = config.PACKAGE_ROOT + '.' + namespace.symbol_prefix

    return {c.name: str(c) for c in all_classes}

external_classes = {
    'GstContext': str(J.Class(
        name='GstContext',
        extends='NativeInstance',
        visibility='public',
        package=config.PACKAGE_ROOT,
        body=[
            J.Method('public', [], 'GstContext', params=['long pointer'],
                     body=[J.Call('super', 'new NativePointer(pointer)')],
                     ),
        ]
    )),
}

standard_classes = {
    'NativeInstance': str(J.Class(
        name='NativeInstance',
        visibility='public',
        package=config.PACKAGE_ROOT,
        abstract=True,
        body=[
            J.Decl('long', 'nativeInstance'),
            '',
            J.Method('protected', [], 'NativeInstance', params=['NativePointer nativePointer'],
                body=[J.Assign('this.nativeInstance', 'nativePointer.pointer')],
            ),
            '',
            J.Method('protected', 'void', '_setInternalPointer', params=['long pointer'],
                body=[J.Assign('nativeInstance', 'pointer')]
            ),
            '',
            J.Method('protected', 'NativePointer', '_newNativePointer', params=['long pointer'],
                body=[J.Return(J.Call('new NativePointer', 'pointer'))],
                static=True,
            ),
            '',
            '@Override',
            J.Method('protected', 'void', 'finalize',
                body=[J.Call('nativeDestructor', 'this.nativeInstance')],
            ),
            '',
            J.Method('private', 'void', 'nativeDestructor', params=['long instancePointer'], native=True),
        ],
    )),
    'NativePointer': str(J.Class(
        name='NativePointer',
        visibility='public',
        package=config.PACKAGE_ROOT,
        body=[
            'final long pointer;',
            '',
            J.Method('default', [], 'NativePointer', params=['long pointer'],
                body=[J.Assign('this.pointer', 'pointer')],
            ),
        ],
    )),
    'ValueEnum': str(J.Class(
        name='ValueEnum',
        visibility='public',
        package=config.PACKAGE_ROOT,
        variation='interface',
        body=[J.Method('public', 'int', 'getValue')],
    )),
}
