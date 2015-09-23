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


import pprint
from collections import defaultdict


class TypeRegistry:
    def __init__(self):
        self.types=[]
        self.by_gir_type = defaultdict(set)
        self.by_c_type = defaultdict(set)
        self.array_by_gir_type = defaultdict(set)
        self.array_by_c_type = defaultdict(set)
        self.enum_aliases = {}

    def _register(self, typ):
        self.types.append(typ)
        if typ.is_array:
            if typ.gir_type:
                self.array_by_gir_type[typ.gir_type] |= set([typ])
            if typ.c_type:
                self.array_by_c_type[typ.c_type] |= set([typ])
        else:
            if typ.gir_type:
                self.by_gir_type[typ.gir_type] |= set([typ])
            if typ.c_type:
                self.by_c_type[typ.c_type] |= set([typ])

    def register(self, typ):
        try:
            [self._register(t) for t in typ]
        except TypeError:
            self._register(typ)

    def register_enum_aliases(self, aliases):
        self.enum_aliases.update(aliases)

    def lookup(self, gir_type = None, c_type = None, is_array=False):
        girs = None;
        cs = None;
        if is_array:
            girs = self.array_by_gir_type[gir_type]
            cs = self.array_by_c_type[c_type]
        else:
            girs = self.by_gir_type[gir_type]
            cs = self.by_c_type[c_type]
        if not girs and len(cs) == 1:
            return next(iter(cs))
        elif not cs and len(girs) == 1:
            return next(iter(girs))
        result = girs & cs
        if len(result) == 1:
            return next(iter(result))
        enum_alias = self.enum_aliases.get(gir_type)
        if enum_alias is not None:
            return self.lookup(enum_alias, c_type)
        if len(girs):
            return max(iter(girs))
        raise LookupError("type lookup failed (gir_type=%s, c_type=%s)" % (gir_type, c_type))


class TypeTransform(object):
    def __init__(self,
            declarations = None,
            conversion = None,
            cleanup = None,
        ):
        self.declarations = declarations or []
        self.conversion = conversion or []
        self.cleanup = cleanup or []


class GirMetaType(object):
    gir_type = None
    java_type = None
    jni_type = None
    c_type = None
    java_signature = None
    is_container = False
    is_array = False
    is_length_param = False
    has_local_ref = False

    def __new__(cls):
        return type(cls.__name__, (cls,), {
            '__new__': object.__new__,
        })

    def __init__(self, name, transfer_ownership=False, allow_none=False):
        self.name = name
        self.transfer_ownership = transfer_ownership
        self.allow_none = allow_none
        if name:
            self.c_name = 'c_' + name
            self.jni_name = 'j_' + name

    @property
    def object_type(self):
        return self.java_type

    @property
    def object_full_type(self):
        return self.java_full_class

    def transform_to_c(self):
        raise AssertionError(self.__class__.__name__ + '.transform_to_c is not implemented')

    def transform_to_jni(self):
        raise AssertionError(self.__class__.__name__ + '.transform_to_jni is not implemented')
