/*
 * Copyright © 2019 Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */
#include <scotch.h>

#include <netloc.h>
#include <private/netloc.h>

int netlocscotch_export_topology(netloc_machine_t *machine,
      netloc_filter_t *filter,
      SCOTCH_Arch *arch, SCOTCH_Arch *subarch);

