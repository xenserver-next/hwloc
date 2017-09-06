/* -*- encoding: utf-8 -*- */
/*
 * Copyright © 2017      Inria.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 * See COPYING in top-level directory.
 *
 * $HEADER$
 */

#define _GNU_SOURCE	   /* See feature_test_macros(7) */
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <libgen.h>

#include <private/autogen/config.h>
#include <private/netloc.h>
#include <private/netloc-xml.h>

#if defined(HWLOC_HAVE_LIBXML2)

int netloc_topology_xml_load(char *path, netloc_topology_t **ptopology)
{
    return netloc_topology_libxml_load(path, ptopology);
}

#else

int netloc_topology_xml_load(char *path, netloc_topology_t **ptopology)
{
    return netloc_topology_nolibxml_load(path, ptopology);
}

#endif
