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
from itertools import chain, starmap


quot = '"{}"'.format


def semi(line):
    return line if line[-1] == ';' else line + ';'


def nosemi(line):
    return line[:-1] if line[-1] == ';' else line


def flatten(lst):
    if isinstance(lst, collections.Iterable) and not isinstance(lst, basestring):
        for sub in lst:
            for el in flatten(sub):
                yield el
    else:
        yield lst


def flatjoin(lst, st):
    return st.join(flatten(lst))


def prune_empty(*lst):
    if len(lst) == 1:
        return [el for el in lst[0] if el]
    return [el for el in lst if el]


def intersperse(lst, el):
    result = [el] * (len(lst) * 2 - 1)
    result[0::2] = lst
    return result


def chunks(lst, size):
    for i in xrange(0, len(lst), size):
        yield lst[i:i+size]


def add_to(self):
    def inject(cls):
        setattr(self, cls.__name__, cls)
        return cls
    return inject


class BaseGenerator():
    def __init__(self, default_line_prefix):
        @add_to(self)
        class Lines(object):
            def __init__(self, **kwargs):
                self.__dict__.update(**kwargs)

            def __iter__(self):
                return iter(self.body)

        @add_to(self)
        class Block(Lines):
            def __init__(self, **kwargs):
                self._start = '{'
                self._end = '}'
                self._line_prefix = default_line_prefix
                self.body = []
                super(Block, self).__init__(**kwargs)

            @property
            def start(self):
                return self._start

            @property
            def end(self):
                return self._end

            @property
            def line_prefix(self):
                return self._line_prefix

            def __iter__(self):
                def prefix(line):
                    return self.line_prefix + line if line else ''

                return chain(
                    flatten(self.start),
                    map(prefix, flatten(self.body)),
                    flatten(self.end)
                )

            def __str__(self):
                return '\n'.join(list(self)) # converting to list gives us better error messages


        @add_to(self)
        class MultiBlock(Lines):
            def __init__(self, **kwargs):
                self._line_prefix = default_line_prefix
                self.__dict__.update(**kwargs)

            @property
            def line_prefix(self):
                return self._line_prefix

            def __iter__(self):
                def prefix(count, lines):
                    def _prefix(line):
                        return self.line_prefix * count + line if len(line) else ''
                    return map(_prefix, flatten(lines))

                return starmap(prefix, self.body)


        @add_to(self)
        class IfElse(MultiBlock):
            def __init__(self, ifs=None, bodies=None, **kwargs):
                super(IfElse, self).__init__(**kwargs)
                self.ifs = ifs or []
                self.bodies = bodies or []

            @property
            def body(self):
                assert len(self.ifs) == len(self.bodies) or len(self.ifs) + 1 == len(self.bodies)
                ifs = ['if (%s) {' % cond for cond in self.ifs]
                ifs[1:] = ('} else ' + cond for cond in ifs[1:])
                ifs = [(0, cond) for cond in ifs]
                if len(self.bodies) > len(self.ifs):
                    ifs.append((0, '} else {'))
                bodies = [(1, body) for body in self.bodies]
                return sum([[x, y] for x, y in zip(ifs, bodies)], []) + [(0, '}')]


        @add_to(self)
        class Switch(MultiBlock):
            def __init__(self, name, cases=None, default=None, **kwargs):
                super(Switch, self).__init__(**kwargs)
                self.name = name
                self.cases = cases or []
                self.default = default

            @property
            def body(self):
                def make_case(case, body):
                    needs_scope = False
                    if isinstance(body, list):
                        for statement in body:
                            if isinstance(statement, Decl):
                                needs_scope = True
                    if needs_scope:
                        return [(0, 'case ' + case + ': {')] + [(1, b) for b in body] + [(1, 'break;')] + [(0, '}')]
                    else:
                        return [(0, 'case ' + case + ':')] + [(1, b) for b in body] + [(1, 'break;')]
                start = [(0, 'switch (' + nosemi(flatjoin(self.name, '')) + ') {')]
                end = [(0, '}')]
                default = []
                if self.default:
                    default = [(0, 'default:')] + [(1, b) for b in self.default]
                return start + sum(starmap(make_case, self.cases), []) + default + end


        class ConditionBlock(Block):
            def __init__(self, condition, *body, **kwargs):
                super(ConditionBlock, self).__init__(body = body, **kwargs)
                self.condition = condition

            @property
            def start(self):
                return self.variation + ' (%s) {' % nosemi(''.join(flatten(self.condition)))

        @add_to(self)
        class If(ConditionBlock):
            variation = 'if'

        @add_to(self)
        class While(ConditionBlock):
            variation = 'while'

        @add_to(self)
        class FunctionBlock(Block):
            @property
            def definition(self):
                return '{}({})'.format(
                    flatjoin([self.modifiers, self.return_type, self.name], ' '),
                    flatjoin(prune_empty(self.params), ', '),
                )

        @add_to(self)
        class Comment(Lines):
            def __init__(self, text):
                self.text = text

            def __iter__(self):
                yield '/* {} */'.format(self.text)

        @add_to(self)
        class Decl(Lines):
            def __init__(self, type, name):
                self.type = type
                self.name = name

            def __iter__(self):
                yield self.type + ' ' + self.name + ';'

        @add_to(self)
        class Assign(Lines):
            def __init__(self, lval, rval, cast = None, op = '='):
                self.lval = lval
                self.rval = rval
                self.cast = cast
                self.op = op

            def __iter__(self):
                yield semi(''.join(chain(
                    map(nosemi, flatten(self.lval)),
                    [' ' + self.op + ' ', '(' + self.cast + ') ' if self.cast is not None else ''],
                    flatten(self.rval)
                )))

        @add_to(self)
        class Call(Lines):
            def __init__(self, name, *args):
                self.name = name
                self.args = args

            def __iter__(self):
                yield semi('{name}({args})'.format(
                    name=self.name,
                    args=', '.join(map(nosemi, flatten(self.args))),
                ))

        @add_to(self)
        class Return(Lines):
            def __init__(self, value=None):
                self.value = value

            def __iter__(self):
                if self.value:
                    yield semi('return ' + flatjoin(self.value, ''))
                else:
                    yield semi('return')
