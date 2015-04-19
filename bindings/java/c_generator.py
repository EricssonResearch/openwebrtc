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


import config
from functools import partial
from collections import defaultdict
from itertools import imap
from java_type_signatures import type_signatures
from base_generator import *

C = BaseGenerator(
    default_line_prefix=config.C_INDENTATION,
)

def jni_param(param):
    if param.jni_type:
        return param.jni_type + ' ' + param.jni_name
    return ()

def c_param(param):
    if param.c_type:
        return param.c_type + ' ' + param.c_name
    return ()

def c_arg(param):
    if param.c_type:
        return param.c_name
    return ()

def jni_arg(param):
    if param.jni_type:
        return param.jni_name
    return ()


@add_to(C)
class Log(C.Lines):
    def __init__(self, level, msg, *args):
        self.msg = msg
        self.args = args
        self.level = level

    def _make_logfunc(level):
        @classmethod
        def logfunc(cls, msg, *args):
            return cls(level, msg, *args)
        return logfunc

    error = _make_logfunc('error')
    warning = _make_logfunc('warning')
    debug = _make_logfunc('debug')
    info = _make_logfunc('info')
    verbose = _make_logfunc('verbose')

    def __iter__(self):
        yield C.Call('log_' + self.level, quot(self.msg), *self.args)


@add_to(C)
class Assert(C.Lines):
    def __init__(self, val):
        self.val = val

    def __iter__(self):
        yield semi('g_assert(' + flatjoin(self.val, '') + ')')


@add_to(C)
class Throw(C.Lines):
    def __init__(self, *args):
        self.args = args

    def __iter__(self):
        yield 'THROW(' + flatjoin(self.args, '') + ');'


@add_to(C)
class ExceptionCheck(C.Lines):
    def __init__(self, value):
        self.value = value

    def __iter__(self):
        yield C.If(C.Env('ExceptionCheck'),
            C.Log('warning', 'exception at %s:%d', '__FILE__', '__LINE__'),
            C.Env('ExceptionDescribe'),
            C.Return(self.value),
        )

    @classmethod
    def default(cls, value):
        return cls(value.parent.return_value.default_value)


@add_to(C)
class CommentHeader(C.Comment):
    def __iter__(self):
        l = len(self.text)
        yield '/**' + l * '*' + '**/'
        yield '/* ' + self.text + ' */'
        yield '/**' + l * '*' + '**/'


@add_to(C)
class Function(C.FunctionBlock):
    modifiers = ['static']

    def __init__(self,
            name,
            return_type='void',
            params=None,
            **kwargs):
        super(Function, self).__init__(**kwargs)
        self.name = name
        self.return_type = return_type
        self.params = params or []

    @property
    def start(self):
        return [self.definition, '{']

    @staticmethod
    def callback(callback, body=None, **kwargs):
        args = {
            'return_type': callback.params.return_value.c_type,
            'name': 'callback_' + callback.value.gir_type,
            'params': map(c_param, callback.params),
            'body': [TypeConversions.params_to_jni(callback.params, body=body or [], push_frame=True)],
        }
        if callback.params.return_value.name is not None:
            args['body'] += [C.Return(callback.params.return_value.c_name)]
        args.update(kwargs)
        return C.Function(**args)


@add_to(C)
class JniExport(C.FunctionBlock):
    modifiers = ['JNIEXPORT']

    def __init__(self,
            package=None,
            clazz=None,
            subclass=None,
            method_name=None,
            return_type='void',
            params=None,
            **kwargs):
        super(JniExport, self).__init__(**kwargs)
        self.package = package
        self.clazz = clazz
        self.subclass = subclass
        self.method_name = method_name
        self.return_type = return_type
        self.java_params = params or []

    @property
    def name(self):
        return '_'.join(prune_empty('Java',
            self.package.replace('.', '_'),
            self.clazz,
            self.subclass,
            self.method_name,
        ))

    @property
    def params(self):
        return ['JNIEnv* env'] + self.java_params

    @property
    def start(self):
        return [self.definition, '{']

    @staticmethod
    def default(function, body=[], **kwargs):
        params = map(jni_param, function.params.java_params)
        if function.params.instance_param is None:
            params = ['jclass jclazz'] + params
        else:
            params = [jni_param(function.params.instance_param)] + params
        args = {
            'return_type': function.params.return_value.jni_type,
            'method_name': function.name,
            'params': params,
            'body': [C.TypeConversions.params_to_c(function.params, body=body, get_env=False)],
        }
        if function.params.return_value.name is not None:
            args['body'] += [C.Return(function.params.return_value.jni_name)]
        args.update(kwargs)
        return JniExport(**args)


