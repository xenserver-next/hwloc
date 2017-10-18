# Copyright © 2017 Inria.  All rights reserved.
#
# See COPYING in top-level directory.
#
# $HEADER$
#
AM_CPPFLAGS = \
        -I$(top_builddir)/include \
        -I$(top_srcdir)/include

bin_PROGRAMS = netloc_cray_find_hosts netloc_cray_discover_topo

netloc_cray_find_hosts_SOURCES = netloc_cray_find_hosts.c

netloc_cray_find_hosts_LDADD = \
	$(top_builddir)/netloc/libnetloc.la

netloc_cray_discover_topo_SOURCES = netloc_cray_discover_topo.c
