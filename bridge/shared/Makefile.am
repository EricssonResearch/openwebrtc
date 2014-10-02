## Process this file with automake to produce Makefile.in

BUILT_SOURCES = \
    wbjsonrpc.js.h

%.js_: %.js
	cp $< $@

%.js.h: %.js_
	xxd -i $< > $@

-include $(top_srcdir)/git.mk