@add_to(C)
class Helper(C.Call):
    helper_functions = {}
    used_helpers = []

    def __init__(self, name, *args):
        super(Helper, self).__init__(name, *args)
        func = self.helper_functions.pop(name, None)
        if func is not None:
            self.used_helpers.append(func)

    @classmethod
    def add_helper(cls, name, func):
        cls.helper_functions[name] = func

    @classmethod
    def enumerate_used_helpers(cls):
        return cls.used_helpers


@add_to(C)
class Cache(C.Lines):
    cached_classes = defaultdict(partial(defaultdict, dict))

    def __init__(self, *args):
        self.args = list(args)

    def __iter__(self):
        yield 'cache_' + flatjoin(self.args, '_')

    @classmethod
    def clazz(cls, *args):
        classname = flatjoin(args, '$')
        cls.cached_classes[type_signatures[classname]['_path']]
        return cls(*args)

    def _make_cacher(func):
        @classmethod
        def cacher(cls, *args):
            methodname = args[-1]
            signatures = type_signatures[flatjoin(args[:-1], '$')]
            cls.cached_classes[signatures['_path']][func][methodname] = signatures[methodname]
            return cls(*args)
        return cacher

    method = _make_cacher('GetMethodID')
    static_method = _make_cacher('GetStaticMethodID')
    field = _make_cacher('GetFieldID')
    static_field = _make_cacher('GetStaticFieldID')

    @classmethod
    def default_class(cls, clazz):
        cls.cached_classes[clazz.java_class_path]
        return cls(clazz.java_type)

    @classmethod
    def default_method(cls, func):
        val = func.value
        args = None
        if hasattr(val, 'outer_java_type'):
            args = [val.outer_java_type, val.java_type, func.name]
        else:
            args = [val.java_type, func.name]
        cls.cached_classes[val.java_class_path]['GetMethodID'][func.name] = func.method_signature
        return cls(*args)

    @classmethod
    def default_enum_member(cls, enum, member):
        typ = enum.type
        if hasattr(enum.type, 'inner_type'):
            typ = enum.type.inner_type
        cls.cached_classes[typ.java_class_path]['GetStaticFieldID'][member.name] = typ.java_signature
        return cls(enum.name, member.name)

    @classmethod
    def enumerate_cached_classes(cls):
        cache_declarations = []
        jni_onload_cache = []

        for classpath, clazz in Cache.cached_classes.items():
            classname = classpath[classpath.rfind('/')+1:]
            to_cache_var = lambda *args: '_'.join(['cache'] + classname.split('$') + list(args))

            classvar = to_cache_var()
            cache_declarations += [C.Decl('static jclass', classvar)]
            jni_onload_cache += [
                C.Assign(classvar, C.Env('FindClass', quot(classpath))),
                C.ExceptionCheck('0'),
                C.Assign(classvar, C.Env('NewGlobalRef', classvar)),
                C.ExceptionCheck('0'),
            ]

            for getfunc, method in clazz.items():
                var_type = 'jmethodID' if 'Method' in getfunc else 'jfieldID'
                for methodname, signature in method.items():
                    methodvar = to_cache_var(methodname)
                    if methodname == '_constructor':
                        methodname = '<init>'
                    cache_declarations += [C.Decl('static ' + var_type, methodvar)]
                    jni_onload_cache += [
                        C.Log('debug', 'getting %s.%s', quot(classname), quot(methodname)),
                        C.Assign(methodvar, C.Env(getfunc, classvar, quot(methodname), quot(signature))),
                        C.ExceptionCheck('0'),
                    ]
            cache_declarations.append('')
            jni_onload_cache.append('')
        return cache_declarations[:-1], jni_onload_cache[:-1]


