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
import re
import os
import sys
import errno
import copy
import argparse

 ######   #######  ##    ##  ######  ########
##    ## ##     ## ###   ## ##    ##    ##
##       ##     ## ####  ## ##          ##
##       ##     ## ## ## ##  ######     ##
##       ##     ## ##  ####       ##    ##
##    ## ##     ## ##   ### ##    ##    ##
 ######   #######  ##    ##  ######     ##

parser = argparse.ArgumentParser()
parser.add_argument('--gir', dest = 'gir', metavar = 'FILE', help = '.gir file')
parser.add_argument('--c-out', dest = 'c_dir', metavar = 'DIR', help = '.c output directory')
parser.add_argument('--j-out', dest = 'j_dir', metavar = 'DIR', help = '.java base output directory')
args = parser.parse_args()

if args.gir:
    print 'reading from gir file "{}"'.format(args.gir)
else:
    print 'missing gir input file (--gir)'
if args.c_dir:
    print 'generating C source to "{}"'.format(args.c_dir)
else:
    print 'missing C output directory (--c-out)'
if args.j_dir:
    print 'generating Java source to "{}"'.format(args.j_dir)
else:
    print 'missing Java output directory (--j-out)'

if not all(args.__dict__.values()):
    print "all arguments must be set"
    sys.exit(-1)

OUT_FILE = "owr_jni.c"
PACKAGE_ROOT = 'com.ericsson.research'

C_HEAD = """ \
/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include <android/log.h>
#include <android/native_window_jni.h>

#include <jni.h>

#include <owr.h>
#include <owr_audio_payload.h>
#include <owr_audio_renderer.h>
#include <owr_candidate.h>
#include <owr_image_renderer.h>
#include <owr_image_server.h>
#include <owr_local.h>
#include <owr_local_media_source.h>
#include <owr_media_renderer.h>
#include <owr_media_session.h>
#include <owr_media_source.h>
#include <owr_payload.h>
#include <owr_remote_media_source.h>
#include <owr_session.h>
#include <owr_transport_agent.h>
#include <owr_types.h>
#include <owr_video_payload.h>
#include <owr_video_renderer.h>
#include <owr_window_registry.h>


#define android_assert(st) if (!(st)) { __android_log_write(ANDROID_LOG_ERROR, "OpenWebRTC", "Assertion failed at "G_STRINGIFY(__LINE__));}
#undef g_assert
#define g_assert android_assert

#define log_verbose(st, ...) __android_log_print(ANDROID_LOG_VERBOSE, "OpenWebRTC", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);
#define log_debug(st, ...) __android_log_print(ANDROID_LOG_DEBUG, "OpenWebRTC", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);
#define log_info(st, ...) __android_log_print(ANDROID_LOG_INFO, "OpenWebRTC", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);
#define log_warning(st, ...) __android_log_print(ANDROID_LOG_WARN, "OpenWebRTC", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);
#define log_error(st, ...) __android_log_print(ANDROID_LOG_ERROR, "OpenWebRTC", "["G_STRINGIFY(__LINE__)"]: "st, ##__VA_ARGS__);

static GHashTable* class_cache_table;

"""

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

JAVA_TYPE_SIGNATURES = {
    'void': 'V',
    'java.lang.String': 'Ljava/lang/String;',
    'java.lang.Object': 'Ljava/lang/Object;',
    'boolean': 'Z',
    'byte': 'B',
    'char': 'C',
    'short': 'S',
    'int': 'I',
    'long': 'J',
    'float': 'F',
    'double': 'D',
    'java.util.List<>': 'Ljava/util/List;',
    'java.util.Map<>': 'Ljava/util/Map;',
    'java.util.ArrayList<>': 'Ljava/util/ArrayList;',
    'java.util.HashMap<>': 'Ljava/util/HashMap;'
}

quot = '"{}"'.format

# boolean     jboolean    unsigned 8 bits
# byte        jbyte       signed 8 bits
# char        jchar       unsigned 16 bits
# short       jshort      signed 16 bits
# int         jint        signed 32 bits
# long        jlong       signed 64 bits
# float       jfloat      32 bits
# double      jdouble     64 bits
# void        void        N/A


##      ## ########  #### ######## ######## ########
##  ##  ## ##     ##  ##     ##    ##       ##     ##
##  ##  ## ##     ##  ##     ##    ##       ##     ##
##  ##  ## ########   ##     ##    ######   ########
##  ##  ## ##   ##    ##     ##    ##       ##   ##
##  ##  ## ##    ##   ##     ##    ##       ##    ##
 ###  ###  ##     ## ####    ##    ######## ##     ##


class Writer:
    indentation_size = 4

    def __init__(self, path, filename):
        self.indentation = 0
        self.indented = True

        try:
            os.makedirs(path)
        except OSError as e:
            if e.errno == errno.EEXIST and os.path.isdir(path):
                pass
            else:
                raise
        self.file = open(path + os.sep + filename, 'w')

    def out(self, st):
        self.file.write(st)

    def outln(self, st):
        self.out(st + '\n')

    def indent(self):
        if not self.indented:
            self.out(' ' * self.indentation_size * self.indentation)
            self.indented = True

    def push(self):
        if not self.indented:
            self.indent()
        else:
            self.out(' ')
        self.out('{')
        self.line()

        self.indentation += 1

    def pop(self, newlines = 1):
        self.indentation -= 1
        if self.indentation < 0:
            self.file.close()
        else:
            self.indent()
            self.out('}')
            self.out('\n' * newlines)
            if (newlines > 0):
                self.indented = False

    def line(self, st = None, push = False):
        if st is not None:
            self.indent()
            self.out(st)

        if push:
            self.push()
        else:
            self.out('\n')
            self.indented = False

    def state(self, st, push = False):
        self.indent()
        self.out(st)

        if push:
            self.push()
        else:
            self.out(';')
            self.line()

    def assignment(self):
        self.out(' = ')

    def par(self, st):
        self.out('({})'.format(st))

    def semi(self):
        self.out(';')
        self.line()

    def ret(self, st = None):
        self.indent()
        if st is not None:
            self.out('return ' + st)
            self.semi()
            self.pop()
        else:
            self.out('return ')

    def lval(self, name):
        self.previous_lval = name
        self.indent()
        self.out(name)
        self.assignment()

    def rval(self, st):
        self.out(st)
        self.semi()

    def cast(self, name):
        self.indent()
        self.par(name)
        self.out(' ')

    def comment(self, comment):
        self.outln('/* {} */'.format(comment))

    def call(self, name, *arguments):
        self.indent()
        self.out(name)
        self.par(', '.join(arguments))
        self.semi()

    def declare(self, typename, name):
        self.indent()
        self.out(typename)
        self.out(' ')
        self.out(name)
        self.semi()

    def case(self, st):
        self.indentation -= 1
        self.line('case {}:'.format(st))
        self.indentation += 1



      ## ##      ## ########  #### ######## ######## ########
      ## ##  ##  ## ##     ##  ##     ##    ##       ##     ##
      ## ##  ##  ## ##     ##  ##     ##    ##       ##     ##
      ## ##  ##  ## ########   ##     ##    ######   ########
