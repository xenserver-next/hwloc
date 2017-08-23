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

#include <private/utils/xml.h>
#include <netloc/uthash.h>
#include <netloc/utarray.h>
#include <dirent.h>
#include <libgen.h>

/******************************************************************************/
/* Function to handle XML tree */
/******************************************************************************/
typedef struct {
    size_t num;
    size_t allocated;
    void **data;
} contents_t;

typedef struct {
    char *name;
    char *content;
    contents_t attributes;
    contents_t children;
} xml_node_t;

static void contents_init(contents_t *contents, size_t allocated)
{
    contents->data = (void **) (allocated ? malloc(sizeof(void *[allocated])) : NULL);
    contents->allocated = allocated;
    contents->num = 0;
}

static void contents_add(contents_t *contents, void *data)
{
    if (contents->num == contents->allocated) {
        if (contents->allocated)
        {
            void **new_data = (void **)
                realloc(contents->data, sizeof(void *[2*contents->allocated]));
            if (!new_data)
                    return;
            contents->data = new_data;
            contents->allocated *= 2;
        } else {
            contents->data = (void **) malloc(sizeof(void *[1]));
            if (!contents->data)
                return;
            contents->allocated = 1;
        }
    }
    contents->data[contents->num] = data;
    contents->num++;
}

static void contents_end(contents_t *contents)
{
    free(contents->data);
}

static xml_node_t *xml_node_new(char *type)
{
    xml_node_t *node = (xml_node_t *)malloc(sizeof(xml_node_t));
    if (!node) {
        fprintf(stderr, "ERROR: Unable to allocate node.\n");
        return NULL;
    }
    node->name = strdup(type);
    contents_init(&node->attributes, 0);
    contents_init(&node->children, 0);
    node->content = NULL;
    return node;
}

static void xml_node_attr_add(xml_node_t *node, const char *name, const char *value)
{
    int ret;
    char *attr = NULL;
    ret = asprintf(&attr, " %s=\"%s\"", name, value);
    if (0 > ret) {
        fprintf(stderr, "WARN: unable to add attribute named \"%s\"\n", name);
        return;
    }
    contents_add(&node->attributes, attr);
}

static void xml_node_child_add(xml_node_t *node, xml_node_t *child)
{
    contents_add(&node->children, child);
}

static void xml_node_content_add(xml_node_t *node, const char *content)
{
    node->content = strdup(content);
}

static void xml_node_write(FILE *out, xml_node_t *node, unsigned int depth)
{
    size_t i;
    fprintf(out, "%*s<%s", depth * 2, "", node->name);
    for (i = 0; i < node->attributes.num; ++i)
        fprintf(out, "%s", node->attributes.data[i]);
    if (0 == node->children.num && !node->content)
        fprintf(out, "/>\n");
    else if (node->content) {
        fprintf(out, ">%s</%s>\n", node->content, node->name);
    } else { /* Cannot have both content and children: because. */
        fprintf(out, ">\n");
        for(i = 0; i < node->children.num; ++i)
            xml_node_write(out, node->children.data[i], depth + 1);
        fprintf(out, "%*s</%s>\n", depth * 2, "", node->name);
    }
}

static void xml_node_destruct(xml_node_t *node)
{
    size_t i;
    free(node->name);
    free(node->content);
    for (i = 0; i < node->attributes.num; ++i)
        free(node->attributes.data[i]);
    contents_end(&node->attributes);
    for (i = 0; i < node->children.num; ++i)
        xml_node_destruct((xml_node_t *)node->children.data[i]);
    contents_end(&node->children);
    free(node);
}    

/******************************************************************************/

#ifndef HWLOC_HAVE_LIBXML2

