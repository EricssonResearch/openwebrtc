## Process this file with automake to produce Makefile.in

BUILT_SOURCES = \
    domutils.js.h \
    sdp.js.h \
    webrtc.js.h

%.js_: %.js
	cp $< $@

%.js.h: %.js_
	xxd -i $< > $@

-include $(top_srcdir)/git.mk
