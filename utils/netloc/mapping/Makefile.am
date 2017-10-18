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

if BUILD_MPITOOLS
if BUILD_NETLOCSCOTCH
bin_PROGRAMS = \
        netloc_mpi_rank_file
endif
endif

if BUILD_NETLOCSCOTCH
netloc_mpi_rank_file_SOURCES = netloc_mpi_rank_file.c
netloc_mpi_rank_file_LDADD = $(LDADD) \
        $(top_builddir)/netloc/libnetlocscotch.la \
        -lscotch -lscotcherr -lm -lpthread
endif
