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


import config
from type_registry import GirMetaType
from type_registry import TypeTransform
from c_generator import C


C.Helper.add_helper('jobject_wrapper_create',
    C.Function('jobject_wrapper_create',
        return_type='JObjectWrapper*',
        params=['jobject jobj', 'gboolean weak'],
        body=[
            C.Decl('JNIEnv*', 'env'),
            C.Decl('JObjectWrapper*', 'wrapper'),
            '',
            C.Assign('env', C.Call('get_jni_env')),
            C.Assign('wrapper', C.Call('g_slice_new0', 'JObjectWrapper')),
            C.Assert('wrapper'),
            C.IfElse(ifs=['weak'],
                bodies=[[
                    C.Assign('wrapper->weak', C.Env('NewWeakGlobalRef', 'jobj')),
                    C.Log.info('created weak global ref: %p', 'wrapper->weak'),
                ],[
                    C.Assign('wrapper->obj', C.Env('NewGlobalRef', 'jobj')),
                    C.Log.info('created global ref: %p', 'wrapper->obj'),
                ]]
            ),
            C.ExceptionCheck('NULL'),
            '',
            C.Return('wrapper'),
        ]
    )
)

C.Helper.add_helper('jobject_wrapper_destroy',
    C.Function('jobject_wrapper_destroy',
        return_type='void',
        params=['gpointer data_pointer', 'gboolean weak'],
        body=[
            C.Decl('JNIEnv*', 'env'),
            C.Decl('JObjectWrapper*', 'wrapper'),
            '',
            C.Assign('env', C.Call('get_jni_env')),
            C.Assign('wrapper', 'data_pointer', cast='JObjectWrapper*'),
            C.Assert('wrapper'),
            '',
            C.IfElse(ifs=['weak'],
                bodies=[[
                    C.Log.info('finalizing weak global ref: %p', 'wrapper->weak'),
                    C.Env('DeleteWeakGlobalRef', 'wrapper->weak'),
                ],[
                    C.Log.info('finalizing global ref: %p', 'wrapper->obj'),
                    C.Env('DeleteGlobalRef', 'wrapper->obj'),
                ]]
            ),
            '',
            C.Call('g_slice_free', 'JObjectWrapper', 'wrapper'),
            C.ExceptionCheck(None),
        ]
    )
)

C.Helper.add_helper('jobject_wrapper_closure_notify',
    C.Function('jobject_wrapper_closure_notify',
        return_type='void',
        params=['gpointer data_pointer', 'GClosure* ignored'],
        body=[
            C.Decl('(void)', 'ignored'),
            C.Helper('jobject_wrapper_destroy', 'data_pointer', 'FALSE'),
        ]
    )
)

C.Helper.add_helper('gobject_to_jobject',
    C.Function('gobject_to_jobject',
        return_type='jobject',
        params=['JNIEnv* env', 'gpointer data_pointer', 'jclass clazz', 'gboolean take_ref'],
        body=[
            C.Decl('GObject*', 'gobj'),
            C.Decl('JObjectWrapper*', 'wrapper'),
            '',
            C.If('!data_pointer',
                C.Log.debug('got jobject[NULL] from GObject[null]'),
                C.Return('NULL')),
            C.Assign('gobj', C.Call('G_OBJECT', 'data_pointer')),
            '',
            C.Assign('wrapper', C.Call('g_object_get_data', 'gobj', '"java_instance"'), cast='JObjectWrapper*'),
            C.IfElse(ifs=['wrapper'],
                bodies=[[
                    C.Log.debug('got jobject[%p] from gobject[%p]', 'wrapper->obj', 'gobj'),
                    C.Return('wrapper->obj'),
                ], [
                    C.Decl('jobject', 'jobj'),
                    C.Decl('jobject', 'native_pointer'),
                    C.Decl('GWeakRef*', 'ref'),
                    '',
                    C.If('take_ref', C.Call('g_object_ref', 'gobj')),
                    '',
                    C.Assign('ref', C.Call('g_new', 'GWeakRef', '1')),
                    C.Call('g_weak_ref_init', 'ref', 'gobj'),
                    '',
                    C.Assign('native_pointer', C.Env.new('NativePointer', '(jlong) ref')),
                    C.ExceptionCheck('NULL'),
                    '',
                    C.Assign('jobj', C.Env('NewObject', 'clazz', C.Cache.method('NativeInstance', '_constructor'), 'native_pointer')),
                    C.ExceptionCheck('NULL'),
                    '',
                    C.Assign('wrapper', C.Helper('jobject_wrapper_create', 'jobj', 'TRUE')),
                    C.Assert('wrapper'),
                    C.Call('g_object_set_data', 'gobj', '"java_instance"', 'wrapper'),
                    '',
                    C.Log.debug('got jobject[%p] from GObject[%p]', 'jobj', 'gobj'),
                    C.Return('jobj'),
                ]]),
        ]
    )
)

