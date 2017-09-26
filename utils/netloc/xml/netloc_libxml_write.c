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

/* 
 * This file provides general function and global variables to
 * generate XML topology files.
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <private/netloc.h>
#include <netloc.h>

#include <private/utils/netloc.h>
#include <netloc/uthash.h>
#include <netloc/utarray.h>
#include <dirent.h>
#include <libgen.h>

#ifdef HWLOC_HAVE_LIBXML2

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

static inline void set_xml_prop(xmlNodePtr pnode, const xmlChar *name, const char *value)
{
    xmlChar *buff = xmlCharStrdup(value);
    if(buff)
        xmlNewProp(pnode, name, buff);
    xmlFree(buff);
}

static inline void insert_xml_link(xmlNodePtr links_node, physical_link_t *link)
{
    xmlChar *buff = NULL;
    char *strBuff = NULL;
    int strBuffSize = 0;
    xmlNodePtr crt_node;
    /* Add current connexion description */
    buff = xmlCharStrdup(link->description);
    crt_node = xmlNewChild(links_node, NULL, BAD_CAST "link", buff);
    xmlFree(buff); buff = NULL;
    /* Set srcport */
    strBuffSize = asprintf(&strBuff, "%d", link->ports[0]);
    if (0 < strBuffSize)
        set_xml_prop(crt_node, BAD_CAST "srcport", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set destport */
    strBuffSize = asprintf(&strBuff, "%d", link->ports[1]);
    if (0 < strBuffSize)
        set_xml_prop(crt_node, BAD_CAST "destport", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set speed */
    set_xml_prop(crt_node, BAD_CAST "speed", link->speed);
    /* Set width */
    set_xml_prop(crt_node, BAD_CAST "width", link->width);
    /* Set bandwidth */
    strBuffSize = asprintf(&strBuff, "%f", link->gbits);
    if (0 < strBuffSize)
        set_xml_prop(crt_node, BAD_CAST "bandwidth", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set logical_id */
    strBuffSize = asprintf(&strBuff, "%llu", link->int_id);
    if (0 < strBuffSize)
        set_xml_prop(crt_node, BAD_CAST "logical_id", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set reverse physical_link->logical_id */
    if (link->other_link && 0 < asprintf(&strBuff, "%llu", link->other_link->int_id))
        set_xml_prop(crt_node, BAD_CAST "reverse_logical_id", strBuff);
    free(strBuff); strBuff = NULL;
}

static inline void insert_xml_edge(xmlNodePtr con_node, edge_t *edge, node_t *node)
{
    xmlChar *buff = NULL;
    char *strBuff = NULL;
    int strBuffSize = 0;
    xmlNodePtr crt_node, links_node;
    /* Set bandwidth */
    strBuffSize = asprintf(&strBuff, "%f", edge->total_gbits);
    if (0 < strBuffSize)
        set_xml_prop(con_node, BAD_CAST "bandwidth", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set nblinks */
    unsigned int num_links = utarray_len(edge->physical_link_idx);
    strBuffSize = asprintf(&strBuff, "%u", num_links);
    if (0 < strBuffSize)
        set_xml_prop(con_node, BAD_CAST "nblinks", strBuff);
    free(strBuff); strBuff = NULL;
    /* Add src */
    buff = xmlCharStrdup(node->physical_id);
    assert(buff);
    crt_node = xmlNewChild(con_node, NULL, BAD_CAST "src", buff);
    xmlFree(buff); buff = NULL;
    /* Add dest */
    buff = xmlCharStrdup(edge->dest);
    assert(buff);
    crt_node = xmlNewChild(con_node, NULL, BAD_CAST "dest", buff);
    xmlFree(buff); buff = NULL;
    if (edge_is_virtual(edge)) {
        xmlNodePtr subcons_node, subcon_node;
        /* Set virtual="yes" */
        xmlNewProp(con_node, BAD_CAST "virtual", BAD_CAST "yes");
        subcons_node = xmlNewChild(con_node, NULL, BAD_CAST "subconnexions", NULL);
        /* Set size */
        unsigned int num_subedges = utarray_len(edge->subedges);
        strBuffSize = asprintf(&strBuff, "%u", num_subedges);
        if (0 < strBuffSize)
            set_xml_prop(subcons_node, BAD_CAST "size", strBuff);
        free(strBuff); strBuff = NULL;
        /* Insert subedges */
        node_t *real_node = NULL;
        for (unsigned int se = 0; se < num_subedges; ++se) {
            edge_t *subedge = *(edge_t **)utarray_eltptr(edge->subedges, se);
            if (node_is_virtual(node))
                HASH_FIND_STR(node->subnodes, subedge->reverse_edge->dest, real_node);
            else
                HASH_FIND_STR(nodes, subedge->reverse_edge->dest, real_node);
            assert(real_node);
            subcon_node = xmlNewChild(subcons_node, NULL, BAD_CAST "connexion", NULL);
            insert_xml_edge(subcon_node, subedge, real_node);
        }
    } else {
        /* Add links */
        links_node = xmlNewChild(con_node, NULL, BAD_CAST "links", NULL);
        for (unsigned int l = 0; l < num_links; l++) {
            unsigned int link_idx = *(unsigned int *)
                utarray_eltptr(edge->physical_link_idx, l);
            physical_link_t *link = (physical_link_t *)
                utarray_eltptr(node->physical_links, link_idx);
            insert_xml_link(links_node, link);
        }
    }
}

static inline void insert_xml_node(xmlNodePtr crt_node, node_t *node, char *hwloc_path)
{
    xmlChar *buff = NULL;
    char *strBuff = NULL;
    int strBuffSize = 0;
    /* Set mac_addr */
    if (node->physical_id && 0 < strlen(node->physical_id)) {
        set_xml_prop(crt_node, BAD_CAST "mac_addr", node->physical_id);
    }
    /* Set type */
    set_xml_prop(crt_node, BAD_CAST "type", netloc_node_type_encode(node->type));
    if (node->hostname && 0 < strlen(node->hostname)) {
        /* Set name */
        set_xml_prop(crt_node, BAD_CAST "name", node->hostname);
        /* Set hwloc_path iif node is a host */
        if (NETLOC_NODE_TYPE_HOST == node->type) {
            FILE *fxml;
            strBuffSize = asprintf(&strBuff, "%s/%s.diff.xml",
                                   hwloc_path, node->hostname);
            if (!(fxml = fopen(strBuff, "r"))) {
                strBuffSize -= 8;
                strcpy(&strBuff[strBuffSize], "xml");
                if (!(fxml = fopen(strBuff, "r"))) {
                    fprintf(stderr, "Hwloc file absent: %s\n", strBuff);
                    strBuffSize = 0;
                } else
                    fclose(fxml);
            } else
                fclose(fxml);
            if (0 < strBuffSize) {
                set_xml_prop(crt_node, BAD_CAST "hwloc_file", 1+strrchr(strBuff, '/'));
            }
            free(strBuff); strBuff = NULL;
        }
    } else if (NETLOC_NODE_TYPE_HOST == node->type) {
        fprintf(stderr, "WARN: Host node with address %s has no hostname\n",
                node->physical_id);
    }
    /* Add description */
    if (node->description && 0 < strlen(node->description)) {
        buff = xmlCharStrdup(node->description);
        if(buff) {
            xmlNewChild(crt_node, NULL, BAD_CAST "description", buff);
        }
        xmlFree(buff);
    }
}

static inline int insert_extra(xmlNodePtr root_node, char *full_hwloc_path)
{
    char *strBuff;
    unsigned int part_size = 0, strBuffSize;
    xmlNodePtr part_node = NULL, crt_node = NULL, nodes_node = NULL, cons_node = NULL;
    part_node = xmlNewChild(root_node, NULL, BAD_CAST "partition", NULL);
    nodes_node = xmlNewChild(part_node, NULL, BAD_CAST "nodes", NULL);
    cons_node = xmlNewChild(part_node, NULL, BAD_CAST "connexions", NULL);
    /* Set name */
    xmlNewProp(part_node, BAD_CAST "name", BAD_CAST "/extra+structural/");
    /* Add nodes */
    node_t *node, *node_tmp;
    HASH_ITER(hh, nodes, node, node_tmp) {
        /* Check if node belongs to no partition */
        if (!node_belongs_to_a_partition(node)) {
            ++part_size;
            crt_node = xmlNewChild(nodes_node, NULL, BAD_CAST "node", NULL);
            insert_xml_node(crt_node, node, full_hwloc_path);
            if (node->subnodes) {
                /* VIRTUAL NODE */
                node_t *subnode, *subnode_tmp;
                /* Set virtual */
                xmlNewProp(crt_node, BAD_CAST "virtual", BAD_CAST "yes");
                /* Set size */
                strBuffSize = asprintf(&strBuff, "%u", HASH_COUNT(node->subnodes));
                if (0 < strBuffSize) {
                    set_xml_prop(crt_node, BAD_CAST "size", strBuff);
                }
                free(strBuff); strBuff = NULL;
                /* Add subnodes */
                xmlNodePtr subnodes_node = xmlNewChild(crt_node, NULL, BAD_CAST "subnodes", NULL);
                xmlNodePtr subnode_node;
                HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
                    subnode_node = xmlNewChild(subnodes_node, NULL, BAD_CAST "node", NULL);
                    insert_xml_node(subnode_node, subnode, full_hwloc_path);
                }
            }
        }
        /* Add links and connexions */
        edge_t *edge, *edge_tmp;
        HASH_ITER(hh, node->edges, edge, edge_tmp) {
            /* Check if edge belongs to no partition */
            if (edge_belongs_to_a_partition(edge))
                continue;
            crt_node = xmlNewChild(cons_node, NULL, BAD_CAST "connexion", NULL);
            insert_xml_edge(crt_node, edge, node);
        }
    }
    if (!nodes_node->children && !cons_node->children) {
        /* No extra needed: remove it from the output */
        xmlUnlinkNode(nodes_node); xmlFree(nodes_node);
        xmlUnlinkNode(cons_node);  xmlFree(cons_node);
        xmlUnlinkNode(part_node);  xmlFree(part_node);
    } else {
        /* Set size */
        strBuffSize = asprintf(&strBuff, "%u", part_size);
        if (0 < strBuffSize)
            set_xml_prop(part_node, BAD_CAST "size", strBuff);
        free(strBuff); strBuff = NULL;
    }
    return NETLOC_SUCCESS;
}

int netloc_libxml_write_xml_file(const char *subnet, const char *path, const char *hwlocpath,
                                 const netloc_network_type_t transportType)
{
    xmlDocPtr doc = NULL;        /* document pointer */
    xmlNodePtr root_node = NULL; /* root pointer */
    xmlChar *buff = NULL;
    char *strBuff = NULL, *full_hwloc_path = NULL;
    int strBuffSize = 0;

    LIBXML_TEST_VERSION;

    /*
     * Add topology definition tag
     */
    /* Creates a new document, a node and set it as a root node. */
    doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST "topology");
    xmlDocSetRootElement(doc, root_node);
    /* Set version */
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST NETLOC_STR_VERS(NETLOCFILE_VERSION_2_0));
    /* Set transport */
    set_xml_prop(root_node, BAD_CAST "transport", netloc_network_type_encode(transportType));
    /* Add subnet node */
    if (subnet && 0 < strlen(subnet)) {
        buff = xmlCharStrdup(subnet);
        if (buff)
            xmlNewChild(root_node, NULL, BAD_CAST "subnet", buff);
        xmlFree(buff); buff = NULL;
    }
    /* Add hwloc_path node */
    DIR* dir;
    if (hwlocpath && 0 < strlen(hwlocpath)) {
        if ('/' != hwlocpath[0]) {
            asprintf(&full_hwloc_path, "%s/%s", path, hwlocpath);
        } else {
            full_hwloc_path = strdup(hwlocpath);
        }
    }
    if (full_hwloc_path && 0 < strlen(full_hwloc_path) && (dir = opendir(full_hwloc_path))) {
        buff = xmlCharStrdup(hwlocpath);
        if (buff)
            xmlNewChild(root_node, NULL, BAD_CAST "hwloc_path", buff);
        xmlFree(buff); buff = NULL;
        closedir(dir);
    }
    /* Add partitions */
    int npartitions = utarray_len(partitions);
    char **ppartition = (char **)utarray_front(partitions);
    for (int p = 0; p < npartitions; ++p) {
        unsigned int part_size = 0;
        xmlNodePtr part_node = NULL, crt_node = NULL, nodes_node = NULL, cons_node = NULL;
        part_node = xmlNewChild(root_node, NULL, BAD_CAST "partition", NULL);
        /* Set name */
        if (ppartition && 0 < strlen(*ppartition)) {
            set_xml_prop(part_node, BAD_CAST "name", *ppartition);
        }
        /* Add nodes */
        nodes_node = xmlNewChild(part_node, NULL, BAD_CAST "nodes", NULL);
        cons_node = xmlNewChild(part_node, NULL, BAD_CAST "connexions", NULL);
        node_t *node, *node_tmp;
        HASH_ITER(hh, nodes, node, node_tmp) {
            /* Check node belongs to the current partition */
            if (!node->partitions || !node->partitions[p])
                continue;
            else
                ++part_size;
            crt_node = xmlNewChild(nodes_node, NULL, BAD_CAST "node", NULL);
            insert_xml_node(crt_node, node, full_hwloc_path);
            if (node->subnodes) {
                /* VIRTUAL NODE */
                node_t *subnode, *subnode_tmp;
                /* Set virtual */
                xmlNewProp(crt_node, BAD_CAST "virtual", BAD_CAST "yes");
                /* Set size */
                strBuffSize = asprintf(&strBuff, "%u", HASH_COUNT(node->subnodes));
                if (0 < strBuffSize) {
                    set_xml_prop(crt_node, BAD_CAST "size", strBuff);
                }
                free(strBuff); strBuff = NULL;
                /* Add subnodes */
                xmlNodePtr subnodes_node = xmlNewChild(crt_node, NULL, BAD_CAST "subnodes", NULL);
                xmlNodePtr subnode_node;
                HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
                    subnode_node = xmlNewChild(subnodes_node, NULL, BAD_CAST "node", NULL);
                    insert_xml_node(subnode_node, subnode, full_hwloc_path);
                }
            }
            /* Add links and connexions */
            edge_t *edge, *edge_tmp;
            HASH_ITER(hh, node->edges, edge, edge_tmp) {
                /* Check edge belongs to this partition */
                if(!edge->partitions || !edge->partitions[p])
                    continue;
                crt_node = xmlNewChild(cons_node, NULL, BAD_CAST "connexion", NULL);
                insert_xml_edge(crt_node, edge, node);
            }
        }
        /* Set size */
        strBuffSize = asprintf(&strBuff, "%u", part_size);
        if (0 < strBuffSize)
            set_xml_prop(part_node, BAD_CAST "size", strBuff);
        free(strBuff); strBuff = NULL;
        /* Get next partition */
        ppartition = (char **)utarray_next(partitions, ppartition);
    }
    /*
     * Add structural/extra edges
     */
    insert_extra(root_node, full_hwloc_path);
    /* 
     * Dumping document to stdio or file
     */
    char *output_path;
    asprintf(&output_path, "%s/IB-%s-nodes.xml", path, subnet);
    xmlSaveFormatFileEnc(output_path, doc, "UTF-8", 1);
    free(output_path); free(full_hwloc_path);
    /* Free the document */
    xmlFreeDoc(doc);
    /*
     * Free the global variables that may
     * have been allocated by the parser.
     */
    xmlCleanupParser();

    return NETLOC_SUCCESS;
}

#endif