##    ## ##  ##  ## ##   ##    ##     ##    ##       ##   ##
##    ## ##  ##  ## ##    ##   ##     ##    ##       ##    ##
 ######   ###  ###  ##     ## ####    ##    ######## ##     ##


class JavaWriter(Writer):

    def __init__(self, fqcn):
        split = fqcn.split('.')
        self.class_name = split[-1]
        self.packages = split[0:-1]
        self.package_name = '.'.join(split[0:-1])
        path = os.sep.join([args.j_dir] + split[0:-1])

        Writer.__init__(self, path, self.class_name + '.java')

        self.state('package ' + self.package_name)
        self.line()

    def class_declaration(self,
                          typename = 'class',
                          extends = None,
                          interfaces = [],
                          name = None,
                          visibility = 'public',
                          static = False
                          ):
        self.class_type = typename
        self.indent()
        self.out(visibility)
        if static:
            self.out(' static')
        self.out(' ' + typename + ' ' + (name or self.class_name))
        if extends is not None:
            self.out(' extends ' + extends)

        if interfaces != []:
            self.out(' implements ' + ', '.join(interfaces))

        self.push()

    def constructor(self,
                    parameters = [],
                    visibility = 'public'
                    ):
        self.indent()
        if visibility is not None:
            self.out(visibility + ' ')
        self.out(self.class_name)
        self.parameters(parameters)
        self.push()

    def method(self,
               obj = None,
               name = None,
               types = None,
               parameters = None,
               visibility = 'public',
               native = True,
               abstract = False,
               static = False
               ):
        name = name or obj and obj['camel_name']
        types = types or obj and obj['types'] or dict(java = 'void')
        parameters = parameters or obj and obj.get('parameters') or []

        self.indent()
        if visibility is not None:
            self.out(visibility + ' ')
        if static:
            self.out('static ')
        if native:
            self.out('native ')
        if abstract:
            self.out('abstract ')

        self.out(self.typename(types) + ' ')
        self.out(name)
        self.parameters(parameters)

        if self.class_type == 'interface':
            self.out(';')
            self.line()
            return

        if not native and not abstract:
            self.push()
        else:
            self.out(';')
            self.line()

    def typename(self, types):
        if types['java'][-2:] == '<>':
            return '%s<%s>' % (types['java'][:-2], ', '.join(map(self.typename, types['inner'])))
            return types['inner'][0]['java'] + '[]'
        else:
            return types['java']

    def parameter(self, parameter):
        return self.typename(parameter['types']) + ' ' + parameter['camel_name']

    def filter_user_data(self, parameters):
        return [p for p in parameters if p.get('c_name') != 'user_data']

    def argument(self, arg):
        return arg['camel_name']

    def parameters(self, parameters = []):
        self.out('(' + ', '.join(map(self.parameter, self.filter_user_data(parameters))) + ')')

    def arguments(self, arguments = []):
        self.out('(' + ', '.join(map(self.argument, self.filter_user_data(arguments))) + ')')


 ######  ##      ## ########  #### ######## ######## ########
##    ## ##  ##  ## ##     ##  ##     ##    ##       ##     ##
##       ##  ##  ## ##     ##  ##     ##    ##       ##     ##
##       ##  ##  ## ########   ##     ##    ######   ########
##       ##  ##  ## ##   ##    ##     ##    ##       ##   ##
##    ## ##  ##  ## ##    ##   ##     ##    ##       ##    ##
 ######   ###  ###  ##     ## ####    ##    ######## ##     ##


