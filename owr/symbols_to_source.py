"""
  Copyright (c) 2014, Ericsson AB. All rights reserved.

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

def symbols_to_source(infile_name, outfile_name):
    with open(infile_name) as infile, open(outfile_name, "w") as outfile:
        symbols = [line.strip() for line in infile]
        outfile.writelines(["extern void *%s;\n" % symbol for symbol in symbols])
        outfile.write("\nvoid _%s()\n{\n" % outfile_name.split(".")[0])
        outfile.write("    void *symbols[%i];\n    " % len(symbols))
        lines = ["symbols[%i] = %s" % (i, symbol) for i, symbol in enumerate(symbols)]
        outfile.writelines(";\n    ".join(lines))
        outfile.write(";\n    (void)symbols;\n}\n\n")


if __name__ == "__main__":
    import sys
    if (len(sys.argv) < 3):
        print "Usage: %s <infile> <outfile>" % sys.argv[0]
        exit(1)
    symbols_to_source(sys.argv[1], sys.argv[2])
