# Copyright © 2017      Inria.  All rights reserved.
#
# See COPYING in top-level directory.
#
# $HEADER$
#

noinst_LTLIBRARIES = \
    libutils_xml_write.la

AM_CPPFLAGS = \
        -I$(top_builddir)/include \
        -I$(top_srcdir)/include

if NETLOC_HAVE_LIBXML2
AM_CPPFLAGS += \
    $(HWLOC_LIBXML2_CFLAGS)
endif

libutils_xml_write_la_SOURCES = \
	utils_xml_write.c

libutils_xml_write_la_LIBADD = \
    $(top_builddir)/netloc/libnetloc.la

if NETLOC_HAVE_LIBXML2
libutils_xml_write_la_LIBADD += \
    $(HWLOC_LIBXML2_LIBS)
endif
