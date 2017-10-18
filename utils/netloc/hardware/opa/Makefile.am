# Copyright 2017 Inria.  All rights reserved.
#
# See COPYING in top-level directory.
#
# $HEADER$
#
bin_SCRIPTS = \
        netloc_opa_parse_snapshot.py \
        netloc_opa_parse_topology.py \
        netloc_opa_gather_raw.sh
CLEANFILES = $(bin_SCRIPTS)

install-exec-hook:
	chmod +x $(DESTDIR)$(bindir)/netloc_opa_parse_snapshot.py \
		$(DESTDIR)$(bindir)/netloc_opa_parse_topology.py \
		$(DESTDIR)$(bindir)/netloc_opa_gather_raw.sh