class CWriter(Writer):
    indentation_size = 4

    def __init__(self, namespace, root):
        Writer.__init__(self, args.c_dir, OUT_FILE)

        self.class_name = ''
        self.package = "{}.{}".format(root, namespace['symbol_prefix'])
        self.underscore_package = self.package.replace('.', '_')
        self.slash_package = self.package.replace('.', '/')

        self.enums = namespace['enums']
        self.callbacks = namespace['callbacks']
        self.symbol_prefix = namespace['symbol_prefix']
        self.identifier_prefix = namespace['identifier_prefix']

        self.out(C_HEAD)

    def setClassname(self, class_name):
        self.class_name = class_name
        self.line()
        self.outln('/**{}**/'.format(len(class_name) * '*'))
        self.outln('/* {} */'.format(class_name))
        self.outln('/**{}**/'.format(len(class_name) * '*'))
        self.line()

    def method(self, method, static = False):
        self.jni_function(
            return_value = method['types']['jni'],
            name = method['camel_name'],
            parameters = method['parameters'],
            static = static
        )

    def jni_function(self,
                     return_value = 'void',
                     name = None,
                     parameters = [],
                     static = False
                     ):
        self.out('JNIEXPORT ')
        self.out(self.str_jni_type(return_value))
        self.set_return_type(self.str_jni_type(return_value))
        self.out(' Java_' + self.underscore_package + '_')
        self.outln(self.class_name + '_' + name)
        self.indentation += 1
        self.indent()

        str_parameters = ['JNIEnv* env', 'jclass jclazz' if static else 'jobject jself']
        str_parameters += [p['types']['jni'] + ' ' + CWriter.str_jni_name(p)
                           for p in parameters if p.get('c_name') != 'user_data']
        self.par(', '.join(str_parameters))
        self.indentation -= 1
        self.line()
        self.push()

    def get_self(self):
        self.lval('self')
        self.cast(self.str_self_type())
        self.call('jobject_to_GObject', 'env', 'jself')

    def g_object_unref(self, name):
        self.state('g_object_unref({})'.format(name))

    def env(self, name, *args):
        self.indent()
        self.out('(*env)->{}(env, {})'.format(name, ', '.join(args)))
        self.semi()

    def make_global_ref(self):
        self.lval(self.previous_lval)
        self.env('NewGlobalRef', self.previous_lval)
        self.g_assert()

    def g_assert(self):
        self.call('android_assert', self.previous_lval)

    def set_return_type(self, return_type):
        self.return_type = return_type;

    def check_exception(self):
        if self.return_type and self.return_type != 'void':
            self.line('if ((*env)->ExceptionCheck(env)) return (%s) 0;' % self.return_type)
        else:
            self.line('if ((*env)->ExceptionCheck(env)) return;')

          ## ##    ## ####
          ## ###   ##  ##
          ## ####  ##  ##
          ## ## ## ##  ##
    ##    ## ##  ####  ##
    ##    ## ##   ###  ##
     ######  ##    ## ####

    def declare_self(self):
        self.declare(self.str_self_type(), 'self')

    def jni_declare(self, param):
        typename = self.str_jni_type(param)
        name = self.str_jni_name(param)
        self.declare(typename, name)
        if param['types']['java'] == 'java.util.List<>':
            self.declare(self.str_jni_type(param['types']['inner'][0]), '%s_item' % name)

    def jni_to_c(self, obj):
        jni_name = self.str_jni_name(obj)
        camel_name = obj['camel_name']
        c_type = obj['types']['c']
        jni_type = obj['types']['jni']
        java_type = obj['types']['java']

        if obj.get('c_name') == 'user_data':
            return
        if java_type in self.callbacks:
            self.lval(camel_name)
            self.rval('callback_{}'.format(java_type))
            self.lval('userData')
            self.call('user_data_create', jni_name)
            self.g_assert()
            return


        if java_type == 'java.util.List<>':
            self.line('NOT IMPLEMENTED: {} to {}'.format(java_type, c_type))
        elif java_type == 'java.util.Map<>':
            self.line('NOT IMPLEMENTED: {} to {}'.format(java_type, c_type))
        elif java_type == 'java.lang.String':
            self.lval('%s_jstring' % camel_name)
            self.cast(c_type)
            self.call('(*env)->GetStringUTFChars', 'env', jni_name, 'NULL')
            self.check_exception()
            self.lval(camel_name)
            self.call('g_strdup', '{}_jstring'.format(camel_name))
            self.g_assert()
        elif java_type == 'android.view.Surface':
            self.lval(camel_name)
            self.cast(c_type)
            self.call('ANativeWindow_fromSurface', 'env', jni_name)
            self.g_assert()
        elif java_type in self.enums:
            self.lval(camel_name)
            self.cast(c_type)
            self.call('jobject_to_enum_value', 'env', jni_name)
        elif jni_type == 'jobject':
            self.lval(camel_name)
            self.cast(c_type)
            self.call('jobject_to_GObject', 'env', jni_name)
            self.g_assert()
        else:
            self.lval(camel_name)
            self.cast(c_type)
            self.rval(jni_name)

    def cleanup_jni(self, obj):
        jni_name = self.str_jni_name(obj)
        c_name = obj['camel_name']
        c_type = obj['types']['c']
        jni_type = obj['types']['jni']
        java_type = obj['types']['java']

        if jni_type == 'jstring':
            self.call('(*env)->ReleaseStringUTFChars', 'env', jni_name, '%s_jstring' % c_name)
            self.check_exception();

    def jni_return_declare(self, obj):
        self.declare(self.str_c_type(obj), 'result')
        self.declare(self.str_c_type(obj), 'ret')
        self.declare(self.str_jni_type(obj), 'jRet')

    def return_jni_result(self, obj):
        ret = copy.copy(obj)
        ret['camel_name'] = 'ret'
        ret['title_name'] = 'Ret'

        self.lval('ret')
        if ret['types']['jni'] == 'jstring':
            self.call('g_strdup', 'result')
        else:
            self.rval('result')

        self.c_to_jni(ret)

        ret['camel_name'] = 'result'
        self.cleanup_c(ret, skip_transfer = 'none')

        self.line()
        self.ret('jRet')

     ######
    ##    ##
    ##
    ##
    ##
    ##    ##
     ######

    def c_declare(self, param):
        jni_type = self.str_jni_type(param)
        if jni_type == 'jstring':
            self.declare(self.str_c_type(param), '%s_jstring' % param['camel_name'])
        self.declare(self.str_c_type(param), param['camel_name'])

    def c_to_jni(self, obj):
        jni_name = self.str_jni_name(obj)
        c_name = obj['camel_name']
        c_type = obj['types']['c']
        jni_type = obj['types']['jni']
        java_type = obj['types']['java']

        if java_type == 'java.util.List<>' :
            obj_copy = copy.deepcopy(obj)
            obj_copy['types'] = obj_copy['types']['inner'][0]
            obj_copy['camel_name'] += '->data'
            obj_copy['title_name'] += '_item'
            self.lval(jni_name)
            self.call('create_jList', 'env')
            self.check_exception()
            self.g_assert()
            self.line('for (; {0} != NULL; {0} = {0}->next)'.format(c_name), push = True)
            self.c_to_jni(obj_copy)
            self.call('jList_add_item', 'env', jni_name, jni_name + '_item')
            self.cleanup_c(obj_copy)
            self.check_exception()
            self.pop()
        elif java_type == 'java.util.Map<>':
            pass
        elif java_type == 'java.lang.String':
            self.lval('%s_jstring' % c_name)
            if obj.get('transfer') == 'none':
                self.call('g_strdup', c_name)
            else:
                self.rval(c_name)
            self.lval(jni_name)
            self.cast(c_type)
            self.call('(*env)->NewStringUTF', 'env', '%s_jstring' % c_name)
            self.check_exception()
            if obj.get('transfer') == 'none':
                self.call('g_free', '%s_jstring' % c_name)
        elif java_type in self.enums:
            self.lval(jni_name)
            self.call('{}_to_java_enum'.format(c_type), 'env', c_name)
            self.g_assert()
        elif jni_type == 'jobject':
            self.lval(jni_name)
            self.call('GObject_to_jobject'.format(java_type), 'env', c_name)
            self.g_assert()
        else:
            self.lval(jni_name)
            self.cast(jni_type)
            self.rval(c_name)

    def cleanup_c(self, obj, skip_transfer = 'full'):
        jni_name = self.str_jni_name(obj)
        c_name = obj['camel_name']
        c_type = obj['types']['c']
        jni_type = obj['types']['jni']
        java_type = obj['types']['java']

        if obj['transfer'] == skip_transfer:
            return

        if jni_type == 'jobjectArray':
            if c_type == 'GList*':
                self.call('g_list_free_full', c_name, 'g_object_unref')
            else:
                self.line('NOT IMPLEMENTED: cleanup {}'.format(c_type))
        elif jni_type == 'jstring':
            if c_type in ['gchar*', 'const gchar*', 'char*', 'const char*']:
                self.call('g_free', '(void*) {}'.format(c_name))
            else:
                self.line('NOT IMPLEMENTED: cleanup {}'.format(c_type))
        elif jni_type == 'jobject':
            if java_type not in self.enums:
                self.g_object_unref(c_name)

    def c_return_declare(self, obj):
        self.declare(self.str_c_type(obj), 'ret')
        self.declare(self.str_c_type(obj), 'result')
        self.declare(self.str_jni_type(obj), 'jResult')

    def return_c_result(self, obj):
        ret = copy.copy(obj)
        ret['camel_name'] = 'result'
        ret['title_name'] = 'Result'

        self.jni_to_c(ret)

        self.lval('ret')
        if ret['types']['jni'] == 'jstring' and ret['transfer'] == 'none':
            if ret['types']['c'] in ['gchar*', 'const gchar*', 'char*', 'const char*']:
                self.call('g_strdup', 'result')
            else:
                self.rval('result')
        else:
            self.rval('result')

        self.cleanup_jni(ret)

        self.line()
        self.ret('ret')

     ######  ######## ########
    ##    ##    ##    ##     ##
    ##          ##    ##     ##
     ######     ##    ########
          ##    ##    ##   ##
    ##    ##    ##    ##    ##
     ######     ##    ##     ##

    def str_self_type(self):
        return self.str_ptr(self.identifier_prefix + self.class_name)

    @staticmethod
    def str_c_type(obj):
        if type(obj) == str:
            return obj
        elif obj.get('types') is not None:
            return obj['types']['c']
        elif obj.get('c') is not None:
            return obj['c']

    @staticmethod
    def str_jni_type(obj):
        if type(obj) == str:
            return obj
        elif obj.get('types') is not None:
            return obj['types']['jni']
        elif obj.get('jni') is not None:
            return obj['jni']

    def str_java_type(self, obj):
        name = None
        if type(obj) == str:
            name = obj
        else:
            name = (obj.get('types') or obj)['java']
            if obj.get('c_name') == 'user_data':
                return ''
        signature = JAVA_TYPE_SIGNATURES.get(name)
        if signature is None:
            return 'L{}/{};'.format(self.slash_package, name)
        if signature == '[]':
            return '[' + self.str_java_type(obj['types']['inner'][0])
        return signature

    def str_gobject_type(self, obj):
        if obj['types']['c'] is not None:
            return obj['types']['c']
        java_type = obj['types']['java']
        if java_type not in self.enums:
            java_type += '*'
        return '{}{}'.format(self.identifier_prefix, java_type)

    def str_class_signature(self, obj, inner = None):
        name = obj if type(obj) == str else obj.get('name')
        inner = inner and (inner if type(inner) == str else inner.get('title_name'))

        if inner:
            return '{}/{}${}'.format(self.slash_package, name, inner)
        else:
            return '{}/{}'.format(self.slash_package, name)

    def str_method_signature(self, obj):
        args = ''.join(map(self.str_java_type, obj['parameters']))
        sig = '({}){}'.format(args or '', self.str_java_type(obj))
        return sig

    @staticmethod
    def str_jni_call_name(obj):
        jni_type = CWriter.str_jni_type(obj)
        if jni_type in ['jarray']:
            jni_type = 'jobject'
        if jni_type == 'void':
            jni_type = '_void'
        if jni_type == 'jstring':
            jni_type = 'jobject'
        return jni_type[1:].title()

    @staticmethod
    def str_jni_name(obj):
        return obj.get('jni_name') or 'j{}'.format(obj['title_name'])

    @staticmethod
    def str_c_name(obj):
        return obj['camel_name']

    @staticmethod
    def str_ptr(st):
        return '{}*'.format(st)

    def str_linebreak(self):
        return '\n' + ' ' * self.indentation_size * (self.indentation + 1)


 ######   ######  ######## ########
