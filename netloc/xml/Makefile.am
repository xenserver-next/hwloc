#
# Copyright © 2017-2018 Inria.  All rights reserved.
#
# See COPYING in top-level directory.
#
# $HEADER$
#

noinst_LTLIBRARIES = \
	libnetloc_hardware.la

AM_CPPFLAGS = \
        -I$(top_builddir)/include \
        -I$(top_srcdir)/include

sources = \
	network.c \
	ib.c

libnetloc_hardware_la_SOURCES = $(sources)
libnetloc_hardware_la_LIBADD = \
	$(top_builddir)/hwloc/libhwloc.la \
	$(top_builddir)/netloc/topos/explicit/libnetloc_topos_explicit.la
