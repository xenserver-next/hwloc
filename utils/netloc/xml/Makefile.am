# Copyright © 2017      Inria.  All rights reserved.
#
# See COPYING in top-level directory.
#
# $HEADER$
#

noinst_LTLIBRARIES = libnetloc_xml_write.la

AM_CPPFLAGS = \
        -I$(top_builddir)/include \
        -I$(top_srcdir)/include

libnetloc_xml_write_la_SOURCES = \
        netloc_xml_write.c

libnetloc_xml_write_la_LIBADD = \
	$(top_builddir)/netloc/libnetloc.la

if NETLOC_HAVE_LIBXML2
AM_CPPFLAGS += \
	$(HWLOC_LIBXML2_CFLAGS)

libnetloc_xml_write_la_SOURCES += \
	netloc_libxml_write.c

libnetloc_xml_write_la_LIBADD += \
	$(HWLOC_LIBXML2_LIBS)
else
libnetloc_xml_write_la_SOURCES += \
	netloc_nolibxml_write.c
endif