##    ## ##    ##    ##    ##     ##
##       ##          ##    ##     ##
##        ######     ##    ########
##             ##    ##    ##   ##
##    ## ##    ##    ##    ##    ##
 ######   ######     ##    ##     ##

JNI_VERSION = 'JNI_VERSION_1_6'

CACHE_CLASS = 'class_{}'.format
CACHE_FIELD = 'field_{}_{}'.format
CACHE_STATIC_FIELD = 'field_static_{}_{}'.format
CACHE_METHOD = 'method_{}_{}'.format
CACHE_STATIC_METHOD = 'method_static_{}_{}'.format

TEMPL_TO_JAVA_ENUM = '{c_name}_to_java_enum'.format

STATIC_HELPER_METHODS = """\

static JNIEnv* get_jni_env()
{{
    JNIEnv* env = NULL;
    int ret;

    ret = (*jvm)->GetEnv(jvm, (void**)&env, {jni_version});

    if (ret == JNI_EDETACHED) {{
        if ((*jvm)->AttachCurrentThread(jvm, (JNIEnv**) &env, NULL) != 0) {{
            log_error("JNI: failed to attach thread");
        }} else {{
            log_info("JNI: successfully attached to thread");
        }}
    }} else if (ret == JNI_EVERSION) {{
        log_error("JNI: version not supported");
    }}

    g_assert(env);
    return env;
}}

""".format(
    jni_version = JNI_VERSION
) + """\
typedef struct {
    jobject self;
} UserData;

static UserData* user_data_create(jobject jself)
{
    JNIEnv* env;
    UserData* data;

    env = get_jni_env();
    data = g_slice_new0(UserData);
    g_assert(data);
    data->self = (*env)->NewGlobalRef(env, jself);
    if ((*env)->ExceptionCheck(env)) return NULL;

    log_info("created global ref: %p", data->self);

    return data;
}

static void user_data_destroy(gpointer data_pointer)
{
    JNIEnv* env;
    UserData* data;

    env = get_jni_env();
    data = (UserData*) data_pointer;

    log_info("finalizing global ref: %p", data->self);

    g_assert(data);
    (*env)->DeleteGlobalRef(env, data->self);
    if ((*env)->ExceptionCheck(env)) return;
    g_slice_free(UserData, data);
}

static void user_data_closure_notify(gpointer data_pointer, GClosure* ignored)
{
    (void) ignored;
    user_data_destroy(data_pointer);
}

static jobject GObject_to_jobject(JNIEnv* env, gpointer self_pointer)
{
    GObject* self = G_OBJECT(self_pointer);
    UserData* data;

    g_object_ref(self);
    data = (UserData*) g_object_get_data(self, "java_instance");

    if (data) {
        log_debug("got jobject[%p] from GObject data[%p]", data->self, self);
        return data->self;
    } else {
        jobject jself;
        jobject native_pointer;
        jclass* class_pointer;
        jclass clazz;
        jfieldID fieldId;
        jmethodID methodId;
        gchar* classname;

        classname = g_strdup_printf("com/ericsson/research/owr/%s", &(G_OBJECT_TYPE_NAME(self)[3]));
        log_verbose("searching for class: %s\\n", classname);
        class_pointer = (jclass*) g_hash_table_lookup(class_cache_table, classname);
        g_assert(class_pointer);
        clazz = *class_pointer;
        g_assert(clazz);
        g_free(classname);

        native_pointer = (*env)->NewObject(env, class_NativePointer, method_NativePointer__constructor, (jlong) self);
        if ((*env)->ExceptionCheck(env)) return NULL;

        fieldId = (*env)->GetFieldID(env, clazz, "nativeInstance", "J");
        if ((*env)->ExceptionCheck(env)) return NULL;
        methodId = (*env)->GetMethodID(env, clazz, "<init>", "(Lcom/ericsson/research/owr/NativePointer;)V");
        if ((*env)->ExceptionCheck(env)) return NULL;

        jself = (*env)->NewObject(env, clazz, methodId, native_pointer);
        if ((*env)->ExceptionCheck(env)) return NULL;

        data = user_data_create(jself);
        g_assert(data);
        g_object_set_data(self, "java_instance", data);

        log_debug("got jobject[%p] from GObject[%p]", jself, self);

        return jself;
    }
}

static gpointer jobject_to_GObject(JNIEnv* env, jobject jself)
{
    jclass clazz;
    jfieldID fieldId;
    gpointer self;

    clazz = (*env)->GetObjectClass(env, jself);
    if ((*env)->ExceptionCheck(env)) return NULL;
    fieldId = (*env)->GetFieldID(env, clazz, "nativeInstance", "J");
    if ((*env)->ExceptionCheck(env)) return NULL;

    self = (gpointer) (*env)->GetLongField(env, jself, fieldId);
    if ((*env)->ExceptionCheck(env)) return NULL;
    g_object_ref(self);

    log_debug("got GObject[%p] from jobject[%p]", self, jself);

    return self;
}

static guint jobject_to_enum_value(JNIEnv* env, jobject jself)
{
    jclass clazz;
    jfieldID fieldId;
    guint value;

    clazz = (*env)->GetObjectClass(env, jself);
    if ((*env)->ExceptionCheck(env)) return 0;
    fieldId = (*env)->GetFieldID(env, clazz, "nativeInstance", "I");
    if ((*env)->ExceptionCheck(env)) return 0;

    value = (guint) (*env)->GetLongField(env, jself, fieldId);

    if ((*env)->ExceptionCheck(env)) return 0;

    return value;
}

static jobject create_jList(JNIEnv* env)
{
    jobject list;

    list = (*env)->NewObject(env, class_ArrayList, method_ArrayList__constructor);
    if ((*env)->ExceptionCheck(env)) return NULL;

    return list;
}

static void jList_add_item(JNIEnv* env, jobject list, jobject item)
{
    (*env)->CallBooleanMethod(env, list, method_ArrayList_add, item);
    if ((*env)->ExceptionCheck(env)) return;
}
/*
static jobject create_jMap(JNIEnv* env)
{
    jobject map;

    map = (*env)->NewObject(env, class_HashMap, method_HashMap__constructor);
    if ((*env)->ExceptionCheck(env)) return NULL;

    return map;
}

static void jMap_add_item(JNIEnv* env, jobject map, jobject key, jobject value)
{
    (*env)->CallObjectMethod(env, map, method_HashMap_put, key, value);
    if ((*env)->ExceptionCheck(env)) return;
}
*/
"""

