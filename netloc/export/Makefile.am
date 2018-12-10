# Copyright © 2014 Cisco Systems, Inc.  All rights reserved.
# Copyright © 2014 University of Wisconsin-La Crosse.
#                         All rights reserved.
# Copyright © 2016-2018 Inria.  All rights reserved.
#
# See COPYING in top-level directory.
#
# $HEADER$
#

if BUILD_NETLOC

AM_CFLAGS = $(NETLOC_CFLAGS) $(HWLOC_CFLAGS)
AM_CPPFLAGS = $(NETLOC_CPPFLAGS) $(HWLOC_CPPFLAGS)

scotchsources = \
        scotch.c

if BUILD_NETLOCSCOTCH
# If we're in standalone mode, build the installable library.
# Otherwise, build the embedded library.

if HWLOC_BUILD_STANDALONE
lib_LTLIBRARIES = libnetlocscotch.la
else
noinst_LTLIBRARIES = libnetlocscotch_embedded.la
endif

# Installable library

libnetlocscotch_la_SOURCES = $(scotchsources)
libnetlocscotch_la_LDFLAGS = -version-info $(libnetloc_so_version)
libnetlocscotch_la_LIBADD = \
        $(top_builddir)/hwloc/libhwloc.la \
        $(top_builddir)/netloc/libnetloc.la

# Embedded library (note the lack of a .so version number -- that
# intentionally only appears in the installable library).  Also note
# the lack of _LDFLAGS -- all libs are added by the upper layer (via
# HWLOC_EMBEDDED_LIBS).

libnetlocscotch_embedded_la_SOURCES = $(scotchsources)
libnetlocscotch_embedded_la_LIBADD = \
        $(HWLOC_top_builddir)/hwloc/libhwloc_embedded.la \
        $(HWLOC_top_builddir)/netloc/libnetloc_embedded.la

else !BUILD_NETLOCSCOTCH
EXTRA_DIST = $(scotchsources)
endif !BUILD_NETLOCSCOTCH
endif BUILD_NETLOC