@add_to(C)
class Env(C.Lines):
    return_type_table = {
        'V': 'Void',
        ';': 'Object',
        'Z': 'Boolean',
        'B': 'Byte',
        'C': 'Char',
        'S': 'Short',
        'I': 'Int',
        'J': 'Long',
        'F': 'Float',
        'D': 'Double',
    }

    def __init__(self, name, *args):
        self.name = name
        self.args = args

    @staticmethod
    def tuple_to_type(args):
        clazz = type_signatures[flatjoin(args[:-1], '$')]
        method = clazz[args[-1]]
        return Env.return_type_table[method[-1]]

    @classmethod
    def method(cls, name, method_tuple, *args):
        return cls('Call' + Env.tuple_to_type(method_tuple) + 'Method', name, C.Cache.method(*method_tuple), *args)

    @classmethod
    def static_method(cls, method_tuple, *args):
        return cls('CallStatic' + Env.tuple_to_type(method_tuple) + 'Method', C.Cache.clazz(method_tuple[:-1]), C.Cache.static_method(*method_tuple), *args)

    @classmethod
    def field(cls, name, field_tuple):
        return cls('Get' + Env.tuple_to_type(field_tuple) + 'Field', name, C.Cache.field(*field_tuple))

    @classmethod
    def new(cls, clazz, *args):
        return cls('NewObject', C.Cache.clazz(clazz), C.Cache.method(clazz, '_constructor'), *args)

    @classmethod
    def throw(cls, clazz, msg):
        return cls('ThrowNew', C.Cache.clazz(clazz), msg)

    @classmethod
    def callback(cls, callback):
        type = Env.return_type_table[callback.params.return_value.java_signature[-1]]
        cached = None
        if hasattr(callback.value, 'outer_java_type'):
            cached = (callback.value.outer_java_type, callback.value.java_type, callback.name)
        else:
            cached = (callback.value.java_type, callback.name)
        return cls('Call' + type + 'Method',
            map(jni_arg, callback.params.closure_params),
            C.Cache.default_method(callback),
            *map(jni_arg, callback.params.java_params)
        )

    def __iter__(self):
        yield semi('(*env)->{name}({args})'.format(
            name=self.name,
            args=flatjoin(['env'] + list(flatten(self.args)), ', '),
        ))


@add_to(C)
class TypeConversions(C.Lines):
    def __init__(self, conversions, return_conversion, body=None, get_env=True, push_frame=False, **kwargs):
        super(TypeConversions, self).__init__(**kwargs)
        self.conversions = list(conversions)
        self.return_conversion = return_conversion
        self.body = body or []
        self.get_env = get_env
        self.push_frame = push_frame

    def __iter__(self):
        conversion = [
            prune_empty([p.declarations for p in self.conversions] + [self.get_env and C.Decl('JNIEnv*', 'env')]),
            self.get_env and C.Assign('env', C.Call('get_jni_env')),
            C.If(Env('PushLocalFrame', str(config.LOCAL_FRAME_SIZE)),
                C.Log('warning', 'failed to push local frame at %s:%d', '__FILE__', '__LINE__')
            ) if self.push_frame else [],
            prune_empty([p.conversion for p in self.conversions]),
            self.body,
            prune_empty(p.cleanup for p in reversed(self.conversions)),
            Env('PopLocalFrame', 'NULL') if self.push_frame else [],
        ]
        if self.return_conversion is not None:
            conversion = [self.return_conversion.declarations] + conversion + [
                self.return_conversion.conversion, self.return_conversion.cleanup,
            ]

        return iter(intersperse(prune_empty(conversion), ''))

    @staticmethod
    def params_to_c(params, **kwargs):
        ret = params.return_value
        return TypeConversions([param.transform_to_c() for param in params],
            ret.transform_to_jni() if ret.name is not None else None, **kwargs)

    @staticmethod
    def params_to_jni(params, **kwargs):
        ret = params.return_value
        return TypeConversions([param.transform_to_jni() for param in params],
            ret.transform_to_c() if ret.name is not None else None, **kwargs)




