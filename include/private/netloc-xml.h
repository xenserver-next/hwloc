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

#ifndef _NETLOC_XML_PRIVATE_H_
#define _NETLOC_XML_PRIVATE_H_

#include <private/netloc.h>
#include <netloc.h>

/**
 * Load the netloc topology as described in the xml file pointed by \ref path.
 *
 * On error, *ptopology is not set.
 *
 * \param path A valid path to the XML file describing the requested topology
 * \param ptopology A valid referece to the topology handle
 *
 * \returns NETLOC_SUCCESS on success
 * \returns NETLOC_ERROR_NOENT if \ref path is not valid
 * \returns NETLOC_ERROR on error
 */
int netloc_topology_xml_load(char *path, netloc_topology_t **ptopology);


#if defined(HWLOC_HAVE_LIBXML2)
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

int netloc_topology_libxml_load(char *path, netloc_topology_t **ptopology);

#else

int netloc_topology_nolibxml_load(char *path, netloc_topology_t **ptopology);

#endif /* defined(HWLOC_HAVE_LIBXML2) */

#endif /* _NETLOC_XML_PRIVATE_H_ */
