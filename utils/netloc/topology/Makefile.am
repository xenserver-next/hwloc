# Copyright © 2017 Inria.  All rights reserved.
# See COPYING in top-level directory.

noinst_LTLIBRARIES = \
	libnetloc_topology.la

AM_CPPFLAGS = \
        -I$(top_builddir)/include \
        -I$(top_srcdir)/include

libnetloc_topology_la_SOURCES = \
    architecture.c \
    explicit.c \
    tree.c \
    utils_to_netloc.c


libnetloc_topology_la_LIBADD = \
	$(top_builddir)/netloc/libnetloc.la

