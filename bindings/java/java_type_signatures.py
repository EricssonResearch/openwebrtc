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


from config import PACKAGE_ROOT

PATH_BASE = PACKAGE_ROOT.replace('.', '/') + '/'

type_signatures = {
    'ArrayList': {
        '_path': 'java/util/ArrayList',
        '_constructor': '()V',
        'add': '(Ljava/lang/Object;)Z',
    },
    'EnumSet': {
        '_path': 'java/util/EnumSet',
        'add': '(Ljava/lang/Object;)Z',
        'noneOf': '(Ljava/lang/Class;)Ljava/util/EnumSet;',
    },
    'HashMap': {
        '_path': 'java/util/HashMap',
        '_constructor': '()V',
        'put': '(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;',
    },
    'Iterable': {
        '_path': 'java/lang/Iterable',
        'iterator': '()Ljava/util/Iterator;',
    },
    'Iterator': {
        '_path': 'java/util/Iterator',
        'hasNext': '()Z',
        'next': '()Ljava/lang/Object;',
    },
    'IllegalStateException': {
        '_path': 'java/lang/IllegalStateException',
        '_constructor': '(Ljava/lang/String;)V',
    },
    'NativeInstance': {
        '_path': PATH_BASE + 'NativeInstance',
        '_constructor': '(L' + PATH_BASE + 'NativePointer' + ';)V',
        'nativeInstance': 'J',
    },
    'NativePointer': {
        '_path': PATH_BASE + 'NativePointer',
        '_constructor': '(J)V',
    },
    'ValueEnum': {
        '_path': PATH_BASE + 'ValueEnum',
        '_constructor': '()V',
        'getValue': '()I',
    },
    'Boolean': {
        '_path': 'java/lang/Boolean',
        'valueOf': '(Z)Ljava/lang/Boolean;',
    },
    'Byte': {
        '_path': 'java/lang/Byte',
        'valueOf': '(B)Ljava/lang/Byte;',
    },
    'Character': {
        '_path': 'java/lang/Character',
        'valueOf': '(C)Ljava/lang/Character;',
    },
    'Short': {
        '_path': 'java/lang/Short',
        'valueOf': '(S)Ljava/lang/Short;',
    },
    'Integer': {
        '_path': 'java/lang/Integer',
        'valueOf': '(I)Ljava/lang/Integer;',
    },
    'Long': {
        '_path': 'java/lang/Long',
        'valueOf': '(J)Ljava/lang/Long;',
    },
    'Float': {
        '_path': 'java/lang/Float',
        'valueOf': '(F)Ljava/lang/Float;',
    },
    'Double': {
        '_path': 'java/lang/Double',
        'valueOf': '(D)Ljava/lang/Double;',
    },
}