def cify_namespace(namespace):
    w = CWriter(namespace, PACKAGE_ROOT)

    enums = namespace['enums']

    enum_names = set()
    for enum in enums.values():
        enum_names.add(enum['name'])

    enums = [enums[name] for name in enum_names]

    # cache declarations

    w.state('static JavaVM* jvm')

    # cache helpers
    jni_onload_writers = []
    current_class = dict()
    def cache_class(name, *sigargs):
        w.line()
        w.declare('static jclass', CACHE_CLASS(name))
        current_class['name'] = name
        def jni_onload_writer():
            w.line()
            current_class['name'] = name
            if len(sigargs) == 1 and '/' in sigargs[0]:
                signature = quot(sigargs[0])
            else:
                signature = quot(w.str_class_signature(*sigargs))
            classname = CACHE_CLASS(name)
            w.lval(classname)
            w.env('FindClass', signature)
            w.check_exception()
            w.make_global_ref()
            w.call('g_hash_table_insert', 'class_cache_table', signature, '&' + w.previous_lval)
        jni_onload_writers.append(jni_onload_writer)

    def cache_method(method):
        name = method['camel_name']
        if name == '<init>':
            name = '_constructor'
        w.declare('static jmethodID', CACHE_METHOD(current_class['name'], name))
        def jni_onload_writer():
            w.lval(CACHE_METHOD(current_class['name'], name))
            w.env('GetMethodID', CACHE_CLASS(current_class['name']), quot(method['camel_name']),
                quot(w.str_method_signature(method)))
            w.check_exception()
        jni_onload_writers.append(jni_onload_writer)

    def cache_field(java_name, signature, static = False):
        name_gen = (CACHE_STATIC_FIELD if static else CACHE_FIELD)
        w.declare('static jfieldID', name_gen(current_class['name'], java_name))
        def jni_onload_writer():
            w.lval(name_gen(current_class['name'], java_name))
            w.env('GetStaticFieldID' if static else 'GetFieldID',
                CACHE_CLASS(current_class['name']), quot(java_name), quot(signature))
            w.check_exception()
        jni_onload_writers.append(jni_onload_writer)

    # declare classes that should be cached

    cache_class('NativePointer', 'NativePointer')
    cache_field('pointer', 'J')
    cache_method(dict(
        camel_name='<init>',
        parameters = ['long'],
        types = dict(java='void')
    ))

    cache_class('ArrayList', 'java/util/ArrayList')
    cache_method(dict(
        camel_name = '<init>',
        parameters = [],
        types = dict(java = 'void')
    ))
    cache_method(dict(
        camel_name = 'add',
        parameters = [dict(java = 'java.lang.Object')],
        types = dict(java = 'boolean')
    ))

    cache_class('HashMap', 'java/util/HashMap')
    cache_method(dict(
        camel_name = '<init>',
        parameters = [],
        types = dict(java = 'void')
    ))
    cache_method(dict(
        camel_name = 'put',
        parameters = [
            dict(java = 'java.lang.Object'),
            dict(java = 'java.lang.Object')
        ],
        types = dict(java = 'java.lang.Object')
    ))

    for callback in namespace['callbacks'].values():
        cache_class(callback['name'], callback)
        cache_method(callback)

    for clazz in namespace['classes']:
        cache_class(clazz['name'], clazz)
        cache_field('nativeInstance', 'J')

        for signal in clazz['signals']:
            classname = '%s_%s' % (clazz['name'], signal['title_name'])
            cache_class(classname, clazz, signal['title_name'] + 'Listener')
            cache_method(signal)

    for enum in (e for e in enums if not e['bitfield']):
        cache_class(enum['name'], enum['name'])
        cache_field('value', 'I')

        for member in enum['members']:
            cache_field(member['name'], w.str_java_type(enum['name']), static = True)


    w.out(STATIC_HELPER_METHODS)

    w.set_return_type('jint')
    w.line('jint JNI_OnLoad(JavaVM* vm, void* reserved)')
    w.push()
    w.declare('JNIEnv*', 'env')
    w.line()
    w.lval('class_cache_table')
    w.call('g_hash_table_new', 'g_str_hash', 'g_str_equal')
    w.line()
    w.lval('jvm')
    w.rval('vm')
    w.lval('env')
    w.call('get_jni_env')

    [writer() for writer in jni_onload_writers]
    w.line()
    w.ret(JNI_VERSION)
    w.line()


 ######   ##        #######  ########     ###    ##
