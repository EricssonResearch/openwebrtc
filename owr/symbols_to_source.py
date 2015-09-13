"""
  Copyright (c) 2014-2015, Ericsson AB. All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or other
  materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
  OF SUCH DAMAGE.
"""


def symbols_to_source(infile_name, outfile_name, platform):
    with open(infile_name) as infile, open(outfile_name, "w") as outfile:
        symbols = []
        for line in infile:
            split = line.split(' if ')
            if not split[1:] or platform in [p.strip() for p in split[1].split(' or ')]:
                symbols.append(split[0].strip())
        outfile.write("#include <stdlib.h>\n")
        outfile.write("#include <stdint.h>\n")
        outfile.writelines(["extern void *%s;\n" % symbol for symbol in symbols])
        outfile.write("\nvoid *_%s(void)\n{\n    " % outfile_name.split(".")[0])
        outfile.write("uintptr_t ret = 0;\n    ")
        lines = ["ret |= (uintptr_t) %s" % symbol for symbol in symbols]
        outfile.writelines(";\n    ".join(lines))
        outfile.write(";\n    ")
        outfile.write("return (void *) ret;\n}\n\n")


if __name__ == "__main__":
    import sys
    if (len(sys.argv) < 4):
        print "Usage: %s <infile> <outfile> <platform>" % sys.argv[0]
        exit(1)
    symbols_to_source(sys.argv[1], sys.argv[2], sys.argv[3])
