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

#if defined(HWLOC_HAVE_LIBXML2)
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

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

/**
 * Load the netloc node as described in the xml file, of which
 * \ref it_node is a valid pointer to a DOM element.
 *
 * The returned element is to be added to the \ref
 * netloc_topology_t. If the \ref netloc_node_t returned has nsubnodes
 * > 0, this means that the node is virtual. The subnodes (contained
 * in the node->subnodes array) have to be added before the virtual
 * node to ease the hashtable liberation.
 *
 * \param it_node A valid XML DOM node pointing to the proper <node> tag
 * \param hwlocpath The path to the directory containing the hwloc topology files
 * \param hwloc_topos A valid pointer to the hwloc_topos field in \ref netloc_topology_t
 *
 * Returns
 *   A newly allocated and initialized pointer to the node information.
 */
netloc_node_t * netloc_node_xml_load(xmlNode *it_node, char *hwlocpath,
                                     netloc_hwloc_topology_t **hwloc_topos);

/**
 * Load the netloc edge as described in the xml file, of which
 * \ref it_edge is a valid pointer to a DOM element.
 *
 * The returned element is to be added to the corresponding \ref
 * netloc_node_t. The node has to be already part of the \ref
 * netloc_topology_t
 *
 * \param it_edge A valid XML DOM node pointing to the proper <connexion> tag
 * \param topology A valid pointer to the current topology being loaded
 * \param partition A valid pointer to the current partition being loaded
 *
 * Returns
 *   A newly allocated and initialized pointer to the edge information.
 */
netloc_edge_t *
netloc_edge_xml_load(xmlNode *it_edge, netloc_topology_t *topology,
                     netloc_partition_t *partition);

/**
 * Load the netloc physical link as described in the xml file, of which
 * \ref it_link is a valid pointer to a DOM element.
 *
 * The returned element is to be added to the \ref netloc_edge_t.
 *
 * \param it_link A valid XML DOM node pointing to the proper <link> tag
 * \param edge A valid pointer to the current edge being loaded
 * \param partition A valid pointer to the current partition being loaded
 *
 * Returns
 *   A newly allocated and initialized pointer to the physical link information.
 */
netloc_physical_link_t *
netloc_physical_link_xml_load(xmlNode *it_link, netloc_edge_t *edge,
                              netloc_partition_t *partition);

xmlDoc *netloc_xml_reader_init(char *path);

int netloc_xml_reader_clean_and_out(xmlDoc *doc);

#else
#warning No support available for netloc without libxml2

int netloc_topology_xml_load(char *path                    __netloc_attribute_unused,
                             netloc_topology_t **ptopology __netloc_attribute_unused);

netloc_node_t *
netloc_node_xml_load(void *it_node                         __netloc_attribute_unused,
                     char *hwlocpath                       __netloc_attribute_unused,
                     netloc_hwloc_topology_t **hwloc_topos __netloc_attribute_unused);

netloc_edge_t *
netloc_edge_xml_load(void *it_edge                 __netloc_attribute_unused,
                     netloc_topology_t *topology   __netloc_attribute_unused,
                     netloc_partition_t *partition __netloc_attribute_unused);

netloc_physical_link_t *
netloc_physical_link_xml_load(void *it_link                 __netloc_attribute_unused,
                              netloc_edge_t *edge           __netloc_attribute_unused,
                              netloc_partition_t *partition __netloc_attribute_unused);

void *netloc_xml_reader_init(char *path __netloc_attribute_unused);

int netloc_xml_reader_clean_and_out(void *doc __netloc_attribute_unused);

#endif /* defined(HWLOC_HAVE_LIBXML2) */

#endif /* _NETLOC_XML_PRIVATE_H_ */