##    ##  ##       ##     ## ##     ##   ## ##   ##
##        ##       ##     ## ##     ##  ##   ##  ##
##   #### ##       ##     ## ########  ##     ## ##
##    ##  ##       ##     ## ##     ## ######### ##
##    ##  ##       ##     ## ##     ## ##     ## ##
 ######   ########  #######  ########  ##     ## ########

    # w.line(STATIC_C_IMPLEMENTATIONS(**w.__dict__))

    # enum accessors
    for enum in (e for e in enums if not e['bitfield']):
        w.setClassname(enum['name'])

        name = TEMPL_TO_JAVA_ENUM(**enum)
        w.set_return_type('jobject')
        w.line('static jobject %s(JNIEnv* env, %s value)' % (name, enum['c_name']))
        w.push()
        w.line('jfieldID fieldId;')

        w.line()
        w.line('switch (value)', push = True)

        for member in enum['members']:
            w.case(format(member['c_name']))
            w.lval('fieldId')
            w.rval(CACHE_STATIC_FIELD(enum['name'], member['name']))
            w.state('break')

        w.pop()
        w.line()
        w.g_assert()
        w.ret()
        w.env('GetStaticObjectField', CACHE_CLASS(enum['name']), 'fieldId')
        w.check_exception()

        w.pop()
        w.line()


    for callback in namespace['callbacks'].values():
        void = callback['types']['c'] == 'void'

        # callback handler
        w.set_return_type('void')
        w.line('static {return_type} {name}({parameters})'.format(
            return_type = callback['types']['c'],
            name = 'callback_{}'.format(callback['name']),
            parameters = ', '.join(
                w.str_gobject_type(p) + ' ' + p['camel_name'] for p in callback['parameters']
            )
        ))
        w.push()

        parameters = [p for p in callback['parameters'] if p['c_name'] != 'user_data']

        # declare
        w.declare('JNIEnv*', 'env')
        w.declare('UserData*', 'data')
        map(w.jni_declare, parameters)
        if not void:
            w.c_return_declare(callback)
        w.line()

        # convert c arguments to jni
        w.lval('env')
        w.call('get_jni_env')
        w.lval('data')
        w.cast('UserData*')
        w.rval('userData')
        for parameter in parameters:
            w.c_to_jni(parameter)

        # cleanup c arguments
        map(w.cleanup_c, parameters)
        w.line()

        # call listener with jni arguments
        if not void:
            w.lval('jResult')
        call_name = 'Call{}Method'.format(w.str_jni_call_name(callback))
        w.env(call_name, 'data->self', CACHE_METHOD(callback['name'], callback['camel_name']), *map(w.str_jni_name, parameters))
        w.check_exception()

        w.call('user_data_destroy', 'data')

        # convert result if there is one
        if not void:
            w.line()
            w.return_c_result(callback)
        else:
            w.pop()
        w.line()


    w.setClassname(namespace['name'])
    for function in namespace['functions']:
        # break
        void = function['types']['c'] == 'void'

        w.method(function, static = True)

        # declarations
        map(w.c_declare, function['parameters'])
        if not void:
            w.jni_return_declare(function)
        if not void or function['parameters']:
            w.line()

        # jni to c
        map(w.jni_to_c, function['parameters'])
        if function['parameters']:
            w.line()

        # call
        if not void:
            w.lval('result')
        w.call(function['c_name'], *map(w.str_c_name, function['parameters']))

        # c to jni
        if not void:
            w.line()
            w.return_jni_result(function)
        else:
            w.pop()
        w.line()


 ######  ##          ###     ######   ######