C.Helper.add_helper('jobject_to_gobject',
    C.Function('jobject_to_gobject',
        return_type='gpointer',
        params=['JNIEnv* env', 'jobject jobj'],
        body=[
            C.Decl('GWeakRef*', 'ref'),
            C.Decl('gpointer', 'gobj'),
            '',
            C.If('!jobj',
                C.Log.debug('got GObject[NULL] from jobject[null]'),
                C.Return('NULL')),
            '',
            C.Assign('ref', C.Env.field('jobj', ('NativeInstance', 'nativeInstance')), cast='GWeakRef*'),
            C.Assign('gobj', C.Call('g_weak_ref_get', 'ref')),
            C.If('!gobj',
                C.Env.throw('IllegalStateException', '"GObject ref was NULL at translation"')),
            C.Log.debug('got gobject[%p] from jobject[%p]', 'gobj', 'jobj'),
            C.Return('gobj'),
        ]
    )
)


class PrimitiveMetaType(GirMetaType):
    default_value = '0'

    def __init__(self, name, transfer_ownership=False, allow_none='Ignored'):
        assert transfer_ownership == False
        super(PrimitiveMetaType, self).__init__(name, transfer_ownership, allow_none=True)

    def __new__(cls, java_type, jni_type, c_type, java_signature, object_type):
        new = super(PrimitiveMetaType, cls).__new__(cls)
        new.gir_type = c_type
        new.java_type = java_type
        new.jni_type = jni_type
        new.c_type = c_type
        new.java_signature = java_signature
        new.object_type = object_type
        new.object_full_type = 'java.lang.' + object_type
        return new

    def transform_to_c(self):
        return TypeTransform([
            C.Decl(self.c_type, self.c_name),
        ],[
            C.Assign(self.c_name, self.jni_name, cast=self.c_type)
        ])

    def transform_to_jni(self):
        return TypeTransform([
            C.Decl(self.jni_type, self.jni_name),
        ],[
            C.Assign(self.jni_name, self.c_name, cast=self.jni_type)
        ])


class CharType   (PrimitiveMetaType('byte',    'jbyte',    'gchar',    'B', 'Byte')): pass
class UcharType  (PrimitiveMetaType('byte',    'jbyte',    'guchar',   'B', 'Byte')): pass
class Int8Type   (PrimitiveMetaType('byte',    'jbyte',    'gint8',    'B', 'Byte')): pass
class Uint8Type  (PrimitiveMetaType('byte',    'jbyte',    'guint8',   'B', 'Byte')): pass
class ShortType  (PrimitiveMetaType('short',   'jshort',   'gshort',   'S', 'Short')): pass
class UshortType (PrimitiveMetaType('short',   'jshort',   'gushort',  'S', 'Short')): pass
class Int16Type  (PrimitiveMetaType('short',   'jshort',   'gint16',   'S', 'Short')): pass
class Uint16Type (PrimitiveMetaType('short',   'jshort',   'guint16',  'S', 'Short')): pass
class IntType    (PrimitiveMetaType('int',     'jint',     'gint',     'I', 'Integer')): pass
class UintType   (PrimitiveMetaType('int',     'jint',     'guint',    'I', 'Integer')): pass
class Uint32Type (PrimitiveMetaType('int',     'jint',     'gint32',   'I', 'Integer')): pass
class Int32Type  (PrimitiveMetaType('int',     'jint',     'guint32',  'I', 'Integer')): pass
class LongType   (PrimitiveMetaType('long',    'jlong',    'glong',    'J', 'Long')): pass
class UlongType  (PrimitiveMetaType('long',    'jlong',    'gulong',   'J', 'Long')): pass
class LongPtrType(PrimitiveMetaType('long',    'jlong',    'gpointer', 'J', 'Long')): pass
class SizeType   (PrimitiveMetaType('long',    'jlong',    'gsize',    'J', 'Long')): pass
class SsizeType  (PrimitiveMetaType('long',    'jlong',    'gssize',   'J', 'Long')): pass
class OffsetType (PrimitiveMetaType('long',    'jlong',    'goffset',  'J', 'Long')): pass
class Int64Type  (PrimitiveMetaType('long',    'jlong',    'gint64',   'J', 'Long')): pass
class Uint64Type (PrimitiveMetaType('long',    'jlong',    'guint64',  'J', 'Long')): pass
class BooleanType(PrimitiveMetaType('boolean', 'jboolean', 'gboolean', 'Z', 'Boolean')): pass
class FloatType  (PrimitiveMetaType('float',   'jfloat',   'gfloat',   'F', 'Float')): pass
class DoubleType (PrimitiveMetaType('double',  'jdouble',  'gdouble',  'D', 'Double')): pass


