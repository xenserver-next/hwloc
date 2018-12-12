#
# Copyright © 2017-2018 Inria.  All rights reserved.
#
# See COPYING in top-level directory.
#
# $HEADER$
#
noinst_LTLIBRARIES = \
	libnetloc_xml.la

# TODO use ${libxml2_CFLAGS}
AM_CPPFLAGS = \
	$(HWLOC_LIBXML2_CFLAGS) \
	-I$(top_builddir)/include/ \
	-I$(top_srcdir)/include/

sources = \
	read.c \
	save.c

libnetloc_xml_la_SOURCES = $(sources)