##    ## ##         ## ##   ##    ## ##    ##
##       ##        ##   ##  ##       ##
##       ##       ##     ##  ######   ######
##       ##       #########       ##       ##
##    ## ##       ##     ## ##    ## ##    ##
 ######  ######## ##     ##  ######   ######


    # classes
    for clazz in namespace['classes']:
        # break
        w.setClassname(clazz['name'])

        # constructor
        if clazz['constructor']:
            w.comment('constructor')

            constructor = clazz['constructor']

            w.jni_function(
                name = constructor['camel_name'],
                parameters = constructor['parameters']
            )

            w.declare(constructor['types']['c'], 'self')
            map(w.c_declare, constructor['parameters'])
            w.declare('UserData*', 'data')
            w.line()

            if constructor['parameters']:
                map(w.jni_to_c, constructor['parameters'])
                w.line()

            w.lval('self')
            w.call(constructor['c_name'], *map(w.str_c_name, constructor['parameters']))
            w.line()

            w.lval('data')
            w.call('user_data_create', 'jself')
            w.g_assert()
            w.call('g_object_set_data', 'G_OBJECT(self)', quot('java_instance'), 'data')
            w.line()
            w.env('SetLongField', 'data->self', CACHE_FIELD(clazz['name'], 'nativeInstance'), '(jlong) self')
            w.check_exception()

            map(w.cleanup_jni, constructor['parameters'])

            w.pop()
            w.line()

        # methods
        if clazz['methods']:
            w.comment('methods')

        for method in clazz['methods']:
            void = method['types']['c'] == 'void'

            w.method(method)
            w.declare_self()
            map(w.c_declare, method['parameters'])
            if not void:
                w.jni_return_declare(method)

            w.line()
            w.get_self()

            if method.get('parameters'):
                map(w.jni_to_c, method['parameters'])

                w.line()
                map(w.cleanup_jni, method['parameters'])

            w.line()
            if not void:
                w.lval('result')
            w.call(method['c_name'], *map(w.str_c_name, [clazz] + method['parameters']))

            if method.get('parameters'):
                w.line()
                map(w.cleanup_c, method['parameters'])
            w.g_object_unref('self')

            if not void:
                w.line()
                w.return_jni_result(method)
            else:
                w.pop()
            w.line()

        for function in clazz['functions']:
            void = function['types']['c'] == 'void'

            w.method(function)
            map(w.c_declare, function['parameters'])
            if not void:
                w.jni_return_declare(function)
            if not void or function['parameters']:
                w.line()

            if function.get('parameters'):
                map(w.jni_to_c, function['parameters'])

                w.line()
                map(w.cleanup_jni, function['parameters'])
                w.line()

            if not void:
                w.lval('result')
            w.call(function['c_name'], *map(w.str_c_name, function['parameters']))

            if function.get('parameters'):
                w.line()
                map(w.cleanup_c, function['parameters'])

            if not void:
                w.line()
                w.return_jni_result(function)
            else:
                w.pop()
            w.line()

        #properties
        if clazz['properties']:
            w.comment('properties')
        for prop in clazz['properties']:
            if prop['writable']:
                w.jni_function(name = 'set' + prop['title_name'], parameters = [prop])
                w.declare_self()
                w.c_declare(prop)
                w.line()

                w.get_self()
                w.jni_to_c(prop)
                w.call('g_object_set', 'self', '"{c_name}"'.format(**prop), prop['camel_name'], 'NULL')
                w.cleanup_jni(prop)
                w.g_object_unref('self')
                w.pop()
                w.line()

            if prop['readable']:
                w.jni_function(return_value = prop, name = 'get' + prop['title_name'])
                w.declare_self()
                w.jni_declare(prop)
                w.c_declare(prop)
                w.line()

                w.get_self()
                w.call('g_object_get', 'self', '"{c_name}"'.format(**prop), '&{camel_name}'.format(**prop), 'NULL')
                w.c_to_jni(prop)

                w.line()
                w.cleanup_c(prop)
                w.g_object_unref('self')
                w.ret(w.str_jni_name(prop))
                w.line()

        for signal in clazz['signals']:
            void = signal['types']['c'] == 'void'
            handler_name = 'signal_{}_{}'.format(clazz['name'], signal['camel_name'])
            listener_param = dict(
                title_name = 'Listener',
                types = dict(jni = 'jobject')
            )

            w.set_return_type(signal['types']['c'])
            # signal handler
            w.line('static {return_type} {name}({parameters})'.format(
                return_type = signal['types']['c'],
                name = handler_name,
                parameters = ''.join([
                    '{}* {},'.format(clazz['c_name'], 'self'),
                    w.str_linebreak() if signal['parameters'] else ' ',
                    ', '.join(['{} {}'.format(w.str_gobject_type(p), p['camel_name'])
                               for p in signal['parameters']] + ['UserData* data']),
                ])
            ))
            w.push()

            # declare
            w.declare('JNIEnv*', 'env')
            for parameter in signal['parameters']:
                w.jni_declare(parameter)
            if not void:
                w.c_return_declare(signal)
            w.line()

            w.cast('void')
            w.rval('self')
            w.line()

            # convert c arguments to jni
            w.lval('env')
            w.call('get_jni_env')
            for parameter in signal['parameters']:
                w.c_to_jni(parameter)

            # cleanup c arguments
            map(w.cleanup_c, signal['parameters'])
            w.line()

            # call listener with jni arguments
            if not void:
                w.lval('jResult')
            call_name = 'Call{}Method'.format(w.str_jni_call_name(signal))
            w.env(call_name, 'data->self', CACHE_METHOD('{[name]}_{[title_name]}'.format(clazz, signal), signal['camel_name']), *map(w.str_jni_name, signal['parameters']))
            w.check_exception()

            # convert result if there is one
            if not void:
                w.line()
                w.return_c_result(signal)
            else:
                w.pop()
            w.line()


            # jni implementation
            w.jni_function(name = 'add' + signal['title_name'] + 'Listener', parameters = [listener_param])
            w.declare_self()
            w.declare('gulong', 'handler_id')
            w.declare('UserData*', 'data')
            w.line()

            w.get_self()
            w.line()

            w.lval('data')
            w.call('user_data_create', w.str_jni_name(listener_param))
            w.lval('handler_id')
            w.call('g_signal_connect_data', 'G_OBJECT(self)', quot(signal['c_name']),
                'G_CALLBACK({})'.format(handler_name), 'data', 'user_data_closure_notify', '0')
            w.cast('void')
            w.rval('handler_id')
            w.pop()
            w.line()

    w.pop()



      ##  ######  ######## ########
      ## ##    ##    ##    ##     ##
      ## ##          ##    ##     ##
      ##  ######     ##    ########
