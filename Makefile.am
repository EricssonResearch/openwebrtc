## Process this file with automake to produce Makefile.in

SUBDIRS = transport local owr bridge docs

if OWR_DEBUG
DEBUG_CFLAGS = "-g"
else
DEBUG_CFLAGS =
endif
export DEBUG_CFLAGS

openwebrtcdocdir = ${prefix}/share/doc/libopenwebrtc
openwebrtcdoc_DATA = \
    COPYING \
    ChangeLog

EXTRA_DIST = $(openwebrtcdoc_DATA)

GITIGNOREFILES = openwebrtc-deps-* build out .build_successful_tag_file.txt

DISTCLEANFILES = aclocal.m4 config.guess config.h.in config.sub \
    depcomp gtk-doc.make m4 install-sh ltmain.sh missing

# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(openwebrtcdocdir)

-include $(top_srcdir)/git.mk