def make_function_gen(package, classname):
    def gen(function):
        call = C.Call(function.c_name, map(c_arg, function.params))
        ret = function.params.return_value
        if ret.name is not None:
            call = C.Assign(ret.c_name, call)
        out = JniExport.default(function, package=package, clazz=classname, body=call)
        if ret.name is not None:
            out.body = [C.Decl(ret.c_type, ret.c_name)] + out.body
        return out
    return gen

def make_callback_gen(package, classname):
    def gen(callback):
        call = C.Env.callback(callback)
        ret = callback.params.return_value
        if ret.name is not None:
            call = C.Assign(ret.jni_name, call)
        out = C.Function.callback(callback, package=package, clazz=classname, body=call)
        if ret.name is not None:
            out.body = [C.Decl(ret.jni_type, ret.jni_name)] + out.body
        return out
    return gen

def make_signal_accessors_gen(package, classname):
    def gen(signal):
        connect_args = map(c_arg, signal.add_listener.params)
        connect_args[0] = 'G_OBJECT(' + connect_args[0] + ')'
        connect_args.insert(1, quot(signal.signal_name))
        connect_args += [C.Helper('jobject_wrapper_closure_notify').name, '0']
        ret = signal.add_listener.params.return_value
        connecter = C.JniExport.default(signal.add_listener, package=package, clazz=classname,
            body=[C.Assign(ret.c_name, C.Call('g_signal_connect_data', connect_args))],
        )
        connecter.body = [C.Decl(ret.c_type, ret.c_name)] + connecter.body
        disconnect_args = map(c_arg, signal.remove_listener.params)
        disconnect_args[0] = 'G_OBJECT(' + disconnect_args[0] + ')'
        disconnecter = C.JniExport.default(signal.remove_listener, package=package, clazz=classname,
            body=C.Call('g_signal_handler_disconnect', disconnect_args),
        )
        return [connecter, disconnecter]
    return gen

def gen_class(package, clazz):
    body = [C.CommentHeader(clazz.name)]
    gen_signal_accessors = make_signal_accessors_gen(package, clazz.name)

    for attr in ['constructors', 'functions', 'methods']:
        body += [C.Comment(attr) if getattr(clazz, attr) else None]
        body += map(make_function_gen(package, clazz.name), getattr(clazz, attr))

    for interface in clazz.interfaces:
        body += map(make_function_gen(package, clazz.name), interface.methods)

    body += [C.Comment('signals') if clazz.signals else None]
    body += map(make_callback_gen(package, clazz.name), clazz.signals)
    body += map(gen_signal_accessors, clazz.signals)

    body += [C.Comment('properties') if clazz.properties else None]
    for prop in clazz.properties:
        body += [C.Comment(prop.name)]
        if prop.readable:
            # getter
            ret = prop.getter.params.return_value
            get_params = map(c_arg, prop.getter.params) + [quot(prop.name), '&' + ret.c_name, 'NULL']
            func = C.JniExport.default(prop.getter, package=package, clazz=clazz.name, body=[
                C.Call('g_object_get', get_params),
            ])
            if ret.name is not None:
                func.body = [C.Decl(ret.c_type, ret.c_name)] + func.body
            body.append(func)

            # change listener
            transform = ret.transform_to_jni()

            func = C.Function(
                package=package,
                clazz=clazz.name,
                name='callback_' + prop.signal.value.gir_type,
                return_type=prop.signal.params.return_value.c_type,
                params=map(c_param, prop.signal.params),
                body=[TypeConversions([p.transform_to_jni() for p in prop.signal.params.params], None, push_frame=True, body=[
                    '(void) c_pspec;',
                    C.Call('g_object_get', get_params),
                    transform.conversion,
                    C.Env.callback(prop.signal),
                    transform.cleanup,
                ])],
            )
            func.body = [
                C.Decl(ret.c_type, ret.c_name),
                transform.declarations,
            ] + func.body
            body.append(func)
            body += gen_signal_accessors(prop.signal)

        if prop.writable:
            # setter
            ret = prop.setter.params.return_value
            params = map(c_arg, prop.setter.params)
            params.insert(1, quot(prop.name))
            params.append('NULL')
            func = C.JniExport.default(prop.setter, package=package, clazz=clazz.name, body=[
                C.Call('g_object_set', params)
            ])
            body += [func]

    return intersperse(prune_empty(body), '')