class GWeakRefType(PrimitiveMetaType('long',    'jlong',    'gpointer', 'J', 'Long')):
    def transform_to_jni(self):
        ref = self.c_name + '_ref'
        return TypeTransform([
            C.Decl('GWeakRef*', ref),
            C.Decl(self.jni_type, self.jni_name),
        ],[
            C.Assign(ref, C.Call('g_new', 'GWeakRef', '1')),
            C.Call('g_weak_ref_init', ref, self.c_name),
            C.Assign(self.jni_name, ref, cast=self.jni_type),
        ])


class VoidType(GirMetaType()):
    gir_type = 'none'
    java_type = 'void'
    jni_type = 'void'
    c_type = 'void'
    java_signature = 'V'
    default_value = None

    def __init__(self, name=None, transfer_ownership=False, allow_none=False):
        super(VoidType, self).__init__(None)

    def transform_to_c(self):
        raise AssertionError('VoidType.transform_to_c should not be reached')

    def transform_to_jni(self):
        raise AssertionError('VoidType.transform_to_jni should not be reached')


class GParamSpecType(GirMetaType()):
    gir_type = None
    java_type = None
    jni_type = None
    c_type = 'GParamSpec*'
    java_signature = None

    def transform_to_c(self):
        return TypeTransform()

    def transform_to_jni(self):
        return TypeTransform()


class ObjectMetaType(GirMetaType):
    jni_type = 'jobject'
    default_value = 'NULL'

    def __new__(cls, gir_type, java_type, c_type, package):
        new = super(ObjectMetaType, cls).__new__(cls)
        if java_type:
            new.java_full_class = package + '.' + java_type
            new.java_class_path = new.java_full_class.replace('.', '/')
            new.java_signature = 'L' + new.java_class_path + ';'

        new.gir_type = gir_type
        new.java_type = java_type
        new.c_type = c_type
        return new


class JObjectWrapperType(ObjectMetaType(
        gir_type='gpointer',
        java_type=None,
        c_type='gpointer',
        package=None,
    )):

    def __init__(self, name, closure, transfer_ownership):
        super(JObjectWrapperType, self).__init__(name, transfer_ownership, allow_none=False)
        if closure is None:
            closure = self
        self.closure = closure

    def transform_to_c(self):
        return TypeTransform([
            C.Decl(self.c_type, self.c_name),
        ],[
            C.Assign(self.c_name, C.Helper('jobject_wrapper_create', self.closure.jni_name, 'FALSE')),
        ])

    def transform_to_jni(self):
        return TypeTransform([
            C.Decl(self.jni_type, self.jni_name),
        ],[
            C.Assign(self.jni_name, '((JObjectWrapper*) ' + self.c_name + ')->obj;'),
        ], self.transfer_ownership and [
            C.Helper('jobject_wrapper_destroy', self.c_name, 'FALSE'),
        ])


class EnumMetaType(ObjectMetaType):
    def __new__(cls, gir_type, c_type, prefix):
        return super(EnumMetaType, cls).__new__(cls,
            gir_type=gir_type,
            java_type=gir_type,
            c_type=c_type,
            package=config.PACKAGE_ROOT + '.' + prefix,
        )

    def transform_to_c(self):
        return TypeTransform([
            C.Decl(self.c_type, self.c_name),
        ],[
            C.Assign(self.c_name, C.Env.method(self.jni_name, ('ValueEnum', 'getValue')), cast=self.c_type),
            C.ExceptionCheck.default(self),
        ])

    def transform_to_jni(self):
        return TypeTransform([
            C.Decl(self.jni_type, self.jni_name),
        ],[
            C.Assign(self.jni_name, C.Helper(self.gir_type + '_to_java_enum', 'env', self.c_name)),
        ])