##    ##       ##    ##    ##   ##
##    ## ##    ##    ##    ##    ##
 ######   ######     ##    ##     ##

STATIC_JAVA_IMPLEMENTATIONS = {
    'NativePointer': """\
public class NativePointer {
    final long pointer;

    private NativePointer(long pointer) {
        this.pointer = pointer;
    }
}
"""
}

def javify_callback(callback, package):
    w = JavaWriter(package + '.' + callback['title_name'])

    w.class_declaration(typename = 'interface')

    w.method(callback, native = False)

    w.pop()


def javify_class(clazz, package):
    w = JavaWriter(package + '.' + clazz['name'])

    w.class_declaration(extends = clazz['parent'])

    # debug tag
    w.line('public static final String TAG = "' + clazz['name'] + '";')

    # constructor
    constructor = clazz.get('constructor')
    if constructor:
        w.line()
        w.constructor(constructor['parameters'])
        w.call('nativeConstructor', *map(w.argument, constructor['parameters']))
        w.pop()
        w.line()

        w.method(constructor, visibility = 'private')

    w.line()
    w.constructor([dict(
        types = dict(java = 'NativePointer'),
        camel_name = 'nativePointer'
    )], visibility = None)

    if clazz.get('parent'):
        w.call('super', 'nativePointer')
    else:
        w.lval('nativeInstance')
        w.rval('nativePointer.pointer')
    w.pop()

    if not constructor or constructor.get('parameters'):
        w.line()
        w.constructor(visibility = None)
        w.pop()

    if not clazz.get('parent'):
        w.line()
        w.state('long nativeInstance')

    # methods
    for method in clazz['methods']:
        w.line()
        w.method(method)

    # functions
    for function in clazz['functions']:
        w.line()
        w.method(function, static = True)

    # properties
    for prop in clazz['properties']:
        w.line()
        w.method(prop, name = 'get' + prop['title_name'])

        if prop['writable']:
            w.method(name = 'set' + prop['title_name'], parameters = [prop])

    # signals
    for signal in clazz['signals']:
        interface = signal['title_name'] + 'Listener'
        w.line()
        w.method(name = 'add' + interface, parameters = [dict(
                camel_name = 'listener',
                types = dict(java = interface)
            )])
        w.line()
        w.class_declaration(static = True, typename = 'interface', name = interface)
        # w.outI('public static interface ' + interface)
        w.method(signal, native = False)
        w.pop()

    # end class
    w.pop()


def javify_bitfield(enum, package):
    w = JavaWriter(package + '.' + enum['name'])

    w.class_declaration(typename = 'class')

    # members
    for member in enum['members']:
        w.state('public static final int {name} = {value}'.format(**member))
    w.pop()


def javify_enum(enum, package):
    w = JavaWriter(package + '.' + enum['name'])

    w.class_declaration(typename = 'enum')

    # members
    w.indent()
    for member in enum['members'][0:-1]:
        w.line('{name}({value}),'.format(**member))
    w.state('{name}({value})'.format(**enum['members'][-1]))
    w.line()

    # value
    w.state('public final int value')
    w.line()

    w.constructor(visibility = 'private', parameters = [dict(
        types = dict(java ='int'),
        camel_name = 'value',
        c_name = '_value',
    )])
    w.state('this.value = value')
    w.pop()

    w.pop()


def javify_functions(namespace, functions, package):
    w = JavaWriter(package + '.' + namespace['name'])

    w.class_declaration()

    for function in functions:
        w.method(function, static = True)
        w.line()

    w.pop()


def javify_namespace(namespace):
    package = PACKAGE_ROOT + '.' + namespace['symbol_prefix']

    for callback in namespace['callbacks'].values():
        javify_callback(callback, package)

    for clazz in namespace['classes']:
        javify_class(clazz, package)

    for name, enum in namespace['enums'].items():
        if enum['bitfield']:
            javify_bitfield(enum, package)
        else:
            javify_enum(enum, package)

    javify_functions(namespace, namespace['functions'], package)

    for name, source in STATIC_JAVA_IMPLEMENTATIONS.items():
        w = JavaWriter(package + '.' + name)
        w.out(source)


########     ###    ########   ######  ########
##     ##   ## ##   ##     ## ##    ## ##
##     ##  ##   ##  ##     ## ##       ##
########  ##     ## ########   ######  ######
##        ######### ##   ##         ## ##
##        ##     ## ##    ##  ##    ## ##
##        ##     ## ##     ##  ######  ########


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


 ######    #######
##    ##  ##     ##
##        ##     ##
##   #### ##     ##
##    ##  ##     ##
##    ##  ##     ##
 ######    #######


tree = ET.parse(args.gir)
root = tree.getroot()

for elem in root:
    if elem.tag == TAG_NAMESPACE:
        namespace = parse_namespace(elem)
        javify_namespace(namespace)
        cify_namespace(namespace)