def gen_namespace(namespace, package):
    body = []
    package = package + '.' + namespace.symbol_prefix

    body += map(make_callback_gen(package, namespace.identifier_prefix), namespace.callbacks)
    body += map(make_function_gen(package, namespace.identifier_prefix), namespace.functions)
    body += map(partial(gen_class, package), namespace.classes)

    return body


def add_helpers(namespace):
    for enum in namespace.enums:
        C.Helper.add_helper(enum.name + '_to_java_enum',
            C.Function(enum.name + '_to_java_enum',
                return_type='jobject',
                params=['JNIEnv* env', enum.type.c_type + ' value'],
                body=[
                    C.Decl('jfieldID', 'fieldId'),
                    C.Decl('jobject', 'result'),
                    '',
                    C.Switch('value', cases=[
                        (member.c_name, C.Assign('fieldId', C.Cache.default_enum_member(enum, member)))
                        for member in enum.members
                    ]),
                    '',
                    C.Assert('fieldId'),
                    C.Assign('result', Env('GetStaticObjectField', C.Cache(enum.name), 'fieldId')),
                    C.ExceptionCheck('NULL'),
                    C.Return('result'),
                ]
            )
        )


def gen_source(namespaces, include_headers):
    body = []
    package = config.PACKAGE_ROOT

    for namespace in namespaces:
        add_helpers(namespace)

    for namespace in namespaces:
        body += gen_namespace(namespace, package)

    jobject_wrapper_struct = C.Block(
        _start = 'typedef union {',
        body = [
            C.Decl('jobject', 'obj'),
            C.Decl('jweak', 'weak'),
        ],
        _end = '} JObjectWrapper;',
    )

    jobject_callback_wrapper_struct = C.Block(
        _start = 'typedef struct {',
        body = [
            C.Decl('JObjectWrapper', '*wrapper'),
            C.Decl('gboolean', 'should_destroy'),
        ],
        _end = '} JObjectCallbackWrapper;',
    )

    native_destructor = [C.JniExport(
        package=package,
        clazz='NativeInstance',
        method_name='nativeDestructor',
        return_type='void',
        params=['jclass clazz', 'jlong instance_pointer'],
        body=[
            C.Decl('GWeakRef*', 'ref'),
            C.Decl('GObject*', 'gobj'),
            C.Decl('JObjectWrapper*', 'wrapper'),
            '(void) clazz;',
            '',
            C.Assign('ref', 'instance_pointer', cast='GWeakRef*'),
            C.Assign('gobj', C.Call('g_weak_ref_get', 'ref')),
            C.Call('g_weak_ref_clear', 'ref'),
            C.Call('g_free', 'ref'),
            '',
            C.If('!gobj',
                C.Env.throw('IllegalStateException', '"GObject ref was NULL at finalization"'),
                C.Return()),
            C.Log('debug', 'unrefing GObject[%p]', 'gobj'),
            C.Assign('wrapper', C.Call('g_object_get_data', 'gobj', '"java_instance"'), cast='JObjectWrapper*'),
            C.If('wrapper', [
                C.Call('g_object_set_data', 'gobj', '"java_instance"', 'NULL'),
                C.Helper('jobject_wrapper_destroy', 'wrapper', 'TRUE'),
            ]),
            C.Call('g_object_unref', 'gobj'),
        ]),
    ]

    helper_functions = Helper.enumerate_used_helpers()

    gobject_class_cache = [
        C.Call('g_hash_table_insert', 'gobject_to_java_class_map', C.Call(clazz.glib_get_type), Cache.default_class(clazz.value))
    for clazz in namespace.classes for namespace in namespaces];

    # cached classes need to be enumerated last
    cache_declarations, jni_onload_cache = C.Cache.enumerate_cached_classes()

    jni_onload = Function(
        name='JNI_OnLoad',
        return_type='jint',
        params=['JavaVM* vm', 'void* reserved'],
        modifiers=[],
        body=[
            C.Decl('JNIEnv*', 'env'),
            '',
            C.Assign('jvm', 'vm'),
            C.Assign('env', C.Call('get_jni_env')),
            '',
            jni_onload_cache,
            '',
            C.Assign('gobject_to_java_class_map', C.Call('g_hash_table_new', 'g_direct_hash', 'g_direct_equal')),
            '',
            gobject_class_cache,
            '',
            C.Return('JNI_VERSION_1_6'),
        ]
    )

    include_headers = ['jni.h', 'android/log.h'] + include_headers
    includes = '\n'.join('#include <' + h + '>' for h in include_headers)

    body = [
        includes,
        HEADER,
        cache_declarations,
        C.Decl('static GHashTable*', 'gobject_to_java_class_map'),
        GET_JNI_ENV,
        jni_onload,
        jobject_wrapper_struct,
        jobject_callback_wrapper_struct,
    ] + helper_functions + [native_destructor] + body

    body = intersperse(prune_empty(body), '')

    return flatjoin(body, '\n')



