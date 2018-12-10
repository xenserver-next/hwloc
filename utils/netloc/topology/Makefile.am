# Copyright © 2017 Inria.  All rights reserved.
# See COPYING in top-level directory.

noinst_LTLIBRARIES = \
	libnetloc_topology.la

AM_CPPFLAGS = \
        -I$(top_builddir)/include \
        -I$(top_srcdir)/include

libnetloc_topology_la_SOURCES = \
    architecture.c \
    tree.c \
    utils_to_netloc.c

libnetloc_topology_la_LIBADD = \
	$(top_builddir)/netloc/libnetloc.la

bin_PROGRAMS = netloc_get_coords
netloc_get_coords_SOURCES = netloc_get_coords.c
netloc_get_coords_LDADD = $(top_builddir)/netloc/libnetloc.la
