# Copyright © 2016-2017 Inria.  All rights reserved.
#
# See COPYING in top-level directory.
#
# $HEADER$
#

AM_CPPFLAGS = $(HWLOC_CPPFLAGS) \
        -I$(top_builddir)/include \
        -I$(top_srcdir)/include
AM_LDFLAGS = $(HWLOC_LDFLAGS)

LDADD = $(top_builddir)/netloc/libnetloc.la

if BUILD_NETLOCSCOTCH
bin_PROGRAMS = \
        netloc_scotch_export_arch
endif

netloc_scotch_export_arch_SOURCES = \
        netloc_scotch_export_arch.c
netloc_scotch_export_arch_LDADD = \
        $(top_builddir)/netloc/libnetlocscotch.la \
        -lscotch -lscotcherr -lm -lpthread