class CallbackMetaType(ObjectMetaType):
    def __new__(cls, gir_type, c_type, prefix):
        return super(CallbackMetaType, cls).__new__(cls,
            gir_type=gir_type,
            java_type=gir_type,
            c_type=c_type,
            package=config.PACKAGE_ROOT + '.' + prefix,
        )

    def transform_to_c(self):
        return TypeTransform([
            C.Decl(self.c_type, self.c_name),
        ],[
            C.Assign(self.c_name, 'G_CALLBACK(callback_' + self.gir_type + ')'),
        ])


class ClassCallbackMetaType(CallbackMetaType):
    def __new__(cls, java_type, outer):
        new = super(ClassCallbackMetaType, cls).__new__(cls,
            gir_type=java_type,
            c_type='GCallback',
            prefix='ignored',
        )
        new.outer_java_type = outer.java_type
        new.gir_type = outer.gir_type + '_' + java_type
        new.java_full_class = outer.java_full_class + '.' + java_type
        new.java_class_path = outer.java_class_path + '$' + java_type
        new.java_signature = 'L' + new.java_class_path + ';'
        return new


class GObjectMetaType(ObjectMetaType):
    def __new__(cls, gir_type, c_type, prefix):
        return super(GObjectMetaType, cls).__new__(cls,
            gir_type=gir_type,
            java_type=gir_type,
            c_type=c_type + '*',
            package=config.PACKAGE_ROOT + '.' + prefix if prefix is not None else config.PACKAGE_ROOT,
        )

    def transform_to_c(self):
        return TypeTransform([
            C.Decl(self.c_type, self.c_name),
        ],[
            C.Assign(self.c_name, C.Helper('jobject_to_gobject', 'env', self.jni_name)),
            C.Call('g_object_ref', self.c_name) if self.transfer_ownership else [],
        ])

    def transform_to_jni(self):
        return TypeTransform([
            C.Decl(self.jni_type, self.jni_name),
        ],[
            C.Assign(self.jni_name, C.Helper('gobject_to_jobject',
                'env', self.c_name, C.Cache.default_class(self), 'TRUE' if not self.transfer_ownership else 'FALSE'))
        ])


class StringMetaType(ObjectMetaType):
    def __new__(cls, c_type):
        return super(StringMetaType, cls).__new__(cls,
            gir_type='utf8',
            java_type='String',
            c_type=c_type,
            package='java.lang',
        )

    def transform_to_c(self):
        if self.transfer_ownership:
            tmp = self.c_name + '_tmp'
            return TypeTransform([
                C.Decl(self.c_type, self.c_name),
                C.Decl(self.c_type, tmp),
            ],[
                C.Assign(tmp, Env('GetStringUTFChars', self.jni_name, 'NULL'), cast=self.c_type),
                C.ExceptionCheck.default(self),
                C.Assign(self.c_name, Call('g_strdup', tmp)),
            ],[
                C.Env('ReleaseStringUTFChars', self.jni_name, tmp),
            ])
        else:
            return TypeTransform([
                C.Decl(self.c_type, self.c_name),
            ],[
                C.Assign(self.c_name, C.Env('GetStringUTFChars', self.jni_name, 'NULL'), cast=self.c_type),
                C.ExceptionCheck.default(self),
            ],[
                C.Env('ReleaseStringUTFChars', self.jni_name, self.c_name),
            ])


    def transform_to_jni(self):
        return TypeTransform([
            C.Decl(self.jni_type, self.jni_name),
        ],[
            C.Assign(self.jni_name, C.Env('NewStringUTF', self.c_name))
        ], self.transfer_ownership and [
            C.Call('g_free', self.c_name),
        ])


class ContainerMetaType(ObjectMetaType):
    is_container = True

    def __init__(self, name, transfer_ownership, allow_none, *inner_values):
        super(ContainerMetaType, self).__init__(name, transfer_ownership, allow_none)
        self.inner_values = inner_values
        self.java_type = '%s<%s>' % (self.java_type, ', '.join(typ.object_type for typ in self.inner_values))
        self.java_full_class = '%s<%s>' % (self.java_full_class, ', '.join(typ.object_full_type for typ in self.inner_values))

    def __new__(cls, gir_type, java_type, c_type):
        return super(ContainerMetaType, cls).__new__(cls,
            gir_type=gir_type,
            java_type=java_type,
            c_type=c_type,
            package='java.util',
        )

    def transform_to_jni(self):
        inner_transforms = [value.transform_to_jni() for value in self.inner_values]
        return TypeTransform(
            sum([transform.declarations for transform in inner_transforms], []),
            sum([transform.conversion for transform in inner_transforms], []),
            sum([transform.cleanup for transform in reversed(inner_transforms)], []),
        )