HEADER = """
#define android_assert(st) if (!(st)) {{ __android_log_write(ANDROID_LOG_ERROR, "OpenWebRTC", "Assertion failed at "G_STRINGIFY(__LINE__));}}
#undef g_assert
#define g_assert android_assert

#define log_verbose(st, ...) __android_log_print(ANDROID_LOG_VERBOSE, "{0}", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);
#define log_debug(st, ...) __android_log_print(ANDROID_LOG_DEBUG, "{0}", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);
#define log_info(st, ...) __android_log_print(ANDROID_LOG_INFO, "{0}", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);
#define log_warning(st, ...) __android_log_print(ANDROID_LOG_WARN, "{0}", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);
#define log_error(st, ...) __android_log_print(ANDROID_LOG_ERROR, "{0}", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);
""".format(config.LOG_TAG)

GET_JNI_ENV = [
    C.Decl('static JavaVM*', 'jvm'),
    C.Decl('static pthread_key_t', 'pthread_detach_key = 0'),
    '',
    C.Function('detach_current_thread',
        params=['void* pthread_key'],
        body=[
            C.Decl('(void)', 'pthread_key'),
            C.Call('g_return_if_fail', 'jvm'),
            '',
            C.Log.debug('JNI: detaching current thread from Java VM: %ld', C.Call('pthread_self')),
            '',
            C.Call('(*jvm)->DetachCurrentThread', 'jvm'),
            C.Call('pthread_setspecific', 'pthread_detach_key', 'NULL'),
        ]
    ),
    '',
    C.Function('get_jni_env',
        return_type='JNIEnv*',
        params=[],
        body=[
            C.Decl('JNIEnv*', 'env'),
            C.Decl('int', 'ret'),
            '',
            C.Assign('env', 'NULL'),
            C.Assign('ret', C.Call('(*jvm)->GetEnv', 'jvm', '(void**)&env', 'JNI_VERSION_1_6')),
            '',
            C.IfElse(ifs=['ret == JNI_EDETACHED', 'ret == JNI_EVERSION'],
                bodies=[
                    C.IfElse(ifs=['(*jvm)->AttachCurrentThread(jvm, (JNIEnv**) &env, NULL) != 0'],
                        bodies=[
                            C.Log.error('JNI: failed to attach thread'), [
                                C.Log.info('JNI: successfully attached to thread'),
                                C.If(C.Call('pthread_key_create', '&pthread_detach_key', 'detach_current_thread'),
                                    C.Log.error('JNI: failed to set detach callback')),
                                C.Call('pthread_setspecific', 'pthread_detach_key', 'jvm'),
                            ]
                        ]),
                    C.Log.error('JNI: version not supported'),
                ]
            ),
            '',
            C.Assert('env'),
            C.Return('env'),
        ]
    ),
]
