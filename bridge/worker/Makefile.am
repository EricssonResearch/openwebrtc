## Process this file with automake to produce Makefile.in

BUILT_SOURCES = \
    bridgeserver.js.h \
    peerhandler.js.h

%.js_: %.js
	cp $< $@

%.js.h: %.js_
	xxd -i $< > $@

-include $(top_srcdir)/git.mk