class BitfieldMetaType(ContainerMetaType):
    def __init__(self, name, transfer_ownership, allow_none):
        super(BitfieldMetaType, self).__init__(name, transfer_ownership, allow_none,
            self.inner_type(name + '_enum'))
        (self.inner_value,) = self.inner_values

    def __new__(cls, gir_type, c_type, prefix=None):
        new = super(BitfieldMetaType, cls).__new__(cls,
            gir_type=gir_type,
            java_type='EnumSet',
            c_type=c_type,
        )
        new.inner_type = EnumMetaType(gir_type, c_type, prefix)
        return new

    def transform_to_c(self):
        it = self.jni_name + '_iterator'
        enum = self.inner_value.jni_name
        return TypeTransform([
            C.Decl(self.c_type, self.c_name),
            C.Decl('jobject', it),
            C.Decl('jobject', enum),
        ],[
            C.Assign(self.c_name, '0'),
            C.Assign(it, C.Env.method(self.jni_name, ('Iterable', 'iterator'))),
            C.While(C.Env.method(it, ('Iterator', 'hasNext')),
                C.Assign(enum, C.Env.method(it, ('Iterator', 'next'))),
                C.Assign(self.c_name, C.Env.method(enum, ('ValueEnum', 'getValue')), cast=self.c_type, op='|='),
                C.ExceptionCheck.default(self),
            )
        ])

    def transform_to_jni(self):
        enum = self.inner_value
        return TypeTransform([
            C.Decl(self.jni_type, self.jni_name),
            C.Decl(enum.jni_type, enum.jni_name),
            C.Decl(enum.c_type, enum.c_name),
        ],[
            C.Assign(self.jni_name, C.Env.static_method(('EnumSet', 'noneOf'), C.Cache(enum.java_type))),
            C.While(self.c_name,
                C.Assign(enum.c_name, "{0} & -{0}".format(self.c_name)),
                C.Assign(enum.jni_name, C.Helper(self.gir_type + '_to_java_enum', 'env', enum.c_name)),
                C.Env.method(self.jni_name, ('EnumSet', 'add'), enum.jni_name),
                C.Assign(self.c_name, "{0} & ({0} - 1)".format(self.c_name)),
            )
        ])


class GListType(ContainerMetaType(
        gir_type='GLib.List',
        java_type='List',
        c_type='GList*',
    )):

    def __init__(self, *args, **kwargs):
        super(GListType, self).__init__(*args, **kwargs)
        (self.inner_value,) = self.inner_values

    def transform_to_jni(self):
        it = self.c_name + '_it'
        inner_transforms = super(GListType, self).transform_to_jni()
        return TypeTransform([
            C.Decl(self.jni_type, self.jni_name),
            C.Decl(self.c_type, it),
            C.Decl(self.inner_value.c_type, self.inner_value.c_name),
            inner_transforms.declarations,
        ],[
            C.Assign(self.jni_name, C.Env.new('ArrayList')),
            C.Assign(it, self.c_name),
            C.While(it,
                C.Assign(self.inner_value.c_name, it + '->data'),
                inner_transforms.conversion,
                C.Env.method(self.jni_name, ('ArrayList', 'add'), self.inner_value.jni_name),
                C.ExceptionCheck.default(self),
                inner_transforms.cleanup,
                C.Assign(it, it + '->next'),
            ),
        ])


standard_types = [
    VoidType,
    CharType,
    UcharType,
    Int8Type,
    Uint8Type,
    ShortType,
    UshortType,
    Int16Type,
    Uint16Type,
    IntType,
    UintType,
    Uint32Type,
    Int32Type,
    LongType,
    UlongType,
    LongPtrType,
    SizeType,
    SsizeType,
    OffsetType,
    Int64Type,
    Uint64Type,
    BooleanType,
    FloatType,
    DoubleType,
    StringMetaType('gchar*'),
    StringMetaType('const gchar*'),
    GListType,
]