static inline void insert_xml_link(xml_node_t *links_node, physical_link_t *link)
{
    char *strBuff = NULL;
    int strBuffSize = 0;
    xml_node_t *crt_node;
    /* Add current connexion description */
    crt_node = xml_node_new("link");
    xml_node_content_add(crt_node, link->description);
    xml_node_child_add(links_node, crt_node);
    /* Set srcport */
    strBuffSize = asprintf(&strBuff, "%d", link->ports[0]);
    if (0 < strBuffSize)
        xml_node_attr_add(crt_node, "srcport", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set destport */
    strBuffSize = asprintf(&strBuff, "%d", link->ports[1]);
    if (0 < strBuffSize)
        xml_node_attr_add(crt_node, "destport", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set speed */
    xml_node_attr_add(crt_node, "speed", link->speed);
    /* Set width */
    xml_node_attr_add(crt_node, "width", link->width);
    /* Set bandwidth */
    strBuffSize = asprintf(&strBuff, "%f", link->gbits);
    if (0 < strBuffSize)
        xml_node_attr_add(crt_node, "bandwidth", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set logical_id */
    strBuffSize = asprintf(&strBuff, "%llu", link->int_id);
    if (0 < strBuffSize)
        xml_node_attr_add(crt_node, "logical_id", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set reverse physical_link->logical_id */
    if (link->other_link && 0 < asprintf(&strBuff, "%llu", link->other_link->int_id))
        xml_node_attr_add(crt_node, "reverse_logical_id", strBuff);
    free(strBuff); strBuff = NULL;
}

static inline void insert_xml_edge(xml_node_t *con_node, edge_t *edge, node_t *node)
{
    char *strBuff = NULL;
    int strBuffSize = 0;
    xml_node_t *crt_node, *links_node;
    /* Set bandwidth */
    strBuffSize = asprintf(&strBuff, "%f", edge->total_gbits);
    if (0 < strBuffSize)
        xml_node_attr_add(con_node, "bandwidth", strBuff);
    free(strBuff); strBuff = NULL;
    /* Set nblinks */
    unsigned int num_links = utarray_len(edge->physical_link_idx);
    strBuffSize = asprintf(&strBuff, "%u", num_links);
    if (0 < strBuffSize)
        xml_node_attr_add(con_node, "nblinks", strBuff);
    free(strBuff); strBuff = NULL;
    /* Add src */
    crt_node = xml_node_new("src");
    xml_node_content_add(crt_node, node->physical_id);
    xml_node_child_add(con_node, crt_node);
    /* Add dest */
    crt_node = xml_node_new("dest");
    xml_node_content_add(crt_node, edge->dest);
    xml_node_child_add(con_node, crt_node);
    if (edge_is_virtual(edge)) {
        xml_node_t *subcons_node, *subcon_node;
        /* Set virtual="yes" */
        xml_node_attr_add(con_node, "virtual", "yes");
        subcons_node = xml_node_new("subconnexions");
        xml_node_child_add(con_node, subcons_node);
        /* Set size */
        unsigned int num_subedges = utarray_len(edge->subedges);
        strBuffSize = asprintf(&strBuff, "%u", num_subedges);
        if (0 < strBuffSize)
            xml_node_attr_add(subcons_node, "size", strBuff);
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
            subcon_node = xml_node_new("connexion");
            xml_node_child_add(subcons_node, subcon_node);
            insert_xml_edge(subcon_node, subedge, real_node);
        }
    } else {
        /* Add links */
        links_node = xml_node_new("links");
        xml_node_child_add(con_node, links_node);
        for (unsigned int l = 0; l < num_links; l++) {
            unsigned int link_idx = *(unsigned int *)
                utarray_eltptr(edge->physical_link_idx, l);
            physical_link_t *link = (physical_link_t *)
                utarray_eltptr(node->physical_links, link_idx);
            insert_xml_link(links_node, link);
        }
    }
}

static inline void insert_xml_node(xml_node_t *crt_node, node_t *node, char *hwloc_path)
{
    char *strBuff = NULL;
    int strBuffSize = 0;
    /* Set mac_addr */
    if (node->physical_id && 0 < strlen(node->physical_id)) {
        xml_node_attr_add(crt_node, "mac_addr", node->physical_id);
    }
    /* Set type */
    xml_node_attr_add(crt_node, "type", netloc_node_type_encode(node->type));
    if (node->hostname && 0 < strlen(node->hostname)) {
        /* Set name */
        xml_node_attr_add(crt_node, "name", node->hostname);
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
                xml_node_attr_add(crt_node, "hwloc_file", 1+strrchr(strBuff, '/'));
            }
            free(strBuff); strBuff = NULL;
        }
    } else if (NETLOC_NODE_TYPE_HOST == node->type) {
        fprintf(stderr, "WARN: Host node with address %s has no hostname\n",
                node->physical_id);
    }
    /* Add description */
    if (node->description && 0 < strlen(node->description)) {
        xml_node_t *desc_node = xml_node_new("description");
        xml_node_content_add(desc_node, node->description);
        xml_node_child_add(crt_node, desc_node);
    }
}

static inline int insert_extra(xml_node_t *root_node, char *full_hwloc_path)
{
    char *strBuff;
    unsigned int part_size = 0, strBuffSize;
    xml_node_t *part_node = NULL, *crt_node = NULL, *nodes_node = NULL, *cons_node = NULL;
    part_node = xml_node_new("partition");
    nodes_node = xml_node_new("nodes");
    xml_node_child_add(part_node, nodes_node);
    cons_node = xml_node_new("connexions");
    xml_node_child_add(part_node, cons_node);
    /* Set name */
    xml_node_attr_add(part_node, "name", "/extra+structural/");
    /* Add nodes */
    node_t *node, *node_tmp;
    HASH_ITER(hh, nodes, node, node_tmp) {
        /* Check if node belongs to no partition */
        if (!node_belongs_to_a_partition(node)) {
            ++part_size;
            crt_node = xml_node_new("node");
            xml_node_child_add(nodes_node, crt_node);
            insert_xml_node(crt_node, node, full_hwloc_path);
            if (node->subnodes) {
                /* VIRTUAL NODE */
                node_t *subnode, *subnode_tmp;
                /* Set virtual */
                xml_node_attr_add(crt_node, "virtual", "yes");
                /* Set size */
                strBuffSize = asprintf(&strBuff, "%u", HASH_COUNT(node->subnodes));
                if (0 < strBuffSize) {
                    xml_node_attr_add(crt_node, "size", strBuff);
                }
                free(strBuff); strBuff = NULL;
                /* Add subnodes */
                xml_node_t *subnodes_node = xml_node_new("subnodes");
                xml_node_child_add(crt_node, subnodes_node);
                xml_node_t *subnode_node;
                HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
                    subnode_node = xml_node_new("node");
                    xml_node_child_add(subnodes_node, subnode_node);
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
            crt_node = xml_node_new("connexion");
            xml_node_child_add(cons_node, crt_node);
            insert_xml_edge(crt_node, edge, node);
        }
    }
    if (!nodes_node->children.num && !cons_node->children.num) {
        xml_node_destruct(part_node);
    } else {
        /* Set size */
        strBuffSize = asprintf(&strBuff, "%u", part_size);
        if (0 < strBuffSize)
            xml_node_attr_add(part_node, "size", strBuff);
        free(strBuff); strBuff = NULL;
        xml_node_child_add(root_node, part_node);
    }
    return NETLOC_SUCCESS;
}

int netloc_nolibxml_write_xml_file(const char *subnet, const char *path, const char *hwlocpath,
                                   const netloc_network_type_t transportType)
{
    char *strBuff = NULL, *full_hwloc_path = NULL;
    int strBuffSize = 0;
    /*
     * Add topology definition tag
     */
    /* Creates a new document, a node and set it as a root node. */
    xml_node_t *root_node = xml_node_new("topology");
    /* Set version */
    xml_node_attr_add(root_node, "version", NETLOC_STR_VERS(NETLOCFILE_VERSION_2_0));
    /* Set transport */
    xml_node_attr_add(root_node, "transport", netloc_network_type_encode(transportType));
    /* Add subnet node */
    if (subnet && 0 < strlen(subnet)) {
        xml_node_t *subnet_node = xml_node_new("subnet");
        xml_node_content_add(subnet_node, subnet);
        xml_node_child_add(root_node, subnet_node);
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
        xml_node_t *hwlocpath_node = xml_node_new("hwloc_path");
        xml_node_content_add(hwlocpath_node, hwlocpath);
        xml_node_child_add(root_node, hwlocpath_node);
        closedir(dir);
    }
    /* Add partitions */
    int npartitions = utarray_len(partitions);
    char **ppartition = (char **)utarray_front(partitions);
    for (int p = 0; p < npartitions; ++p) {
        unsigned int part_size = 0;
        xml_node_t *part_node = NULL, *crt_node = NULL, *nodes_node = NULL, *cons_node = NULL;
        part_node = xml_node_new("partition");
        xml_node_child_add(root_node, part_node);
        /* Set name */
        if (ppartition && 0 < strlen(*ppartition)) {
            xml_node_attr_add(part_node, "name", *ppartition);
        }
        /* Add nodes */
        nodes_node = xml_node_new("nodes");
        xml_node_child_add(part_node, nodes_node);
        cons_node = xml_node_new("connexions");
        xml_node_child_add(part_node, cons_node);
        node_t *node, *node_tmp;
        HASH_ITER(hh, nodes, node, node_tmp) {
            /* Check node belongs to the current partition */
            if (!node->partitions || !node->partitions[p])
                continue;
            else
                ++part_size;
            crt_node = xml_node_new("node");
            xml_node_child_add(nodes_node, crt_node);
            insert_xml_node(crt_node, node, full_hwloc_path);
            if (node->subnodes) {
                /* VIRTUAL NODE */
                node_t *subnode, *subnode_tmp;
                /* Set virtual */
                xml_node_attr_add(crt_node, "virtual", "yes");
                /* Set size */
                strBuffSize = asprintf(&strBuff, "%u", HASH_COUNT(node->subnodes));
                if (0 < strBuffSize) {
                    xml_node_attr_add(crt_node, "size", strBuff);
                }
                free(strBuff); strBuff = NULL;
                /* Add subnodes */
                xml_node_t *subnodes_node = xml_node_new("subnodes");
                xml_node_child_add(crt_node, subnodes_node);
                xml_node_t *subnode_node;
                HASH_ITER(hh, node->subnodes, subnode, subnode_tmp) {
                    subnode_node = xml_node_new("node");
                    xml_node_child_add(subnodes_node, subnode_node);
                    insert_xml_node(subnode_node, subnode, full_hwloc_path);
                }
            }
            /* Add links and connexions */
            edge_t *edge, *edge_tmp;
            HASH_ITER(hh, node->edges, edge, edge_tmp) {
                /* Check edge belongs to this partition */
                if(!edge->partitions || !edge->partitions[p])
                    continue;
                crt_node = xml_node_new("connexion");
                xml_node_child_add(cons_node, crt_node);
                insert_xml_edge(crt_node, edge, node);
            }
        }
        /* Set size */
        strBuffSize = asprintf(&strBuff, "%u", part_size);
        if (0 < strBuffSize)
            xml_node_attr_add(part_node, "size", strBuff);
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
    FILE *out = fopen(output_path, "w");
    if (!out)
        return NETLOC_ERROR;
    fprintf(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    xml_node_write(out, root_node, 0);
    /* Close file, and free path */
    fclose(out);
    free(output_path); free(full_hwloc_path);
    /* Free the XML nodes */
    xml_node_destruct(root_node);

    return NETLOC_SUCCESS;
}

#endif
